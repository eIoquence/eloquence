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
#include "drivers/virtio/virtio_blk.h"
#include "memory/mmio.h"
#include "memory/page_allocator.h"
#include "monitor/serial_shell.h"
#include "paging.h"
#include "io.h"
#include "serial.h"
#include "timer.h"
#include "storage/block_device.h"
#include "storage/gpt.h"
#include "storage/fat32.h"
#include "storage/vfs.h"
#include "user/mmio_demo.h"
#include "user/elf_loader.h"
#include "user_mode.h"
#include "system_info.h"

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
static struct fat32_directory_entry fat32_entries[FAT32_MAX_DIRECTORY_ENTRIES];
static uint8_t fat32_file_buffer[16384];
static struct fat32_volume shell_root_volume;
static struct vfs_entry shell_vfs_entries[VFS_MAX_DIRECTORY_ENTRIES];
static uint8_t shell_write_buffer[FAT32_MAX_FILE_WRITE_BYTES];
static char shell_write_name[FAT32_SHORT_NAME_LENGTH];
static uint32_t shell_write_length;
static bool shell_write_active;
static uint64_t pci_functions_found;

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
    if (device->vendor_id == 0x1af4 && device->device_id == 0x1001) {
        serial_write(virtio_blk_initialize(device)
                     ? "  VirtIO legacy block driver initialized\n"
                     : "  VirtIO legacy block driver probe failed\n");
    }
}

static void report_pci_devices(void) {
    bool mmio_available = x86_mmio_initialize(memory_map_request.response,
                                               hhdm_request.response->offset);
    if (!mmio_available) {
        serial_write("MMIO mapping setup unavailable; EDU access skipped\n");
    }

    uint64_t found = pci_enumerate(report_pci_device, &mmio_available);
    pci_functions_found = found;

    serial_write("PCI functions discovered: ");
    serial_write_decimal(found);
    serial_write("\n");
}

static const char *gpt_scan_result_name(enum gpt_scan_result result) {
    switch (result) {
        case GPT_SCAN_NOT_PRESENT: return "not present";
        case GPT_SCAN_IO_ERROR: return "read error";
        case GPT_SCAN_INVALID: return "invalid";
        case GPT_SCAN_LIMIT_EXCEEDED: return "limit exceeded";
        case GPT_SCAN_OK: return "ok";
        default: return "unknown";
    }
}

static void report_gpt_device(uint64_t device_index, const char *indent) {
    uint32_t partition_count = 0;
    enum gpt_scan_result result = gpt_scan_device(
        device_index, discovered_partitions, GPT_MAX_PARTITIONS,
        &partition_count);

    if (result != GPT_SCAN_OK) {
        serial_write(indent);
        serial_write("primary GPT: ");
        serial_write(gpt_scan_result_name(result));
        serial_write("\n");
        return;
    }

    serial_write(indent);
    serial_write("primary GPT partitions: ");
    serial_write_decimal(partition_count);
    serial_write("\n");
    for (uint32_t partition = 0; partition < partition_count; partition++) {
        const struct gpt_partition *entry = &discovered_partitions[partition];
        serial_write(indent);
        serial_write("  entry=");
        serial_write_decimal(entry->entry_number);
        serial_write(" first-lba=");
        serial_write_decimal(entry->first_lba);
        serial_write(" last-lba=");
        serial_write_decimal(entry->last_lba);
        serial_write("\n");
    }
}

static void report_block_devices(void) {
    if (!ata_pio_initialize()) {
        serial_write("Legacy ATA PIO probe unavailable\n");
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

        report_gpt_device(i, "    ");
    }
}

static bool text_equals(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool command_has_prefix(const char *command, const char *prefix) {
    while (*prefix != '\0' && *command == *prefix) {
        command++;
        prefix++;
    }
    return *prefix == '\0' && (*command == '\0' || *command == ' ');
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ') {
        text++;
    }
    return text;
}

static bool parse_device_index(const char *text, uint64_t *device_index) {
    uint64_t value = 0;

    if (*text < '0' || *text > '9') {
        return false;
    }
    while (*text >= '0' && *text <= '9') {
        uint64_t digit = (uint64_t)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        text++;
    }
    if (*skip_spaces(text) != '\0') {
        return false;
    }

    *device_index = value;
    return true;
}

static bool parse_number_argument(const char **text, uint64_t *value) {
    const char *cursor = skip_spaces(*text);
    uint64_t parsed = 0;

    if (*cursor < '0' || *cursor > '9') {
        return false;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        uint64_t digit = (uint64_t)(*cursor - '0');
        if (parsed > (UINT64_MAX - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
        cursor++;
    }
    *text = cursor;
    *value = parsed;
    return true;
}

static const char *fat32_result_name(enum fat32_result result) {
    switch (result) {
        case FAT32_OK: return "ok";
        case FAT32_NOT_FAT32: return "not FAT32";
        case FAT32_IO_ERROR: return "read error";
        case FAT32_INVALID: return "invalid volume or data";
        case FAT32_LIMIT_EXCEEDED: return "size limit exceeded";
        case FAT32_NOT_FOUND: return "file not found";
        default: return "unknown error";
    }
}

static bool find_gpt_partition(uint64_t device_index,
                               uint64_t partition_number,
                               struct gpt_partition *partition) {
    uint32_t count = 0;
    enum gpt_scan_result result = gpt_scan_device(
        device_index, discovered_partitions, GPT_MAX_PARTITIONS, &count);

    if (result != GPT_SCAN_OK) {
        serial_write("GPT scan failed: ");
        serial_write(gpt_scan_result_name(result));
        serial_write("\n");
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (discovered_partitions[i].entry_number == partition_number) {
            *partition = discovered_partitions[i];
            return true;
        }
    }
    serial_write("GPT partition number was not found.\n");
    return false;
}

static bool mount_fat32(uint64_t device_index, uint64_t partition_number,
                        struct fat32_volume *volume) {
    struct gpt_partition partition;

    if (!find_gpt_partition(device_index, partition_number, &partition)) {
        return false;
    }
    enum fat32_result result = fat32_mount(device_index, &partition, volume);
    if (result != FAT32_OK) {
        serial_write("FAT32 mount failed: ");
        serial_write(fat32_result_name(result));
        serial_write("\n");
        return false;
    }
    return true;
}

static bool parse_filesystem_target(const char *arguments,
                                    uint64_t *device_index,
                                    uint64_t *partition_number,
                                    const char **remaining) {
    const char *cursor = arguments;

    if (!parse_number_argument(&cursor, device_index)
        || !parse_number_argument(&cursor, partition_number)) {
        return false;
    }
    *remaining = skip_spaces(cursor);
    return true;
}

static void report_fat32_root(uint64_t device_index,
                              uint64_t partition_number) {
    struct fat32_volume volume;
    uint32_t entry_count = 0;

    if (!mount_fat32(device_index, partition_number, &volume)) {
        return;
    }
    enum fat32_result result = fat32_list_root(
        &volume, fat32_entries, FAT32_MAX_DIRECTORY_ENTRIES, &entry_count);
    if (result != FAT32_OK) {
        serial_write("FAT32 directory read failed: ");
        serial_write(fat32_result_name(result));
        serial_write("\n");
        return;
    }
    serial_write("FAT32 root directory:\n");
    for (uint32_t i = 0; i < entry_count; i++) {
        serial_write("  ");
        serial_write(fat32_entries[i].short_name);
        if ((fat32_entries[i].attributes & 0x10U) != 0) {
            serial_write(" [directory]");
        } else {
            serial_write("  ");
            serial_write_decimal(fat32_entries[i].size);
            serial_write(" bytes");
        }
        serial_write("\n");
    }
}

static bool read_fat32_file(uint64_t device_index,
                            uint64_t partition_number,
                            const char *short_name, uint32_t *bytes_read) {
    struct fat32_volume volume;

    if (bytes_read == 0
        || !mount_fat32(device_index, partition_number, &volume)) {
        return false;
    }
    enum fat32_result result = fat32_read_root_file(
        &volume, short_name, fat32_file_buffer, sizeof(fat32_file_buffer),
        bytes_read);
    if (result != FAT32_OK) {
        serial_write("FAT32 file read failed: ");
        serial_write(fat32_result_name(result));
        serial_write("\n");
        return false;
    }
    return true;
}

static void report_fat32_file(uint64_t device_index,
                              uint64_t partition_number,
                              const char *short_name) {
    uint32_t bytes_read = 0;

    if (!read_fat32_file(device_index, partition_number, short_name,
                         &bytes_read)) {
        return;
    }

    serial_write("Contents of ");
    serial_write(short_name);
    serial_write(":\n");
    for (uint32_t i = 0; i < bytes_read; i++) {
        char character = (char)fat32_file_buffer[i];
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            serial_write("\n");
        } else if (character == '\t') {
            serial_write("\t");
        } else {
            char text[2] = {
                character >= 32 && character <= 126 ? character : '?',
                '\0'
            };
            serial_write(text);
        }
    }
    if (bytes_read == 0 || fat32_file_buffer[bytes_read - 1] != '\n') {
        serial_write("\n");
    }
}

static bool valid_short_name_character(char value) {
    return (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9')
        || value == '_' || value == '-' || value == '$' || value == '~';
}

static bool decode_short_name(uint64_t packed_base, uint64_t packed_extension,
                              char name[FAT32_SHORT_NAME_LENGTH]) {
    size_t length = 0;
    bool ended = false;

    if (packed_base == 0 || (packed_extension >> 24) != 0) {
        return false;
    }
    for (size_t i = 0; i < 8; i++) {
        char value = (char)(packed_base >> (i * 8));
        if (value == '\0') {
            ended = true;
            continue;
        }
        if (ended || !valid_short_name_character(value)) {
            return false;
        }
        name[length++] = value;
    }
    if (length == 0) {
        return false;
    }
    if (packed_extension != 0) {
        name[length++] = '.';
        ended = false;
        for (size_t i = 0; i < 3; i++) {
            char value = (char)(packed_extension >> (i * 8));
            if (value == '\0') {
                ended = true;
                continue;
            }
            if (ended || !valid_short_name_character(value)) {
                return false;
            }
            name[length++] = value;
        }
        if (name[length - 1] == '.') {
            return false;
        }
    }
    name[length] = '\0';
    return true;
}

static bool fat32_vfs_list(void *context, struct vfs_entry *entries,
                           uint32_t capacity, uint32_t *entry_count) {
    struct fat32_volume *volume = context;
    uint32_t count = 0;
    if (volume == 0 || entries == 0 || entry_count == 0
        || capacity > FAT32_MAX_DIRECTORY_ENTRIES) {
        return false;
    }
    enum fat32_result result = fat32_list_root(
        volume, fat32_entries, FAT32_MAX_DIRECTORY_ENTRIES, &count);
    if (result != FAT32_OK || count > capacity) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        struct vfs_entry *entry = &entries[i];
        size_t character = 0;
        while (fat32_entries[i].short_name[character] != '\0'
               && character + 1 < sizeof(entry->name)) {
            entry->name[character] = fat32_entries[i].short_name[character];
            character++;
        }
        entry->name[character] = '\0';
        entry->size = fat32_entries[i].size;
        entry->directory = (fat32_entries[i].attributes & 0x10U) != 0;
    }
    *entry_count = count;
    return true;
}

static bool fat32_vfs_read(void *context, const char *name, void *buffer,
                           uint32_t capacity, uint32_t *bytes_read) {
    struct fat32_volume *volume = context;
    return volume != 0
        && fat32_read_root_file(volume, name, buffer, capacity, bytes_read)
               == FAT32_OK;
}

static bool fat32_vfs_create(void *context, const char *name) {
    struct fat32_volume *volume = context;
    return volume != 0
        && fat32_create_root_file(volume, name) == FAT32_OK;
}

static bool fat32_vfs_delete(void *context, const char *name) {
    struct fat32_volume *volume = context;
    return volume != 0
        && fat32_delete_root_file(volume, name) == FAT32_OK;
}

static bool fat32_vfs_write(void *context, const char *name,
                            const void *buffer, uint32_t bytes) {
    struct fat32_volume *volume = context;
    return volume != 0
        && fat32_write_root_file(volume, name, buffer, bytes) == FAT32_OK;
}

static bool fat32_vfs_mkdir(void *context, const char *name) {
    struct fat32_volume *volume = context;
    return volume != 0
        && fat32_create_root_directory(volume, name) == FAT32_OK;
}

static bool mount_shell_vfs(void) {
    static const struct vfs_operations operations = {
        .list_root = fat32_vfs_list,
        .read_root_file = fat32_vfs_read,
        .create_root_file = fat32_vfs_create,
        .delete_root_file = fat32_vfs_delete,
        .write_root_file = fat32_vfs_write,
        .create_root_directory = fat32_vfs_mkdir
    };
    struct gpt_partition partition;

    if (vfs_root_mounted()) {
        return true;
    }
    if (!find_gpt_partition(0, 1, &partition)
        || fat32_mount(0, &partition, &shell_root_volume) != FAT32_OK) {
        serial_write("Could not mount root filesystem.\n");
        return false;
    }
    return vfs_mount_root(&operations, &shell_root_volume);
}

bool kernel_system_file_list(uint64_t device_index,
                             uint64_t partition_number) {
    uint32_t entry_count = 0;
    if (device_index != 0 || partition_number != 1
        || !mount_shell_vfs()) {
        return false;
    }
    if (!vfs_list_root(shell_vfs_entries, VFS_MAX_DIRECTORY_ENTRIES,
                       &entry_count)) {
        serial_write("VFS root directory read failed.\n");
        return true;
    }
    serial_write("Root directory (/):\n");
    for (uint32_t i = 0; i < entry_count; i++) {
        serial_write("  ");
        serial_write(shell_vfs_entries[i].name);
        if (shell_vfs_entries[i].directory) {
            serial_write(" [directory]\n");
        } else {
            serial_write("  ");
            serial_write_decimal(shell_vfs_entries[i].size);
            serial_write(" bytes\n");
        }
    }
    return true;
}

bool kernel_system_file_cat(uint64_t packed_name,
                            uint64_t packed_extension) {
    char name[FAT32_SHORT_NAME_LENGTH];
    if (!decode_short_name(packed_name, packed_extension, name)) {
        return false;
    }
    char path[VFS_NAME_CAPACITY + 1] = "/";
    size_t length = 1;
    while (name[length - 1] != '\0') {
        path[length] = name[length - 1];
        length++;
    }
    path[length] = '\0';
    uint32_t bytes_read = 0;
    if (!mount_shell_vfs()
        || !vfs_read_file(path, fat32_file_buffer,
                          sizeof(fat32_file_buffer), &bytes_read)) {
        serial_write("VFS file read failed or file not found.\n");
        return true;
    }
    serial_write("Contents of ");
    serial_write(name);
    serial_write(":\n");
    for (uint32_t i = 0; i < bytes_read; i++) {
        char value = (char)fat32_file_buffer[i];
        if (value == '\r') {
            continue;
        }
        if (value == '\n') {
            serial_write("\n");
        } else if (value == '\t') {
            serial_write("\t");
        } else {
            char text[2] = {value >= 32 && value <= 126 ? value : '?', '\0'};
            serial_write(text);
        }
    }
    if (bytes_read == 0 || fat32_file_buffer[bytes_read - 1] != '\n') {
        serial_write("\n");
    }
    return true;
}

bool kernel_system_file_stat(uint64_t packed_name,
                             uint64_t packed_extension) {
    char name[FAT32_SHORT_NAME_LENGTH];
    uint32_t entry_count = 0;

    if (!decode_short_name(packed_name, packed_extension, name)
        || !mount_shell_vfs()) {
        return false;
    }
    if (!vfs_list_root(shell_vfs_entries, VFS_MAX_DIRECTORY_ENTRIES,
                       &entry_count)) {
        serial_write("VFS root directory read failed.\n");
        return true;
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        if (text_equals(shell_vfs_entries[i].name, name)) {
            serial_write("Name: ");
            serial_write(name);
            serial_write("\nType: ");
            serial_write(shell_vfs_entries[i].directory
                             ? "directory\n" : "file\n");
            serial_write("Size: ");
            serial_write_decimal(shell_vfs_entries[i].size);
            serial_write(" bytes\n");
            return true;
        }
    }
    serial_write("File not found\n");
    return true;
}

static bool make_vfs_path(const char *name, char path[VFS_NAME_CAPACITY + 1]) {
    size_t length = 0;
    path[length++] = '/';
    while (name[length - 1] != '\0' && length < VFS_NAME_CAPACITY) {
        path[length] = name[length - 1];
        length++;
    }
    if (name[length - 1] != '\0') {
        return false;
    }
    path[length] = '\0';
    return true;
}

bool kernel_system_file_create(uint64_t packed_name,
                               uint64_t packed_extension) {
    char name[FAT32_SHORT_NAME_LENGTH];
    char path[VFS_NAME_CAPACITY + 1];
    if (!decode_short_name(packed_name, packed_extension, name)
        || !make_vfs_path(name, path) || !mount_shell_vfs()) {
        return false;
    }
    if (!vfs_create_file(path)) {
        serial_write("Could not create file (read-only media or directory full).\n");
        return false;
    }
    return true;
}

bool kernel_system_file_delete(uint64_t packed_name,
                               uint64_t packed_extension) {
    char name[FAT32_SHORT_NAME_LENGTH];
    char path[VFS_NAME_CAPACITY + 1];
    if (!decode_short_name(packed_name, packed_extension, name)
        || !make_vfs_path(name, path) || !mount_shell_vfs()) {
        return false;
    }
    if (!vfs_delete_file(path)) {
        serial_write("Could not delete file (not found, read-only media, or I/O error).\n");
        return false;
    }
    return true;
}

bool kernel_system_file_write_begin(uint64_t packed_name,
                                    uint64_t packed_extension) {
    if (shell_write_active || !decode_short_name(packed_name,
                                                 packed_extension,
                                                 shell_write_name)
        || !mount_shell_vfs()) {
        return false;
    }
    shell_write_length = 0;
    shell_write_active = true;
    return true;
}

bool kernel_system_file_write_data(uint64_t packed_data, uint64_t bytes) {
    if (!shell_write_active || bytes == 0 || bytes > sizeof(packed_data)
        || bytes > sizeof(shell_write_buffer) - shell_write_length) {
        return false;
    }
    for (uint64_t i = 0; i < bytes; i++) {
        uint8_t value = (uint8_t)(packed_data >> (i * 8));
        if (value != '\n' && value != '\r' && value != '\t'
            && (value < 0x20 || value > 0x7e)) {
            return false;
        }
    }
    for (uint64_t i = 0; i < bytes; i++) {
        shell_write_buffer[shell_write_length++] =
            (uint8_t)(packed_data >> (i * 8));
    }
    return true;
}

bool kernel_system_file_write_commit(void) {
    char path[VFS_NAME_CAPACITY + 1];
    if (!shell_write_active) {
        return false;
    }
    shell_write_active = false;
    if (!make_vfs_path(shell_write_name, path)
        || !vfs_write_file(path, shell_write_buffer, shell_write_length)) {
        serial_write("File write failed (read-only media, size limit, or I/O error).\n");
        return false;
    }
    return true;
}

void kernel_system_file_write_abort(void) {
    shell_write_active = false;
    shell_write_length = 0;
}

bool kernel_system_directory_create(uint64_t packed_name,
                                    uint64_t packed_extension) {
    char name[FAT32_SHORT_NAME_LENGTH];
    char path[VFS_NAME_CAPACITY + 1];
    if (!decode_short_name(packed_name, packed_extension, name)
        || !make_vfs_path(name, path) || !mount_shell_vfs()) {
        return false;
    }
    if (!vfs_create_directory(path)) {
        serial_write("Could not create directory (read-only media, name exists, or I/O error).\n");
        return false;
    }
    return true;
}

static void run_fat32_elf(uint64_t device_index,
                          uint64_t partition_number,
                          const char *short_name) {
    uint32_t image_size = 0;
    uint64_t exit_status = 0;
    bool faulted = false;

    if (!read_fat32_file(device_index, partition_number, short_name,
                         &image_size)) {
        return;
    }
    if (!x86_user_elf_run(fat32_file_buffer, image_size,
                          &exit_status, &faulted)) {
        serial_write("ELF image rejected or process setup failed.\n");
        return;
    }
    if (faulted) {
        serial_write("Process terminated after CPU exception vector ");
        serial_write_decimal(exit_status);
        serial_write(".\n");
        return;
    }
    serial_write("Process exited with status ");
    serial_write_decimal(exit_status);
    serial_write(".\n");
}

static bool parse_short_name(const char *text, char *name, size_t capacity) {
    size_t length = 0;

    if (text == 0 || name == 0 || capacity < 2) {
        return false;
    }
    while (text[length] != '\0' && text[length] != ' '
           && length + 1 < capacity) {
        name[length] = text[length];
        length++;
    }
    name[length] = '\0';
    return length != 0 && text[length] == '\0';
}

static void report_system_info(void) {
    serial_write("Architecture: x86_64\n");
    serial_write("Bootloader: ");
    if (bootloader_request.response == 0
        || bootloader_request.response->name == 0
        || bootloader_request.response->version == 0) {
        serial_write("unavailable\n");
    } else {
        serial_write(bootloader_request.response->name);
        serial_write(" ");
        serial_write(bootloader_request.response->version);
        serial_write("\n");
    }
    serial_write("PCI functions: ");
    serial_write_decimal(pci_functions_found);
    serial_write("\nBlock devices: ");
    serial_write_decimal(block_device_count());
    serial_write("\nInteractive monitor: polling COM1\n");
}

struct x86_cpuid_result {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

static struct x86_cpuid_result x86_cpuid(uint32_t leaf, uint32_t subleaf) {
    struct x86_cpuid_result result;
    __asm__ volatile ("cpuid"
                      : "=a"(result.eax), "=b"(result.ebx),
                        "=c"(result.ecx), "=d"(result.edx)
                      : "a"(leaf), "c"(subleaf));
    return result;
}

static void report_cpu_info(void) {
    struct x86_cpuid_result basic = x86_cpuid(0, 0);
    struct x86_cpuid_result version = x86_cpuid(1, 0);
    char vendor[13];
    const uint32_t vendor_words[3] = {basic.ebx, basic.edx, basic.ecx};

    for (uint32_t word = 0; word < 3; word++) {
        for (uint32_t byte = 0; byte < 4; byte++) {
            vendor[word * 4 + byte] =
                (char)(vendor_words[word] >> (byte * 8));
        }
    }
    vendor[12] = '\0';
    uint32_t base_family = (version.eax >> 8) & 0xf;
    uint32_t family = base_family;
    uint32_t model = (version.eax >> 4) & 0xf;
    uint32_t extended_family = (version.eax >> 20) & 0xff;
    uint32_t extended_model = (version.eax >> 16) & 0xf;
    if (family == 0xf) {
        family += extended_family;
    }
    if (base_family == 6 || base_family == 0xf) {
        model |= extended_model << 4;
    }

    serial_write("Architecture: x86_64\nVendor: ");
    serial_write(vendor);
    serial_write("\nFamily: ");
    serial_write_decimal(family);
    serial_write(" Model: ");
    serial_write_decimal(model);
    serial_write("\nFeatures:");
    if ((version.edx & (1U << 25)) != 0) serial_write(" SSE");
    if ((version.edx & (1U << 26)) != 0) serial_write(" SSE2");
    if ((version.ecx & (1U << 25)) != 0) serial_write(" AES");
    if ((version.ecx & (1U << 28)) != 0) serial_write(" AVX");
    if ((version.ecx & (1U << 31)) != 0) serial_write(" hypervisor");
    if (basic.eax >= 7) {
        struct x86_cpuid_result structured = x86_cpuid(7, 0);
        if ((structured.ebx & (1U << 5)) != 0) serial_write(" AVX2");
    }
    serial_write("\n");
}

bool kernel_system_report(uint64_t report) {
    const struct limine_memmap_response *memory_map =
        memory_map_request.response;

    switch (report) {
        case ELOQUENCE_SYSTEM_REPORT_SYSINFO:
            report_system_info();
            return true;
        case ELOQUENCE_SYSTEM_REPORT_MEMINFO: {
            uint64_t usable_bytes = 0;
            if (memory_map == 0 || memory_map->entries == 0) {
                serial_write("Memory map unavailable\n");
                return true;
            }
            for (uint64_t i = 0; i < memory_map->entry_count; i++) {
                const struct limine_memmap_entry *entry = memory_map->entries[i];
                if (entry != 0 && entry->type == LIMINE_MEMMAP_USABLE
                    && usable_bytes <= UINT64_MAX - entry->length) {
                    usable_bytes += entry->length;
                }
            }
            serial_write("Usable physical memory: ");
            serial_write_decimal(usable_bytes / (1024 * 1024));
            serial_write(" MiB (boot memory map; not free-page count)\n");
            return true;
        }
        case ELOQUENCE_SYSTEM_REPORT_CPUINFO:
            report_cpu_info();
            return true;
        case ELOQUENCE_SYSTEM_REPORT_DEVINFO:
            serial_write("PCI functions: ");
            serial_write_decimal(pci_functions_found);
            serial_write("\nBlock devices: ");
            serial_write_decimal(block_device_count());
            serial_write("\n");
            for (uint64_t i = 0; i < block_device_count(); i++) {
                const struct block_device *device = block_device_get(i);
                if (device != 0) {
                    serial_write(device->name);
                    serial_write(" model=");
                    serial_write(device->model);
                    serial_write(device->read_only ? " read-only\n" : " writable\n");
                }
            }
            return true;
        case ELOQUENCE_SYSTEM_REPORT_IRQINFO:
            serial_write("Timer ticks: ");
            serial_write_decimal(x86_timer_ticks());
            serial_write(" (100 Hz)\nLocal APIC timer routing: ");
            serial_write(x86_timer_uses_local_apic() ? "active\n" : "inactive\n");
            return true;
        case ELOQUENCE_SYSTEM_REPORT_BOOTINFO:
            serial_write("Bootloader: ");
            if (bootloader_request.response != 0
                && bootloader_request.response->name != 0
                && bootloader_request.response->version != 0) {
                serial_write(bootloader_request.response->name);
                serial_write(" ");
                serial_write(bootloader_request.response->version);
                serial_write("\n");
            } else {
                serial_write("unavailable\n");
            }
            serial_write("Physical memory map entries: ");
            serial_write_decimal(memory_map != 0 ? memory_map->entry_count : 0);
            serial_write("\n");
            return true;
        default:
            return false;
    }
}

static void report_disk_list(void) {
    uint64_t device_count = block_device_count();

    serial_write("Block devices: ");
    serial_write_decimal(device_count);
    serial_write("\n");
    for (uint64_t i = 0; i < device_count; i++) {
        const struct block_device *device = block_device_get(i);
        if (device == 0) {
            continue;
        }
        serial_write("  [");
        serial_write_decimal(i);
        serial_write("] ");
        serial_write(device->name);
        serial_write(" model=");
        serial_write(device->model);
        serial_write(" sectors=");
        serial_write_decimal(device->sector_count);
        serial_write(" sector-size=");
        serial_write_decimal(device->sector_size);
        serial_write(device->read_only ? " read-only\n" : " read-write\n");
    }
}

static void report_gpt_list(bool all_devices, uint64_t device_index) {
    uint64_t device_count = block_device_count();

    if (all_devices) {
        if (device_count == 0) {
            serial_write("No block devices are registered.\n");
            return;
        }
        for (uint64_t i = 0; i < device_count; i++) {
            const struct block_device *device = block_device_get(i);
            if (device == 0) {
                continue;
            }
            serial_write("Device [");
            serial_write_decimal(i);
            serial_write("] ");
            serial_write(device->name);
            serial_write(":\n");
            report_gpt_device(i, "  ");
        }
        return;
    }

    if (device_index >= device_count) {
        serial_write("Block device index is out of range.\n");
        return;
    }
    report_gpt_device(device_index, "");
}

static void handle_monitor_command(const char *line, void *context) {
    (void)context;
    if (*line == '\0') {
        return;
    }
    if (text_equals(line, "help")) {
        serial_write("Commands:\n");
        serial_write("  help              show this list\n");
        serial_write("  hwinfo            show boot and device summary\n");
        serial_write("  mem               show the boot memory map\n");
        serial_write("  uptime            show time since timer start\n");
        serial_write("  irq status        show interrupt-controller state\n");
        serial_write("  disk list         list registered block devices\n");
        serial_write("  gpt list [index]  list primary GPT partitions\n");
        serial_write("  fs list DEV PART  list a FAT32 root directory\n");
        serial_write("  fs cat DEV PART NAME  print a short-name text file\n");
        serial_write("  run DEV PART NAME  launch a static x86-64 ELF in ring 3\n");
        serial_write("  halt              halt the CPU\n");
        return;
    }
    if (text_equals(line, "hwinfo")) {
        report_system_info();
        return;
    }
    if (text_equals(line, "mem")) {
        report_memory_map();
        return;
    }
    if (text_equals(line, "uptime")) {
        uint64_t ticks = x86_timer_ticks();
        serial_write("Uptime: ");
        serial_write_decimal(ticks / X86_TIMER_FREQUENCY_HZ);
        serial_write(" seconds (");
        serial_write_decimal(ticks);
        serial_write(" timer ticks)\n");
        return;
    }
    if (text_equals(line, "irq status")) {
        arch_irq_state_t irq_state = arch_irq_save();
        x86_outb(0x20, 0x0a);
        uint8_t pending = x86_inb(0x20);
        x86_outb(0x20, 0x0b);
        uint8_t in_service = x86_inb(0x20);
        uint8_t mask = x86_inb(0x21);
        arch_irq_restore(irq_state);
        serial_write("CPU IF: ");
        serial_write((irq_state & (1ULL << 9)) != 0
                     ? "enabled\n" : "disabled\n");
        serial_write("PIC master mask: 0x");
        serial_write_hex(mask);
        serial_write(" IRR: 0x");
        serial_write_hex(pending);
        serial_write(" ISR: 0x");
        serial_write_hex(in_service);
        serial_write("\nTimer ticks: ");
        serial_write_decimal(x86_timer_ticks());
        serial_write("\nLocal APIC: ");
        serial_write(x86_timer_uses_local_apic() ? "enabled\n" : "unavailable\n");
        serial_write("\n");
        return;
    }
    if (text_equals(line, "disk list")) {
        report_disk_list();
        return;
    }
    if (command_has_prefix(line, "gpt list")) {
        const char *argument = skip_spaces(line + 8);
        if (*argument == '\0') {
            report_gpt_list(true, 0);
            return;
        }
        uint64_t device_index;
        if (!parse_device_index(argument, &device_index)) {
            serial_write("Usage: gpt list [device-index]\n");
            return;
        }
        report_gpt_list(false, device_index);
        return;
    }
    if (command_has_prefix(line, "fs list")) {
        uint64_t device_index;
        uint64_t partition_number;
        const char *remaining;
        if (!parse_filesystem_target(skip_spaces(line + 7), &device_index,
                                     &partition_number, &remaining)
            || *remaining != '\0') {
            serial_write("Usage: fs list device-index gpt-partition-number\n");
            return;
        }
        report_fat32_root(device_index, partition_number);
        return;
    }
    if (command_has_prefix(line, "fs cat")) {
        uint64_t device_index;
        uint64_t partition_number;
        const char *remaining;
        char short_name[FAT32_SHORT_NAME_LENGTH];
        if (!parse_filesystem_target(skip_spaces(line + 6), &device_index,
                                     &partition_number, &remaining)) {
            serial_write("Usage: fs cat device-index gpt-partition-number 8.3-name\n");
            return;
        }
        if (!parse_short_name(remaining, short_name, sizeof(short_name))) {
            serial_write("Usage: fs cat device-index gpt-partition-number 8.3-name\n");
            return;
        }
        report_fat32_file(device_index, partition_number, short_name);
        return;
    }
    if (command_has_prefix(line, "run")) {
        uint64_t device_index;
        uint64_t partition_number;
        const char *remaining;
        char short_name[FAT32_SHORT_NAME_LENGTH];
        if (!parse_filesystem_target(skip_spaces(line + 3), &device_index,
                                     &partition_number, &remaining)
            || !parse_short_name(remaining, short_name,
                                 sizeof(short_name))) {
            serial_write("Usage: run device-index gpt-partition-number 8.3-elf-name\n");
            return;
        }
        run_fat32_elf(device_index, partition_number, short_name);
        return;
    }
    if (text_equals(line, "halt")) {
        arch_cpu_halt();
    }

    serial_write("Unknown command. Type 'help'.\n");
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
    if (!x86_timer_initialize()) {
        serial_write("Warning: local APIC setup failed; timer IRQ delivery may be unavailable\n");
    }
    serial_write("Boot information report complete.\n");
    __asm__ volatile ("sti" : : : "memory");
    if (block_device_count() != 0) {
        serial_write("Starting user shell from disk 0, GPT partition 1.\n");
        run_fat32_elf(0, 1, "SHELL.ELF");
    }
    serial_shell_run(handle_monitor_command, 0);
}
