#include <stdint.h>

#include <limine.h>

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

void kmain(void) {
    serial_initialize();
    serial_write("Eloquence x86-64 kernel\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        panic("unsupported Limine base revision");
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
    serial_write("Boot information report complete; kernel halted.\n");
    halt_cpu();
}
