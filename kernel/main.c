#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "pci.h"
#include "arch/irq.h"
#include "mmio.h"
#include "exceptions.h"
#include "capability/mmio_capability.h"
#include "drivers/ata/ata_pio.h"
#include "drivers/pci/edu.h"
#include "memory/mmio.h"
#include "memory/page_allocator.h"
#include "paging.h"
#include "serial.h"
#include "storage/block_device.h"
#include "storage/gpt.h"
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
static struct gpt_partition discovered_partitions[GPT_MAX_PARTITIONS];

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
    if (!page_allocator_free(&frame)) {
        panic("could not release the page allocator probe frame");
    }
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

static void initialize_edu_driver(const struct pci_device_info *pci,
                                  bool mmio_available) {
    struct edu_device edu = {0};
    uint32_t before = 0;
    uint32_t after = 0;
    bool liveness_ok;

    if (!mmio_available) {
        serial_write("  EDU driver skipped: MMIO mapper unavailable\n");
        return;
    }
    if (!edu_device_initialize(pci, &edu)) {
        serial_write("  EDU driver probe failed\n");
        if (edu.initialized && !edu_device_shutdown(&edu)) {
            serial_write("  EDU BAR mapping remains active after cleanup failure\n");
        }
        return;
    }

    serial_write("  EDU driver BAR0 size=0x");
    serial_write_hex(edu.bar0_size);
    serial_write(" identity validated\n");
    liveness_ok = edu_device_liveness_roundtrip(
        &edu, 0x5a5aa5a5U, &before, &after);
    serial_write("  EDU liveness inversion and restore: ");
    serial_write(liveness_ok ? "pass" : "fail");
    serial_write(" before=0x");
    serial_write_hex32(before);
    serial_write(" observed=0x");
    serial_write_hex32(after);
    serial_write("\n");

    struct pci_bar_info bar = edu.bar0;
    uint64_t bar_size = edu.bar0_size;
    bool shutdown_ok = edu_device_shutdown(&edu);
    if (!shutdown_ok) {
        serial_write("  EDU driver could not release BAR mapping\n");
        return;
    }
    if (liveness_ok) {
        exercise_edu_user_domain(&bar, bar_size);
    }
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
        initialize_edu_driver(device, *mmio_available);
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

static void report_block_devices(void) {
    if (!ata_pio_initialize()) {
        serial_write("Legacy ATA PIO probe unavailable\n");
        return;
    }

    serial_write("Block devices discovered: ");
    serial_write_decimal(block_device_count());
    serial_write("\n");
    for (uint64_t i = 0; i < block_device_count(); i++) {
        const struct block_device *device = block_device_get(i);
        if (device == 0) {
            continue;
        }
        serial_write("  ");
        serial_write(device->name);
        serial_write(" model=");
        serial_write(device->model);
        serial_write(" sectors=");
        serial_write_decimal(device->sector_count);
        serial_write(" sector-size=");
        serial_write_decimal(device->sector_size);
        serial_write("\n");

        uint32_t partition_count = 0;
        enum gpt_scan_result result = gpt_scan_device(
            i, discovered_partitions, GPT_MAX_PARTITIONS, &partition_count);
        if (result != GPT_SCAN_OK) {
            serial_write("    primary GPT: ");
            switch (result) {
                case GPT_SCAN_NOT_PRESENT: serial_write("not present"); break;
                case GPT_SCAN_IO_ERROR: serial_write("read error"); break;
                case GPT_SCAN_LIMIT_EXCEEDED: serial_write("limit exceeded"); break;
                case GPT_SCAN_INVALID: serial_write("invalid"); break;
                case GPT_SCAN_OK: break;
            }
            serial_write("\n");
            continue;
        }

        serial_write("    primary GPT partitions: ");
        serial_write_decimal(partition_count);
        serial_write("\n");
        for (uint32_t partition = 0; partition < partition_count; partition++) {
            const struct gpt_partition *entry =
                &discovered_partitions[partition];
            serial_write("      entry=");
            serial_write_decimal(entry->entry_number);
            serial_write(" first-lba=");
            serial_write_decimal(entry->first_lba);
            serial_write(" last-lba=");
            serial_write_decimal(entry->last_lba);
            serial_write("\n");
        }
    }
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
    report_block_devices();
    serial_write("Boot information report complete; kernel halted.\n");
    arch_cpu_halt();
}
