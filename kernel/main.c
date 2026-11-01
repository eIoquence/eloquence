#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "pci.h"
#include "arch/irq.h"
#include "mmio.h"
#include "exceptions.h"
#include "capability/mmio_capability.h"
#include "memory/mmio.h"
#include "memory/page_allocator.h"
#include "paging.h"
#include "serial.h"
#include "user/mmio_demo.h"
#include "user_mode.h"

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

struct limine_page_allocator_context {
    const struct limine_memmap_response *memory_map;
    uint64_t hhdm_offset;
};

static struct limine_page_allocator_context page_allocator_context;

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

static __attribute__((noreturn)) void panic(const char *message) {
    serial_write("PANIC: ");
    serial_write(message);
    serial_write("\n");
    arch_cpu_halt();
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

static bool read_limine_memory_region(const void *context, uint64_t index,
                                     struct page_allocator_region *region) {
    const struct limine_page_allocator_context *limine_context = context;
    const struct limine_memmap_response *memory_map;
    const struct limine_memmap_entry *entry;

    if (limine_context == 0 || region == 0) {
        return false;
    }
    memory_map = limine_context->memory_map;
    if (memory_map == 0 || index >= memory_map->entry_count
        || memory_map->entries == 0) {
        return false;
    }

    entry = memory_map->entries[index];
    if (entry == 0) {
        return false;
    }

    region->base = entry->base;
    region->length = entry->length;
    region->usable = entry->type == LIMINE_MEMMAP_USABLE;
    return true;
}

static void *limine_physical_to_virtual(const void *context,
                                        uint64_t physical_address) {
    const struct limine_page_allocator_context *limine_context = context;

    if (limine_context == 0
        || limine_context->hhdm_offset > UINT64_MAX - physical_address) {
        return 0;
    }

    return (void *)(uintptr_t)(limine_context->hhdm_offset + physical_address);
}

static void report_page_allocator(void) {
    struct page_frame frame;
    struct page_allocator_source source;

    if (hhdm_request.response == 0 || memory_map_request.response == 0) {
        panic("Limine did not return the memory map and HHDM offset");
    }
    page_allocator_context.memory_map = memory_map_request.response;
    page_allocator_context.hhdm_offset = hhdm_request.response->offset;
    source.context = &page_allocator_context;
    source.region_count = memory_map_request.response->entry_count;
    source.read_region = read_limine_memory_region;
    source.physical_to_virtual = limine_physical_to_virtual;
    if (!page_allocator_initialize(&source)) {
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

static const char *capability_operation_name(
    enum mmio_capability_operation operation) {
    switch (operation) {
        case MMIO_CAPABILITY_OP_GRANT: return "grant";
        case MMIO_CAPABILITY_OP_READ32: return "read32";
        case MMIO_CAPABILITY_OP_WRITE32: return "write32";
        case MMIO_CAPABILITY_OP_REVOKE: return "revoke";
        default: return "unknown";
    }
}

static const char *capability_result_name(enum mmio_capability_result result) {
    switch (result) {
        case MMIO_CAPABILITY_OK: return "ok";
        case MMIO_CAPABILITY_INVALID_ARGUMENT: return "invalid-argument";
        case MMIO_CAPABILITY_TABLE_FULL: return "table-full";
        case MMIO_CAPABILITY_INVALID_HANDLE: return "invalid-handle";
        case MMIO_CAPABILITY_STALE_HANDLE: return "stale-handle";
        case MMIO_CAPABILITY_ACCESS_DENIED: return "denied";
        case MMIO_CAPABILITY_OUT_OF_RANGE: return "out-of-range";
        case MMIO_CAPABILITY_MISALIGNED: return "misaligned";
        case MMIO_CAPABILITY_MAPPING_ERROR: return "mapping-error";
        default: return "unknown";
    }
}

static void report_capability_audit(void) {
    struct mmio_capability_audit_event events[16];
    size_t count = mmio_capability_audit_read(events,
                                              sizeof(events) / sizeof(events[0]));

    serial_write("  MMIO audit events: ");
    serial_write_decimal(count);
    serial_write(" overwritten=");
    serial_write_decimal(mmio_capability_audit_overflow_count());
    serial_write("\n");
    for (size_t i = 0; i < count; i++) {
        serial_write("    #");
        serial_write_decimal(events[i].sequence);
        serial_write(" domain=");
        serial_write_decimal(events[i].actor_domain);
        serial_write(" owner=");
        serial_write_decimal(events[i].owner_domain);
        serial_write(" op=");
        serial_write(capability_operation_name(events[i].operation));
        serial_write(" result=");
        serial_write(capability_result_name(events[i].result));
        serial_write(" offset=0x");
        serial_write_hex(events[i].offset);
        serial_write("\n");
    }
}

static void exercise_edu_user_domain(const struct pci_bar_info *bar,
                                     uint64_t size) {
    uint64_t failures = UINT64_MAX;
    bool ran = user_mmio_demo_execute(bar, size, &failures);

    serial_write("  EDU user-domain capability checks: ");
    serial_write(ran && failures == 0 ? "pass" : "fail");
    if (ran) {
        serial_write(" failures=0x");
        serial_write_hex(failures);
    }
    serial_write("\n");
    report_capability_audit();
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
                exercise_edu_user_domain(bar, size);
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
    if (!x86_user_mode_initialize()) {
        panic("could not initialize x86-64 user mode and syscall support");
    }
    x86_exceptions_initialize();
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
    if (!x86_paging_initialize(hhdm_request.response->offset)) {
        panic("could not initialize x86-64 paging support");
    }
    report_pci_devices();
    serial_write("Boot information report complete; kernel halted.\n");
    arch_cpu_halt();
}
