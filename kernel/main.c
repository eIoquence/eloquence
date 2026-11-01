#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "pci.h"
#include "mmio.h"
#include "exceptions.h"
#include "capability/mmio_capability.h"
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

static void exercise_edu_capability(const struct pci_bar_info *bar,
                                    uint64_t size) {
    const uint64_t owner_domain = 1;
    const uint64_t foreign_domain = 2;
    const uint32_t probe_value = 0x5a5aa5a5;
    struct x86_mmio_mapping mapping = {0};
    struct mmio_capability_handle capability = {0};
    enum mmio_capability_result result;

    if (!x86_mmio_map_range(bar->base_address, size, &mapping)) {
        serial_write("  EDU BAR0 MMIO map failed\n");
        return;
    }

    result = mmio_capability_grant(owner_domain, &mapping,
                                   MMIO_CAPABILITY_READ32
                                   | MMIO_CAPABILITY_WRITE32,
                                   &capability);
    if (result != MMIO_CAPABILITY_OK) {
        serial_write("  EDU capability grant failed: ");
        serial_write(capability_result_name(result));
        serial_write("\n");
        x86_mmio_unmap(&mapping);
        report_capability_audit();
        return;
    }

    uint32_t device_id = 0;
    uint32_t previous = 0;
    uint32_t observed = 0;
    uint32_t restored = 0;
    bool id_read = mmio_capability_read32(owner_domain, capability, 0,
                                          &device_id) == MMIO_CAPABILITY_OK;
    bool previous_read = mmio_capability_read32(owner_domain, capability, 4,
                                                &previous)
        == MMIO_CAPABILITY_OK;
    bool write_ok = false;
    bool observed_read = false;
    bool restore_ok = false;
    bool restored_read = false;

    if (previous_read) {
        write_ok = mmio_capability_write32(owner_domain, capability, 4,
                                           probe_value) == MMIO_CAPABILITY_OK;
        observed_read = mmio_capability_read32(owner_domain, capability, 4,
                                               &observed)
            == MMIO_CAPABILITY_OK;
        restore_ok = mmio_capability_write32(owner_domain, capability, 4,
                                             ~previous)
            == MMIO_CAPABILITY_OK;
        restored_read = mmio_capability_read32(owner_domain, capability, 4,
                                               &restored)
            == MMIO_CAPABILITY_OK;
    }

    uint32_t foreign_value;
    enum mmio_capability_result foreign_result =
        mmio_capability_read32(foreign_domain, capability, 0, &foreign_value);
    enum mmio_capability_result revoke_result =
        mmio_capability_revoke(owner_domain, capability);
    uint32_t stale_value;
    enum mmio_capability_result stale_result =
        mmio_capability_read32(owner_domain, capability, 0, &stale_value);

    enum mmio_capability_result read_only_write_result =
        MMIO_CAPABILITY_MAPPING_ERROR;
    enum mmio_capability_result read_only_revoke_result =
        MMIO_CAPABILITY_MAPPING_ERROR;
    if (revoke_result == MMIO_CAPABILITY_OK) {
        struct x86_mmio_mapping read_only_mapping = {0};
        struct mmio_capability_handle read_only_capability = {0};

        if (x86_mmio_map_range(bar->base_address, size, &read_only_mapping)) {
            result = mmio_capability_grant(owner_domain, &read_only_mapping,
                                           MMIO_CAPABILITY_READ32,
                                           &read_only_capability);
            if (result == MMIO_CAPABILITY_OK) {
                read_only_write_result = mmio_capability_write32(
                    owner_domain, read_only_capability, 4, probe_value);
                read_only_revoke_result = mmio_capability_revoke(
                    owner_domain, read_only_capability);
            } else {
                x86_mmio_unmap(&read_only_mapping);
                read_only_write_result = result;
            }
        }
    }

    serial_write("  EDU MMIO id=0x");
    serial_write_hex32(device_id);
    serial_write(" authorized-read=");
    serial_write(id_read ? "pass" : "fail");
    serial_write(" identity=");
    serial_write(id_read && device_id == 0x010000edu ? "pass" : "fail");
    serial_write(" liveness=");
    serial_write(previous_read && write_ok && observed_read && restore_ok
                     && restored_read && observed == ~probe_value
                     && restored == previous ? "pass" : "fail");
    serial_write(" foreign-access=");
    serial_write(capability_result_name(foreign_result));
    serial_write(" revoke=");
    serial_write(capability_result_name(revoke_result));
    serial_write(" stale-access=");
    serial_write(capability_result_name(stale_result));
    serial_write(" read-only-write=");
    serial_write(capability_result_name(read_only_write_result));
    serial_write(" read-only-revoke=");
    serial_write(capability_result_name(read_only_revoke_result));
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
                exercise_edu_capability(bar, size);
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
    report_pci_devices();
    serial_write("Boot information report complete; kernel halted.\n");
    halt_cpu();
}
