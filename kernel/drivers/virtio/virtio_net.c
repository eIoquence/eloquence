#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/serial.h"
#include "net/arp.h"
#include "net/ethernet.h"
#include "net/netif.h"
#include "virtio_net.h"

#define VIRTIO_PCI_VENDOR_ID 0x1af4U
#define VIRTIO_PCI_NET_LEGACY_ID 0x1000U
#define VIRTIO_PCI_BAR_INDEX 0U

#define VIRTIO_PCI_HOST_FEATURES 0U
#define VIRTIO_PCI_GUEST_FEATURES 4U
#define VIRTIO_PCI_QUEUE_ADDRESS 8U
#define VIRTIO_PCI_QUEUE_SIZE 12U
#define VIRTIO_PCI_QUEUE_SELECT 14U
#define VIRTIO_PCI_QUEUE_NOTIFY 16U
#define VIRTIO_PCI_STATUS 18U
#define VIRTIO_PCI_CONFIG 20U
#define VIRTIO_PCI_MINIMUM_BAR_SIZE 28ULL

#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER 2U
#define VIRTIO_STATUS_DRIVER_OK 4U
#define VIRTIO_STATUS_FAILED 128U

#define VIRTIO_NET_F_MAC (1U << 5U)

#define VIRTIO_NET_QUEUE_RX 0U
#define VIRTIO_NET_QUEUE_TX 1U
#define VIRTIO_NET_QUEUE_COUNT 2U

#define VIRTIO_QUEUE_MAX_SIZE 256U
#define VIRTIO_QUEUE_MIN_SIZE 4U
#define VIRTIO_QUEUE_MEMORY_BYTES 16384U
#define VIRTIO_DMA_TIMEOUT_POLLS 10000000U

#define VIRTIO_NET_HEADER_LEN 12U
#define VIRTIO_NET_DESCRIPTOR_DEVICE_WRITABLE 2U
#define VIRTIO_NET_DESCRIPTOR_NEXT 1U

struct virtio_queue_descriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtio_queue_available {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTIO_QUEUE_MAX_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct virtio_queue_used_element {
    uint32_t id;
    uint32_t length;
} __attribute__((packed));

struct virtio_queue_used {
    uint16_t flags;
    uint16_t index;
    struct virtio_queue_used_element ring[VIRTIO_QUEUE_MAX_SIZE];
    uint16_t available_event;
} __attribute__((packed));

_Static_assert(sizeof(struct virtio_queue_descriptor) == 16,
               "VirtIO descriptor layout must be 16 bytes");
_Static_assert(sizeof(struct virtio_queue_used_element) == 8,
               "VirtIO used element layout must be 8 bytes");

static uint8_t rx_queue_memory[VIRTIO_QUEUE_MEMORY_BYTES]
    __attribute__((aligned(4096)));
static uint8_t tx_queue_memory[VIRTIO_QUEUE_MEMORY_BYTES]
    __attribute__((aligned(4096)));

/* RX buffers: the device prefixes a 12-byte virtio-net header to every
 * received frame, so each slot holds header + frame. */
static uint8_t rx_buffers[NET_RECEIVE_QUEUE][VIRTIO_NET_HEADER_LEN
                                              + NET_MAX_PACKET]
    __attribute__((aligned(16)));
static uint8_t tx_buffer[VIRTIO_NET_HEADER_LEN + NET_MAX_PACKET]
    __attribute__((aligned(16)));

struct virtio_net_state {
    uint16_t io_base;
    uint8_t mac[ETH_ADDR_LEN];
    uint32_t negotiated_features;
    uint16_t queue_size;
    bool link_up;
    bool initialized;

    volatile struct virtio_queue_descriptor *rx_desc;
    volatile struct virtio_queue_available *rx_avail;
    volatile struct virtio_queue_used *rx_used;
    uint16_t rx_avail_index;
    uint16_t rx_used_index;

    volatile struct virtio_queue_descriptor *tx_desc;
    volatile struct virtio_queue_available *tx_avail;
    volatile struct virtio_queue_used *tx_used;
    uint16_t tx_avail_index;
    uint16_t tx_used_index;
};

static struct virtio_net_state net;

static void queue_layout(volatile struct virtio_queue_descriptor **out_desc,
                         volatile struct virtio_queue_available **out_avail,
                         volatile struct virtio_queue_used **out_used,
                         uint16_t queue_size, uint8_t *memory) {
    uint64_t first_ring_bytes = (uint64_t)sizeof(struct virtio_queue_descriptor)
        * queue_size + sizeof(uint16_t) * (3U + queue_size);
    uint64_t used_ring_bytes = sizeof(uint16_t) * 3U
        + sizeof(struct virtio_queue_used_element) * queue_size;
    uint64_t used_ring_offset = (first_ring_bytes + 4095U) & ~4095ULL;

    *out_desc = (volatile struct virtio_queue_descriptor *)memory;
    *out_avail = (volatile struct virtio_queue_available *)
        (memory + sizeof(struct virtio_queue_descriptor) * queue_size);
    *out_used = (volatile struct virtio_queue_used *)(memory + used_ring_offset);
    (void)used_ring_bytes;
}

static bool queue_memory_contiguous(uint8_t *memory) {
    uint64_t physical = 0;
    return x86_virtual_to_physical((uint64_t)(uintptr_t)memory, &physical)
        && (physical & 4095U) == 0
        && (physical >> 12) <= UINT32_MAX;
}

static bool setup_queues(void) {
    uint16_t maximum_size;

    if (!queue_memory_contiguous(rx_queue_memory)
        || !queue_memory_contiguous(tx_queue_memory)) {
        return false;
    }

    /* RX queue. */
    x86_outw(net.io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_QUEUE_RX);
    maximum_size = x86_inw(net.io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (maximum_size < VIRTIO_QUEUE_MIN_SIZE
        || maximum_size > VIRTIO_QUEUE_MAX_SIZE
        || (maximum_size & (maximum_size - 1U)) != 0) {
        return false;
    }
    net.queue_size = maximum_size;
    queue_layout(&net.rx_desc, &net.rx_avail, &net.rx_used, maximum_size,
                 rx_queue_memory);

    uint64_t rx_queue_physical;
    if (!x86_virtual_to_physical((uint64_t)(uintptr_t)rx_queue_memory,
                                 &rx_queue_physical)
        || (rx_queue_physical & 4095U) != 0
        || (rx_queue_physical >> 12) > UINT32_MAX) {
        return false;
    }
    x86_outl(net.io_base + VIRTIO_PCI_QUEUE_ADDRESS,
             (uint32_t)(rx_queue_physical >> 12));
    if (x86_inl(net.io_base + VIRTIO_PCI_QUEUE_ADDRESS)
        != (uint32_t)(rx_queue_physical >> 12)) {
        return false;
    }

    /* TX queue. */
    x86_outw(net.io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_QUEUE_TX);
    maximum_size = x86_inw(net.io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (maximum_size < VIRTIO_QUEUE_MIN_SIZE
        || maximum_size > VIRTIO_QUEUE_MAX_SIZE
        || (maximum_size & (maximum_size - 1U)) != 0) {
        return false;
    }
    queue_layout(&net.tx_desc, &net.tx_avail, &net.tx_used, maximum_size,
                 tx_queue_memory);

    uint64_t tx_queue_physical;
    if (!x86_virtual_to_physical((uint64_t)(uintptr_t)tx_queue_memory,
                                 &tx_queue_physical)
        || (tx_queue_physical & 4095U) != 0
        || (tx_queue_physical >> 12) > UINT32_MAX) {
        return false;
    }
    x86_outl(net.io_base + VIRTIO_PCI_QUEUE_ADDRESS,
             (uint32_t)(tx_queue_physical >> 12));
    if (x86_inl(net.io_base + VIRTIO_PCI_QUEUE_ADDRESS)
        != (uint32_t)(tx_queue_physical >> 12)) {
        return false;
    }

    return true;
}

/* Pre-fill the RX avail ring with descriptors pointing at the receive
 * buffers. Each buffer is a single-descriptor chain, device-writable. */
static bool fill_rx_buffers(void) {
    for (uint32_t i = 0; i < NET_RECEIVE_QUEUE; i++) {
        uint64_t physical = 0;
        if (!x86_virtual_to_physical(
                (uint64_t)(uintptr_t)rx_buffers[i], &physical)) {
            return false;
        }
        net.rx_desc[i] = (struct virtio_queue_descriptor){
            .address = physical,
            .length = VIRTIO_NET_HEADER_LEN + NET_MAX_PACKET,
            .flags = VIRTIO_NET_DESCRIPTOR_DEVICE_WRITABLE,
            .next = 0
        };
        net.rx_avail->ring[i] = (uint16_t)i;
    }
    __asm__ volatile ("sfence" : : : "memory");
    net.rx_avail->index = (uint16_t)NET_RECEIVE_QUEUE;
    __asm__ volatile ("sfence" : : : "memory");
    x86_outw(net.io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_QUEUE_RX);
    x86_outw(net.io_base + VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_NET_QUEUE_RX);
    return true;
}

static bool read_mac_address(void) {
    uint32_t low = x86_inl(net.io_base + VIRTIO_PCI_CONFIG + 0);
    uint32_t high = x86_inl(net.io_base + VIRTIO_PCI_CONFIG + 4);
    net.mac[0] = (uint8_t)low;
    net.mac[1] = (uint8_t)(low >> 8);
    net.mac[2] = (uint8_t)(low >> 16);
    net.mac[3] = (uint8_t)(low >> 24);
    net.mac[4] = (uint8_t)high;
    net.mac[5] = (uint8_t)(high >> 8);
    return true;
}

static bool transmit_frame(const uint8_t *frame, uint32_t length) {
    if (frame == 0 || length == 0 || length > NET_MAX_PACKET
        || !net.initialized) {
        return false;
    }
    volatile struct virtio_queue_descriptor *descriptors = net.tx_desc;
    volatile struct virtio_queue_available *available = net.tx_avail;
    volatile struct virtio_queue_used *used = net.tx_used;

    uint64_t tx_physical = 0;
    if (!x86_virtual_to_physical((uint64_t)(uintptr_t)tx_buffer, &tx_physical)) {
        return false;
    }

    /* Build the transmit frame: 12-byte (zeroed) virtio-net header followed
     * by the Ethernet frame. Descriptor 0 = header, descriptor 1 = frame. */
    for (uint32_t i = 0; i < VIRTIO_NET_HEADER_LEN; i++) {
        tx_buffer[i] = 0;
    }
    for (uint32_t i = 0; i < length; i++) {
        tx_buffer[VIRTIO_NET_HEADER_LEN + i] = frame[i];
    }

    descriptors[0] = (struct virtio_queue_descriptor){
        .address = tx_physical,
        .length = VIRTIO_NET_HEADER_LEN,
        .flags = 0,
        .next = 1
    };
    descriptors[1] = (struct virtio_queue_descriptor){
        .address = tx_physical + VIRTIO_NET_HEADER_LEN,
        .length = length,
        .flags = 0,
        .next = 0
    };

    uint16_t previous_used = used->index;
    available->ring[net.tx_avail_index % net.queue_size] = 0;
    __asm__ volatile ("sfence" : : : "memory");
    net.tx_avail_index++;
    available->index = net.tx_avail_index;
    __asm__ volatile ("sfence" : : : "memory");
    x86_outw(net.io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_QUEUE_TX);
    x86_outw(net.io_base + VIRTIO_PCI_QUEUE_NOTIFY,
             VIRTIO_NET_QUEUE_TX);

    for (uint32_t poll = 0; poll < VIRTIO_DMA_TIMEOUT_POLLS; poll++) {
        if (used->index != previous_used) {
            __asm__ volatile ("lfence" : : : "memory");
            return true;
        }
        __asm__ volatile ("pause" : : : "memory");
    }
    return false;
}

static bool netif_poll_rx(void) {
    volatile struct virtio_queue_used *used = net.rx_used;
    uint16_t queue_size = net.queue_size;
    bool any_returned = false;

    while (net.rx_used_index != used->index) {
        __asm__ volatile ("lfence" : : : "memory");
        uint32_t id = used->ring[net.rx_used_index % queue_size].id;
        uint32_t total_length =
            used->ring[net.rx_used_index % queue_size].length;
        net.rx_used_index++;

        if (id < NET_RECEIVE_QUEUE && total_length > VIRTIO_NET_HEADER_LEN) {
            uint8_t *buffer = rx_buffers[id];
            uint32_t frame_length = total_length - VIRTIO_NET_HEADER_LEN;
            (void)ethernet_input(buffer + VIRTIO_NET_HEADER_LEN,
                                 frame_length);
        }

        /* Return the buffer to the avail ring. */
        net.rx_desc[id] = (struct virtio_queue_descriptor){
            .address = 0,
            .length = VIRTIO_NET_HEADER_LEN + NET_MAX_PACKET,
            .flags = VIRTIO_NET_DESCRIPTOR_DEVICE_WRITABLE,
            .next = 0
        };
        uint64_t physical = 0;
        if (x86_virtual_to_physical(
                (uint64_t)(uintptr_t)rx_buffers[id], &physical)) {
            net.rx_desc[id].address = physical;
        }
        net.rx_avail->ring[net.rx_avail_index % queue_size] = id;
        __asm__ volatile ("sfence" : : : "memory");
        net.rx_avail_index++;
        net.rx_avail->index = net.rx_avail_index;
        __asm__ volatile ("sfence" : : : "memory");
        any_returned = true;
    }
    if (any_returned) {
        x86_outw(net.io_base + VIRTIO_PCI_QUEUE_SELECT, VIRTIO_NET_QUEUE_RX);
        x86_outw(net.io_base + VIRTIO_PCI_QUEUE_NOTIFY, VIRTIO_NET_QUEUE_RX);
    }
    return true;
}

bool virtio_net_initialize(const struct pci_device_info *device) {
    const struct pci_bar_info *bar;
    uint32_t host_features;
    uint32_t accepted_features;
    uint8_t status;

    if (device == 0 || device->vendor_id != VIRTIO_PCI_VENDOR_ID
        || (device->device_id != VIRTIO_PCI_NET_LEGACY_ID
            && device->device_id != 0x1004U)
        || device->bar_count == 0 || net.initialized) {
        return false;
    }

    bar = &device->bars[VIRTIO_PCI_BAR_INDEX];
    if (!bar->valid || bar->kind != PCI_BAR_IO || bar->base_address == 0
        || bar->base_address + VIRTIO_PCI_MINIMUM_BAR_SIZE > 0x10000ULL) {
        return false;
    }
    uint64_t bar_size = 0;
    if (!pci_probe_bar_size(device, bar, &bar_size)
        || bar_size < VIRTIO_PCI_MINIMUM_BAR_SIZE
        || !pci_enable_io_bus_master(device)) {
        return false;
    }

    net = (struct virtio_net_state){0};
    net.io_base = (uint16_t)bar->base_address;

    x86_outb(net.io_base + VIRTIO_PCI_STATUS, 0);
    x86_outb(net.io_base + VIRTIO_PCI_STATUS,
             VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    host_features = x86_inl(net.io_base + VIRTIO_PCI_HOST_FEATURES);
    accepted_features = host_features & VIRTIO_NET_F_MAC;
    net.negotiated_features = accepted_features;
    x86_outl(net.io_base + VIRTIO_PCI_GUEST_FEATURES, accepted_features);

    if (!setup_queues()) {
        x86_outb(net.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        return false;
    }

    if (!fill_rx_buffers() || !read_mac_address()) {
        x86_outb(net.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        return false;
    }
    net.rx_avail_index = (uint16_t)NET_RECEIVE_QUEUE;

    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
        | VIRTIO_STATUS_DRIVER_OK;
    x86_outb(net.io_base + VIRTIO_PCI_STATUS, status);
    if (x86_inb(net.io_base + VIRTIO_PCI_STATUS) != status) {
        x86_outb(net.io_base + VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
        return false;
    }

    net.link_up = true;
    net.initialized = true;
    netif_set_link_up(true);
    /* QEMU user networking: guest 10.0.2.15, gateway 10.0.2.2. */
    netif_set_addresses(net.mac, 0x0a00020fU);
    netif_register_poll(virtio_net_poll);
    if (!netif_register_driver(transmit_frame)) {
        return false;
    }
    serial_write("VirtIO network driver initialized, MAC ");
    serial_write_hex32(((uint32_t)net.mac[0] << 24)
                       | ((uint32_t)net.mac[1] << 16)
                       | ((uint32_t)net.mac[2] << 8) | net.mac[3]);
    serial_write("\n");
    return true;
}

void virtio_net_poll(void) {
    if (!net.initialized) {
        return;
    }
    netif_poll_rx();
}
