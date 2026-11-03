#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/serial.h"
#include "storage/block_device.h"
#include "virtio_blk.h"

#define VIRTIO_PCI_VENDOR_ID 0x1af4U
#define VIRTIO_PCI_BLOCK_LEGACY_ID 0x1001U
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

#define VIRTIO_BLK_F_RO (1U << 5)
#define VIRTIO_BLK_F_FLUSH (1U << 9)
#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_T_OUT 1U
#define VIRTIO_BLK_T_FLUSH 4U
#define VIRTIO_BLK_S_OK 0U
#define VIRTIO_BLK_DESCRIPTOR_NEXT 1U
#define VIRTIO_BLK_DESCRIPTOR_DEVICE_WRITABLE 2U

#define VIRTIO_QUEUE_MAX_SIZE 256U
#define VIRTIO_QUEUE_MIN_SIZE 4U
#define VIRTIO_QUEUE_MEMORY_BYTES 16384U
#define VIRTIO_DMA_TIMEOUT_POLLS 10000000U
#define VIRTIO_SECTOR_SIZE 512U

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

struct virtio_block_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

struct virtio_block_state {
    uint16_t io_base;
    uint16_t queue_size;
    uint16_t used_ring_offset;
    uint16_t available_index;
    uint32_t negotiated_features;
    uint64_t sector_count;
    uint64_t queue_physical_address;
    uint64_t transfer_physical_address;
    uint64_t header_physical_address;
    uint64_t status_physical_address;
    struct virtio_block_request_header request_header;
    volatile uint8_t request_status;
    bool read_only;
    bool initialized;
    bool request_in_progress;
};

static uint8_t queue_memory[VIRTIO_QUEUE_MEMORY_BYTES]
    __attribute__((aligned(4096)));
static uint8_t transfer_buffer[BLOCK_DEVICE_MAX_TRANSFER_BYTES]
    __attribute__((aligned(16)));
static struct virtio_block_state block_state;

_Static_assert(sizeof(struct virtio_queue_descriptor) == 16,
               "VirtIO descriptor layout must be 16 bytes");
_Static_assert(sizeof(struct virtio_queue_used_element) == 8,
               "VirtIO used element layout must be 8 bytes");

static struct virtio_queue_descriptor *queue_descriptors(void) {
    return (struct virtio_queue_descriptor *)queue_memory;
}

static volatile struct virtio_queue_available *queue_available(void) {
    uintptr_t address = (uintptr_t)queue_memory
        + sizeof(struct virtio_queue_descriptor) * VIRTIO_QUEUE_MAX_SIZE;
    return (volatile struct virtio_queue_available *)address;
}

static volatile struct virtio_queue_used *queue_used(void) {
    return (volatile struct virtio_queue_used *)(uintptr_t)
        (queue_memory + block_state.used_ring_offset);
}

static bool translate_contiguous_buffer(const void *buffer, uint64_t length,
                                        uint64_t *physical_address) {
    uint64_t virtual_start = (uint64_t)(uintptr_t)buffer;
    uint64_t virtual_end;
    uint64_t physical_start;

    if (buffer == 0 || length == 0 || physical_address == 0
        || virtual_start > UINT64_MAX - (length - 1)
        || !x86_virtual_to_physical(virtual_start, &physical_start)) {
        return false;
    }
    virtual_end = virtual_start + length - 1;

    for (uint64_t offset = 4096; offset < length; offset += 4096) {
        uint64_t physical_page;
        if (!x86_virtual_to_physical(virtual_start + offset,
                                     &physical_page)
            || physical_page != physical_start + offset) {
            return false;
        }
    }

    uint64_t physical_end;
    return x86_virtual_to_physical(virtual_end, &physical_end)
        && physical_end == physical_start + length - 1
        && (*physical_address = physical_start, true);
}

static void set_failed_status(void) {
    uint8_t status = x86_inb(block_state.io_base + VIRTIO_PCI_STATUS);
    x86_outb(block_state.io_base + VIRTIO_PCI_STATUS,
             status | VIRTIO_STATUS_FAILED);
}

static bool configure_queue(void) {
    uint64_t queue_physical_address;
    uint16_t maximum_size;
    uint64_t first_ring_bytes;
    uint64_t used_ring_bytes;
    uint64_t used_ring_offset;

    if (!translate_contiguous_buffer(queue_memory, sizeof(queue_memory),
                                     &queue_physical_address)
        || (queue_physical_address & 4095U) != 0
        || (queue_physical_address >> 12) > UINT32_MAX) {
        return false;
    }

    x86_outw(block_state.io_base + VIRTIO_PCI_QUEUE_SELECT, 0);
    maximum_size = x86_inw(block_state.io_base + VIRTIO_PCI_QUEUE_SIZE);
    if (maximum_size < VIRTIO_QUEUE_MIN_SIZE
        || maximum_size > VIRTIO_QUEUE_MAX_SIZE
        || (maximum_size & (maximum_size - 1U)) != 0) {
        return false;
    }

    block_state.queue_size = maximum_size;
    first_ring_bytes = (uint64_t)sizeof(struct virtio_queue_descriptor)
        * maximum_size + sizeof(uint16_t) * (3U + maximum_size);
    used_ring_bytes = sizeof(uint16_t) * 3U
        + sizeof(struct virtio_queue_used_element) * maximum_size;
    used_ring_offset = (first_ring_bytes + 4095U) & ~4095ULL;
    if (used_ring_offset + ((used_ring_bytes + 4095U) & ~4095ULL)
        > sizeof(queue_memory)) {
        return false;
    }
    block_state.used_ring_offset = (uint16_t)used_ring_offset;
    block_state.queue_physical_address = queue_physical_address;
    x86_outl(block_state.io_base + VIRTIO_PCI_QUEUE_ADDRESS,
             (uint32_t)(queue_physical_address >> 12));
    return x86_inl(block_state.io_base + VIRTIO_PCI_QUEUE_ADDRESS)
        == (uint32_t)(queue_physical_address >> 12);
}

static bool initialize_dma_addresses(void) {
    uint64_t header_address = (uint64_t)(uintptr_t)&block_state.request_header;
    uint64_t status_address = (uint64_t)(uintptr_t)&block_state.request_status;

    return translate_contiguous_buffer(transfer_buffer,
                                       sizeof(transfer_buffer),
                                       &block_state.transfer_physical_address)
        && x86_virtual_to_physical(header_address,
                                  &block_state.header_physical_address)
        && x86_virtual_to_physical(status_address,
                                  &block_state.status_physical_address);
}

static bool submit_request(uint32_t request_type, uint64_t sector,
                           uint32_t data_length, bool device_writes_data) {
    struct virtio_queue_descriptor *descriptors = queue_descriptors();
    volatile struct virtio_queue_available *available = queue_available();
    volatile struct virtio_queue_used *used = queue_used();
    uint16_t previous_used_index = used->index;
    bool has_data = data_length != 0;

    if (block_state.request_in_progress) {
        return false;
    }
    block_state.request_in_progress = true;
    block_state.request_header.type = request_type;
    block_state.request_header.reserved = 0;
    block_state.request_header.sector = sector;
    block_state.request_status = 0xffU;

    descriptors[0] = (struct virtio_queue_descriptor){
        .address = block_state.header_physical_address,
        .length = sizeof(block_state.request_header),
        .flags = VIRTIO_BLK_DESCRIPTOR_NEXT,
        .next = 1
    };
    if (has_data) {
        descriptors[1] = (struct virtio_queue_descriptor){
            .address = block_state.transfer_physical_address,
            .length = data_length,
            .flags = VIRTIO_BLK_DESCRIPTOR_NEXT
                | (device_writes_data
                   ? VIRTIO_BLK_DESCRIPTOR_DEVICE_WRITABLE : 0),
            .next = 2
        };
        descriptors[2] = (struct virtio_queue_descriptor){
            .address = block_state.status_physical_address,
            .length = sizeof(block_state.request_status),
            .flags = VIRTIO_BLK_DESCRIPTOR_DEVICE_WRITABLE,
            .next = 0
        };
    } else {
        descriptors[1] = (struct virtio_queue_descriptor){
            .address = block_state.status_physical_address,
            .length = sizeof(block_state.request_status),
            .flags = VIRTIO_BLK_DESCRIPTOR_DEVICE_WRITABLE,
            .next = 0
        };
    }

    available->ring[block_state.available_index % block_state.queue_size] = 0;
    __asm__ volatile ("sfence" : : : "memory");
    block_state.available_index++;
    available->index = block_state.available_index;
    __asm__ volatile ("sfence" : : : "memory");
    x86_outw(block_state.io_base + VIRTIO_PCI_QUEUE_NOTIFY, 0);

    for (uint32_t poll = 0; poll < VIRTIO_DMA_TIMEOUT_POLLS; poll++) {
        if (used->index != previous_used_index) {
            __asm__ volatile ("lfence" : : : "memory");
            uint32_t used_id = used->ring[
                previous_used_index % block_state.queue_size].id;
            bool completed = used_id == 0
                && block_state.request_status == VIRTIO_BLK_S_OK;
            block_state.request_in_progress = false;
            (void)x86_inb(block_state.io_base + VIRTIO_PCI_STATUS + 1);
            if (!completed) {
                serial_write("VirtIO request failed: used-id=0x");
                serial_write_hex32(used_id);
                serial_write(" status=0x");
                serial_write_hex32(block_state.request_status);
                serial_write("\n");
            }
            return completed;
        }
        __asm__ volatile ("pause" : : : "memory");
    }

    block_state.request_in_progress = false;
    serial_write("VirtIO request timeout: avail=0x");
    serial_write_hex32(available->index);
    serial_write(" used=0x");
    serial_write_hex32(used->index);
    serial_write(" status=0x");
    serial_write_hex32(block_state.request_status);
    serial_write("\n");
    return false;
}

static bool virtio_block_read(void *context, uint64_t lba, uint32_t count,
                              void *buffer) {
    struct virtio_block_state *state = context;
    uint64_t transfer_size = (uint64_t)count * VIRTIO_SECTOR_SIZE;
    uint8_t *destination = buffer;

    if (state != &block_state || !state->initialized || buffer == 0
        || transfer_size == 0
        || transfer_size > sizeof(transfer_buffer)
        || lba >= state->sector_count
        || count > state->sector_count - lba) {
        return false;
    }
    if (!submit_request(VIRTIO_BLK_T_IN, lba, (uint32_t)transfer_size, true)) {
        return false;
    }
    for (uint64_t i = 0; i < transfer_size; i++) {
        destination[i] = transfer_buffer[i];
    }
    return true;
}

static bool virtio_block_write(void *context, uint64_t lba, uint32_t count,
                               const void *buffer) {
    struct virtio_block_state *state = context;
    uint64_t transfer_size = (uint64_t)count * VIRTIO_SECTOR_SIZE;
    const uint8_t *source = buffer;

    if (state != &block_state || !state->initialized || state->read_only
        || buffer == 0 || transfer_size == 0
        || transfer_size > sizeof(transfer_buffer)
        || lba >= state->sector_count
        || count > state->sector_count - lba) {
        return false;
    }
    for (uint64_t i = 0; i < transfer_size; i++) {
        transfer_buffer[i] = source[i];
    }
    return submit_request(VIRTIO_BLK_T_OUT, lba, (uint32_t)transfer_size,
                          false);
}

static bool virtio_block_flush(void *context) {
    struct virtio_block_state *state = context;

    return state == &block_state && state->initialized
        && (state->negotiated_features & VIRTIO_BLK_F_FLUSH) != 0
        && submit_request(VIRTIO_BLK_T_FLUSH, 0, 0, false);
}

static void set_string(char *destination, size_t capacity,
                       const char *source) {
    size_t i = 0;
    while (i + 1 < capacity && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

bool virtio_blk_initialize(const struct pci_device_info *device) {
    const struct pci_bar_info *bar;
    struct block_device registration = {0};
    uint64_t bar_size;
    uint32_t host_features;
    uint32_t accepted_features;
    uint64_t low_capacity;
    uint64_t high_capacity;
    uint8_t status;

    if (device == 0 || device->vendor_id != VIRTIO_PCI_VENDOR_ID
        || device->device_id != VIRTIO_PCI_BLOCK_LEGACY_ID
        || device->bar_count == 0 || block_state.initialized) {
        return false;
    }

    bar = &device->bars[VIRTIO_PCI_BAR_INDEX];
    if (!bar->valid || bar->kind != PCI_BAR_IO || bar->base_address == 0
        || bar->base_address + VIRTIO_PCI_MINIMUM_BAR_SIZE > 0x10000ULL
        || !pci_probe_bar_size(device, bar, &bar_size)
        || bar_size < VIRTIO_PCI_MINIMUM_BAR_SIZE
        || !pci_enable_io_bus_master(device)) {
        return false;
    }

    block_state = (struct virtio_block_state){0};
    for (uint32_t i = 0; i < sizeof(queue_memory); i++) {
        queue_memory[i] = 0;
    }
    block_state.io_base = (uint16_t)bar->base_address;
    x86_outb(block_state.io_base + VIRTIO_PCI_STATUS, 0);
    x86_outb(block_state.io_base + VIRTIO_PCI_STATUS,
             VIRTIO_STATUS_ACKNOWLEDGE);
    x86_outb(block_state.io_base + VIRTIO_PCI_STATUS,
             VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    host_features = x86_inl(block_state.io_base + VIRTIO_PCI_HOST_FEATURES);
    accepted_features = host_features & (VIRTIO_BLK_F_RO | VIRTIO_BLK_F_FLUSH);
    block_state.negotiated_features = accepted_features;
    block_state.read_only = (accepted_features & VIRTIO_BLK_F_RO) != 0;
    x86_outl(block_state.io_base + VIRTIO_PCI_GUEST_FEATURES,
             accepted_features);

    if (!initialize_dma_addresses() || !configure_queue()) {
        set_failed_status();
        return false;
    }

    low_capacity = x86_inl(block_state.io_base + VIRTIO_PCI_CONFIG);
    high_capacity = x86_inl(block_state.io_base + VIRTIO_PCI_CONFIG + 4);
    block_state.sector_count = low_capacity | (high_capacity << 32);
    if (block_state.sector_count == 0) {
        set_failed_status();
        return false;
    }

    status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
        | VIRTIO_STATUS_DRIVER_OK;
    x86_outb(block_state.io_base + VIRTIO_PCI_STATUS, status);
    if (x86_inb(block_state.io_base + VIRTIO_PCI_STATUS) != status) {
        set_failed_status();
        return false;
    }

    set_string(registration.name, sizeof(registration.name), "virtio0");
    set_string(registration.model, sizeof(registration.model), "VirtIO Block");
    registration.sector_size = VIRTIO_SECTOR_SIZE;
    registration.sector_count = block_state.sector_count;
    registration.read_only = block_state.read_only;
    registration.context = &block_state;
    registration.read = virtio_block_read;
    registration.write = block_state.read_only ? 0 : virtio_block_write;
    registration.flush = (accepted_features & VIRTIO_BLK_F_FLUSH) != 0
        ? virtio_block_flush : 0;

    if (!block_device_register(&registration)) {
        set_failed_status();
        return false;
    }

    block_state.initialized = true;
    serial_write("  VirtIO queue size=");
    serial_write_decimal(block_state.queue_size);
    serial_write(" dma queue=0x");
    serial_write_hex(block_state.queue_physical_address);
    serial_write("\n");
    return true;
}
