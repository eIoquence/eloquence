#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "pci.h"
#include "mmio.h"
#include "memory/page_allocator.h"
#include "serial.h"

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_bootloader_info_request bootloader_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

static __attribute__((noreturn)) void halt_cpu(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static __attribute__((noreturn)) void panic(const char *message) {
    serial_write("PANIC: ");
    serial_write(message);
    serial_write("\n");
    halt_cpu();
}

static const char *memory_type_name(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return "usable";
        case LIMINE_MEMMAP_RESERVED: return "reserved";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return "acpi-reclaimable";
        case LIMINE_MEMMAP_ACPI_NVS: return "acpi-nvs";
        case LIMINE_MEMMAP_BAD_MEMORY: return "bad-memory";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "bootloader-reclaimable";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "kernel-and-modules";
        case LIMINE_MEMMAP_FRAMEBUFFER: return "framebuffer";
        case LIMINE_MEMMAP_RESERVED_MAPPED: return "reserved-mapped";
        default: return "unknown";
    }
}

static void report_memory_map(void) {
    struct limine_memmap_response *response = memory_map_request.response;

    if (response == 0) {
        panic("Limine did not return a memory map");
    }
    if (response->entry_count != 0 && response->entries == 0) {
        panic("memory map entry list is null");
    }

    serial_write("Physical memory map entries: ");
    serial_write_decimal(response->entry_count);
    serial_write("\n");

    for (uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry *entry = response->entries[i];

        if (entry == 0) {
            panic("memory map contains a null entry");
        }

        serial_write("  [");
        serial_write_decimal(i);
        serial_write("] base=0x");
        serial_write_hex(entry->base);
        serial_write(" length=0x");
        serial_write_hex(entry->length);
        serial_write(" type=");
        serial_write(memory_type_name(entry->type));
        serial_write(" (");
        serial_write_decimal(entry->type);
        serial_write(")\n");
    }
}

static void report_page_allocator(void) {
    struct page_frame frame;

    if (hhdm_request.response == 0) {
        panic("Limine did not return the HHDM offset");
    }
    if (!page_allocator_initialize(memory_map_request.response,
                                   hhdm_request.response->offset)) {
        panic("could not initialize the physical page allocator");
    }
    if (!page_allocator_allocate(&frame)) {
        panic("no usable physical page is available");
    }

    serial_write("HHDM offset: 0x");
    serial_write_hex(hhdm_request.response->offset);
    serial_write("\nFirst allocated page: physical=0x");
    serial_write_hex(frame.physical_address);
    serial_write(" virtual=0x");
    serial_write_hex((uint64_t)(uintptr_t)frame.virtual_address);
    serial_write("\n");
}

static void report_pci_device(const struct pci_device_info *device,
                              void *context) {
    const bool *mmio_available = context;

    serial_write("PCI ");
    serial_write_decimal(device->bus);
    serial_write(":");
    serial_write_decimal(device->device);
    serial_write(".");
    serial_write_decimal(device->function);
    serial_write(" vendor=0x");
    serial_write_hex32(device->vendor_id);
    serial_write(" device=0x");
    serial_write_hex32(device->device_id);
    serial_write(" class=0x");
    serial_write_hex32(((uint32_t)device->class_code << 16)
                       | ((uint32_t)device->subclass << 8)
                       | device->programming_interface);
    serial_write(" header=0x");
    serial_write_hex32(device->header_type);
    serial_write("\n");

    for (uint8_t i = 0; i < device->bar_count; i++) {
        const struct pci_bar_info *bar = &device->bars[i];

        serial_write("  BAR");
        serial_write_decimal(bar->index);
        serial_write(" ");

        if (!bar->valid || bar->kind == PCI_BAR_RESERVED) {
            serial_write("reserved/invalid\n");
            continue;
        }
        if (bar->kind == PCI_BAR_IO) {
            serial_write("io base=0x");
        } else {
            serial_write(bar->is_64_bit ? "memory64 base=0x" : "memory32 base=0x");
        }

        serial_write_hex(bar->base_address);
        if (bar->kind == PCI_BAR_MEMORY && bar->prefetchable) {
            serial_write(" prefetchable");
        }
        serial_write("\n");
    }

    if (device->vendor_id == 0x1234 && device->device_id == 0x11e8) {
        for (uint8_t i = 0; i < device->bar_count; i++) {
            const struct pci_bar_info *bar = &device->bars[i];
            uint64_t size;

            if (bar->index == 0 && bar->kind == PCI_BAR_MEMORY) {
                if (!pci_probe_bar_size(device, bar, &size)) {
                    serial_write("  EDU BAR0 size probe failed\n");
                    continue;
                }

                serial_write("  EDU BAR0 size=0x");
                serial_write_hex(size);
                serial_write("\n");

                if (size < 8 || !*mmio_available) {
                    continue;
                }

                struct x86_mmio_mapping mapping = {0};
                if (!x86_mmio_map_range(bar->base_address, size, &mapping)) {
                    serial_write("  EDU BAR0 MMIO map failed\n");
                    continue;
                }

                volatile uint32_t *registers =
                    (volatile uint32_t *)mapping.address;
                uint32_t device_id = registers[0];
                uint32_t previous = registers[1];
                uint32_t probe_value = 0x5a5aa5a5;
                registers[1] = probe_value;
                uint32_t observed = registers[1];
                registers[1] = ~previous;
                uint32_t restored = registers[1];

                serial_write("  EDU MMIO id=0x");
                serial_write_hex32(device_id);
                serial_write(" liveness=");
                serial_write(observed == ~probe_value && restored == previous
                                 ? "pass\n" : "fail\n");

                if (!x86_mmio_unmap(&mapping)) {
                    serial_write("  EDU BAR0 MMIO unmap failed\n");
                }
            }
        }
    }
}

static void report_pci_devices(void) {
    bool mmio_available = x86_mmio_initialize(memory_map_request.response,
                                               hhdm_request.response->offset);
    if (!mmio_available) {
        serial_write("MMIO mapping setup unavailable; EDU access skipped\n");
    }

    uint64_t found = pci_enumerate(report_pci_device, &mmio_available);

    serial_write("PCI functions discovered: ");
    serial_write_decimal(found);
    serial_write("\n");
}

void kmain(void) {
    serial_initialize();
    serial_write("Eloquence x86-64 kernel\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        panic("unsupported Limine base revision");
    }
    if (paging_mode_request.response == 0
        || paging_mode_request.response->mode != LIMINE_PAGING_MODE_X86_64_4LVL) {
        panic("Limine did not select the requested 4-level paging mode");
    }

    if (bootloader_request.response != 0
        && bootloader_request.response->name != 0
        && bootloader_request.response->version != 0) {
        serial_write("Bootloader: ");
        serial_write(bootloader_request.response->name);
        serial_write(" ");
        serial_write(bootloader_request.response->version);
        serial_write("\n");
    }

    report_memory_map();
    report_page_allocator();
    report_pci_devices();
    serial_write("Boot information report complete; kernel halted.\n");
    halt_cpu();
}
