#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "block_device.h"
#include "gpt.h"

#define GPT_MIN_SECTOR_SIZE 512U
#define GPT_MAX_SECTOR_SIZE 4096U
#define GPT_HEADER_MIN_SIZE 92U
#define GPT_ENTRY_PREFIX_SIZE 56U
#define GPT_MIN_ENTRY_ARRAY_BYTES (16U * 1024U)
#define GPT_MAX_ENTRY_ARRAY_BYTES (256U * 1024U)
#define GPT_MAX_ENTRY_COUNT 4096U
#define GPT_MAX_ENTRY_SIZE 4096U
#define GPT_IO_BUFFER_SIZE (16U * 1024U)
#define GPT_HEADER_REVISION_1_0 0x00010000U

static uint8_t gpt_io_buffer[GPT_IO_BUFFER_SIZE];

static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *bytes) {
    return (uint64_t)read_u32_le(bytes)
        | ((uint64_t)read_u32_le(bytes + 4) << 32);
}

static bool bytes_are_zero(const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t count) {
    for (size_t i = 0; i < count; i++) {
        destination[i] = source[i];
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes,
                             size_t count) {
    for (size_t i = 0; i < count; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return crc;
}

static uint32_t header_crc32(const uint8_t *header, uint32_t header_size) {
    uint32_t crc = 0xffffffffU;

    for (uint32_t i = 0; i < header_size; i++) {
        uint8_t byte = i >= 16 && i < 20 ? 0 : header[i];
        crc = crc32_update(crc, &byte, 1);
    }
    return ~crc;
}

static enum gpt_scan_result append_partition(
    const uint8_t *entry, uint32_t entry_number,
    uint64_t first_usable_lba, uint64_t last_usable_lba,
    struct gpt_partition *partitions, uint32_t capacity, uint32_t *count) {
    uint64_t first_lba = read_u64_le(entry + 32);
    uint64_t last_lba = read_u64_le(entry + 40);

    if (bytes_are_zero(entry, 16)) {
        return GPT_SCAN_OK;
    }
    if (*count == capacity) {
        return GPT_SCAN_LIMIT_EXCEEDED;
    }
    if (first_lba < first_usable_lba || last_lba > last_usable_lba
        || first_lba > last_lba
        || bytes_are_zero(entry + 16, 16)) {
        return GPT_SCAN_INVALID;
    }

    for (uint32_t i = 0; i < *count; i++) {
        const struct gpt_partition *existing = &partitions[i];
        if (first_lba <= existing->last_lba
            && existing->first_lba <= last_lba) {
            return GPT_SCAN_INVALID;
        }
        bool duplicate_guid = true;
        for (uint8_t byte = 0; byte < 16; byte++) {
            if (entry[16 + byte] != existing->unique_guid[byte]) {
                duplicate_guid = false;
                break;
            }
        }
        if (duplicate_guid) {
            return GPT_SCAN_INVALID;
        }
    }

    struct gpt_partition *partition = &partitions[*count];
    partition->entry_number = entry_number + 1;
    copy_bytes(partition->type_guid, entry, 16);
    copy_bytes(partition->unique_guid, entry + 16, 16);
    partition->first_lba = first_lba;
    partition->last_lba = last_lba;
    (*count)++;
    return GPT_SCAN_OK;
}

static bool valid_signature(const uint8_t *header) {
    static const uint8_t signature[8] = {
        'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'
    };
    for (uint8_t i = 0; i < sizeof(signature); i++) {
        if (header[i] != signature[i]) {
            return false;
        }
    }
    return true;
}

static bool header_is_valid(const uint8_t *sector, uint32_t sector_size,
                            uint64_t sector_count,
                            uint64_t *first_usable_lba,
                            uint64_t *last_usable_lba,
                            uint64_t *entry_array_lba,
                            uint32_t *entry_count, uint32_t *entry_size,
                            uint32_t *entry_array_crc) {
    uint32_t header_size = read_u32_le(sector + 12);
    uint64_t alternate_lba = read_u64_le(sector + 32);
    uint64_t last_lba = sector_count - 1;
    uint64_t array_bytes;
    uint64_t array_sectors;
    uint64_t array_end_lba;

    if (!valid_signature(sector)
        || read_u32_le(sector + 8) != GPT_HEADER_REVISION_1_0
        || header_size < GPT_HEADER_MIN_SIZE || header_size > sector_size
        || read_u32_le(sector + 20) != 0
        || read_u64_le(sector + 24) != 1
        || alternate_lba != last_lba) {
        return false;
    }

    *first_usable_lba = read_u64_le(sector + 40);
    *last_usable_lba = read_u64_le(sector + 48);
    *entry_array_lba = read_u64_le(sector + 72);
    *entry_count = read_u32_le(sector + 80);
    *entry_size = read_u32_le(sector + 84);
    *entry_array_crc = read_u32_le(sector + 88);

    if (header_crc32(sector, header_size) != read_u32_le(sector + 16)
        || *first_usable_lba < 2
        || *first_usable_lba > *last_usable_lba
        || *last_usable_lba >= last_lba
        || *entry_array_lba < 2
        || *entry_count == 0 || *entry_count > GPT_MAX_ENTRY_COUNT
        || *entry_size < 128 || *entry_size > GPT_MAX_ENTRY_SIZE
        || (*entry_size % 128U) != 0
        || (((*entry_size / 128U) & ((*entry_size / 128U) - 1U)) != 0)) {
        return false;
    }

    array_bytes = (uint64_t)*entry_count * *entry_size;
    if (array_bytes < GPT_MIN_ENTRY_ARRAY_BYTES
        || array_bytes > GPT_MAX_ENTRY_ARRAY_BYTES) {
        return false;
    }

    array_sectors = array_bytes / sector_size;
    if (array_bytes % sector_size != 0) {
        array_sectors++;
    }
    if (*entry_array_lba >= sector_count
        || array_sectors > sector_count - *entry_array_lba) {
        return false;
    }
    array_end_lba = *entry_array_lba + array_sectors;
    return array_end_lba <= *first_usable_lba;
}

static enum gpt_scan_result read_entry_array(
    uint64_t device_index, const struct block_device *device,
    uint64_t entry_array_lba, uint64_t array_bytes, uint32_t entry_count,
    uint32_t entry_size, uint64_t first_usable_lba,
    uint64_t last_usable_lba, uint32_t expected_crc,
    struct gpt_partition *partitions, uint32_t partition_capacity,
    uint32_t *partition_count) {
    uint64_t bytes_remaining = array_bytes;
    uint64_t current_lba = entry_array_lba;
    uint32_t crc = 0xffffffffU;
    uint32_t parsed_count = 0;
    uint32_t entry_number = 0;
    uint32_t entry_offset = 0;
    uint8_t entry_prefix[GPT_ENTRY_PREFIX_SIZE] = {0};
    uint32_t sectors_per_read = GPT_IO_BUFFER_SIZE / device->sector_size;

    while (bytes_remaining != 0) {
        uint64_t sectors_remaining = bytes_remaining / device->sector_size;
        if (bytes_remaining % device->sector_size != 0) {
            sectors_remaining++;
        }
        uint32_t read_count = sectors_remaining < sectors_per_read
            ? (uint32_t)sectors_remaining : sectors_per_read;
        uint64_t chunk_capacity = (uint64_t)read_count * device->sector_size;
        size_t chunk_bytes = bytes_remaining < chunk_capacity
            ? (size_t)bytes_remaining : (size_t)chunk_capacity;

        if (!block_device_read(device_index, current_lba, read_count,
                               gpt_io_buffer)) {
            return GPT_SCAN_IO_ERROR;
        }
        crc = crc32_update(crc, gpt_io_buffer, chunk_bytes);

        size_t position = 0;
        while (position < chunk_bytes) {
            uint32_t entry_bytes_left = entry_size - entry_offset;
            size_t segment_size = chunk_bytes - position;
            if (segment_size > entry_bytes_left) {
                segment_size = entry_bytes_left;
            }

            if (entry_offset < GPT_ENTRY_PREFIX_SIZE) {
                uint32_t prefix_bytes_left = GPT_ENTRY_PREFIX_SIZE
                    - entry_offset;
                size_t prefix_copy_size = segment_size < prefix_bytes_left
                    ? segment_size : prefix_bytes_left;
                copy_bytes(entry_prefix + entry_offset,
                           gpt_io_buffer + position, prefix_copy_size);
            }

            position += segment_size;
            bytes_remaining -= segment_size;
            entry_offset += (uint32_t)segment_size;
            if (entry_offset == entry_size) {
                enum gpt_scan_result result = append_partition(
                    entry_prefix, entry_number, first_usable_lba,
                    last_usable_lba, partitions, partition_capacity,
                    &parsed_count);
                if (result != GPT_SCAN_OK) {
                    return result;
                }
                entry_number++;
                entry_offset = 0;
            }
        }

        uint64_t sectors_read = chunk_capacity / device->sector_size;
        current_lba += sectors_read;
    }

    if (entry_offset != 0 || entry_number != entry_count
        || ~crc != expected_crc) {
        return GPT_SCAN_INVALID;
    }

    *partition_count = parsed_count;
    return GPT_SCAN_OK;
}

enum gpt_scan_result gpt_scan_device(uint64_t device_index,
                                     struct gpt_partition *partitions,
                                     uint32_t partition_capacity,
                                     uint32_t *partition_count) {
    const struct block_device *device = block_device_get(device_index);
    uint8_t header[GPT_MAX_SECTOR_SIZE];
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint64_t entry_array_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entry_array_crc;
    uint64_t array_bytes;
    uint32_t output_capacity;

    if (partition_count == 0) {
        return GPT_SCAN_INVALID;
    }
    *partition_count = 0;
    if (device == 0 || partitions == 0 || partition_capacity == 0
        || device->sector_size < GPT_MIN_SECTOR_SIZE
        || device->sector_size > GPT_MAX_SECTOR_SIZE
        || (device->sector_size & (device->sector_size - 1)) != 0
        || device->sector_count < 6) {
        return GPT_SCAN_INVALID;
    }
    output_capacity = partition_capacity < GPT_MAX_PARTITIONS
        ? partition_capacity : GPT_MAX_PARTITIONS;
    if (!block_device_read(device_index, 1, 1, header)) {
        return GPT_SCAN_IO_ERROR;
    }
    if (!valid_signature(header)) {
        return GPT_SCAN_NOT_PRESENT;
    }
    if (!header_is_valid(header, device->sector_size, device->sector_count,
                         &first_usable_lba, &last_usable_lba,
                         &entry_array_lba, &entry_count, &entry_size,
                         &entry_array_crc)) {
        return GPT_SCAN_INVALID;
    }

    array_bytes = (uint64_t)entry_count * entry_size;
    return read_entry_array(device_index, device, entry_array_lba, array_bytes,
                            entry_count, entry_size, first_usable_lba,
                            last_usable_lba, entry_array_crc, partitions,
                            output_capacity, partition_count);
}
