#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "block_device.h"
#include "fat32.h"
#include "arch/x86_64/serial.h"

#define FAT32_MIN_SECTOR_SIZE 512U
#define FAT32_MAX_SECTOR_SIZE 4096U
#define FAT32_MIN_CLUSTERS 65525U
#define FAT32_DIRECTORY_ENTRY_SIZE 32U
#define FAT32_ATTRIBUTE_LONG_NAME 0x0fU
#define FAT32_ATTRIBUTE_VOLUME_ID 0x08U
#define FAT32_ATTRIBUTE_DIRECTORY 0x10U
#define FAT32_CLUSTER_MASK 0x0fffffffU
#define FAT32_CLUSTER_BAD 0x0ffffff7U
#define FAT32_CLUSTER_EOC_MIN 0x0ffffff8U

static uint8_t sector_buffer[FAT32_MAX_SECTOR_SIZE];
static struct fat32_directory_entry root_entry_scratch[
    FAT32_MAX_DIRECTORY_ENTRIES];

static bool cluster_sector_lba(const struct fat32_volume *volume,
                               uint32_t cluster, uint32_t sector_in_cluster,
                               uint64_t *lba);
static void make_short_name(const uint8_t *raw, char *name);
static bool short_name_equals(const char *left, const char *right);
static enum fat32_result list_directory(
    const struct fat32_volume *volume, uint32_t first_cluster,
    struct fat32_directory_entry *entries, uint32_t capacity,
    uint32_t *entry_count);
static enum fat32_result fat32_create_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name);

static uint16_t read_u16_le(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

static void write_u16_le(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static bool power_of_two(uint32_t value) {
    return value != 0 && (value & (value - 1U)) == 0;
}

enum fat32_result fat32_mount(uint64_t device_index,
                              const struct gpt_partition *partition,
                              struct fat32_volume *volume) {
    const struct block_device *device = block_device_get(device_index);
    uint64_t partition_sectors;
    uint64_t first_data_sector;
    uint64_t fat_sectors_total;
    uint64_t cluster_count;
    uint64_t fat_capacity;

    if (device == 0 || partition == 0 || volume == 0
        || partition->first_lba > partition->last_lba
        || partition->last_lba >= device->sector_count) {
        return FAT32_INVALID;
    }
    partition_sectors = partition->last_lba - partition->first_lba + 1;
    if (device->sector_size < FAT32_MIN_SECTOR_SIZE
        || device->sector_size > FAT32_MAX_SECTOR_SIZE
        || !power_of_two(device->sector_size)) {
        return FAT32_NOT_FAT32;
    }
    if (!block_device_read(device_index, partition->first_lba, 1,
                           sector_buffer)) {
        return FAT32_IO_ERROR;
    }

    uint8_t *boot = sector_buffer;
    uint32_t bytes_per_sector = read_u16_le(boot + 11);
    uint32_t sectors_per_cluster = boot[13];
    uint32_t reserved_sectors = read_u16_le(boot + 14);
    uint32_t fat_count = boot[16];
    uint32_t root_entry_count = read_u16_le(boot + 17);
    uint32_t total_sectors_16 = read_u16_le(boot + 19);
    uint32_t fat_size_16 = read_u16_le(boot + 22);
    uint32_t total_sectors_32 = read_u32_le(boot + 32);
    uint32_t fat_size_32 = read_u32_le(boot + 36);
    uint32_t extended_flags = read_u16_le(boot + 40);
    uint32_t filesystem_version = read_u16_le(boot + 42);
    uint32_t root_cluster = read_u32_le(boot + 44) & FAT32_CLUSTER_MASK;
    uint32_t active_fat = (extended_flags & 0x0080U) != 0
        ? extended_flags & 0x000fU : 0;

    if (boot[510] != 0x55 || boot[511] != 0xaa
        || bytes_per_sector != device->sector_size
        || !power_of_two(sectors_per_cluster) || sectors_per_cluster > 128
        || reserved_sectors == 0 || fat_count == 0 || fat_count > 2
        || root_entry_count != 0 || total_sectors_16 != 0
        || fat_size_16 != 0 || total_sectors_32 == 0 || fat_size_32 == 0
        || filesystem_version != 0 || active_fat >= fat_count
        || total_sectors_32 > partition_sectors) {
        return FAT32_NOT_FAT32;
    }

    fat_sectors_total = (uint64_t)fat_count * fat_size_32;
    first_data_sector = (uint64_t)reserved_sectors + fat_sectors_total;
    if (first_data_sector >= total_sectors_32) {
        return FAT32_INVALID;
    }
    cluster_count = (total_sectors_32 - first_data_sector)
        / sectors_per_cluster;
    fat_capacity = ((uint64_t)fat_size_32 * device->sector_size) / 4U;
    if (cluster_count < FAT32_MIN_CLUSTERS
        || cluster_count > UINT32_MAX - 2U
        || fat_capacity < cluster_count + 2U
        || root_cluster < 2 || root_cluster >= cluster_count + 2U
        || partition->first_lba > UINT64_MAX - first_data_sector
        || partition->first_lba > UINT64_MAX - reserved_sectors
        || partition->first_lba + total_sectors_32 - 1
            > partition->last_lba) {
        return FAT32_INVALID;
    }

    volume->device_index = device_index;
    volume->partition_start_lba = partition->first_lba;
    volume->first_data_lba = partition->first_lba + first_data_sector;
    volume->fat_start_lba = partition->first_lba + reserved_sectors;
    volume->sector_size = device->sector_size;
    volume->sectors_per_cluster = sectors_per_cluster;
    volume->fat_sector_count = fat_size_32;
    volume->fat_count = fat_count;
    volume->active_fat_index = active_fat;
    volume->fat_mirroring = (extended_flags & 0x0080U) == 0;
    volume->cluster_count = (uint32_t)cluster_count;
    volume->root_cluster = root_cluster;
    return FAT32_OK;
}

static bool valid_volume(const struct fat32_volume *volume) {
    return volume != 0 && volume->sector_size >= FAT32_MIN_SECTOR_SIZE
        && volume->sector_size <= FAT32_MAX_SECTOR_SIZE
        && power_of_two(volume->sector_size)
        && power_of_two(volume->sectors_per_cluster)
        && volume->cluster_count >= FAT32_MIN_CLUSTERS
        && volume->cluster_count <= UINT32_MAX - 2U
        && volume->fat_count != 0 && volume->fat_count <= 2
        && volume->active_fat_index < volume->fat_count
        && volume->root_cluster >= 2
        && volume->root_cluster < volume->cluster_count + 2U;
}

static enum fat32_result next_cluster(const struct fat32_volume *volume,
                                      uint32_t cluster, uint32_t *next,
                                      bool *end_of_chain) {
    const struct block_device *device = block_device_get(volume->device_index);
    uint64_t fat_offset = (uint64_t)cluster * 4U;
    uint64_t fat_sector_offset = fat_offset / volume->sector_size;
    uint32_t byte_offset = (uint32_t)(fat_offset % volume->sector_size);

    if (next == 0 || end_of_chain == 0 || cluster < 2
        || cluster >= volume->cluster_count + 2U
        || fat_sector_offset >= volume->fat_sector_count) {
        return FAT32_INVALID;
    }
    uint64_t active_fat_offset = volume->fat_mirroring
        ? 0 : (uint64_t)volume->active_fat_index * volume->fat_sector_count;
    if (device == 0
        || volume->fat_start_lba > UINT64_MAX - active_fat_offset
        || volume->fat_start_lba + active_fat_offset
            > UINT64_MAX - fat_sector_offset
        || !block_device_read(volume->device_index,
                              volume->fat_start_lba + active_fat_offset
                                  + fat_sector_offset,
                              1, sector_buffer)) {
        return FAT32_IO_ERROR;
    }

    uint32_t value = read_u32_le(sector_buffer + byte_offset)
        & FAT32_CLUSTER_MASK;
    if (value >= FAT32_CLUSTER_EOC_MIN) {
        *end_of_chain = true;
        *next = 0;
        return FAT32_OK;
    }
    if (value == 0 || value == 1 || value == FAT32_CLUSTER_BAD
        || (value >= 0x0ffffff0U && value < FAT32_CLUSTER_EOC_MIN)
        || value < 2 || value >= volume->cluster_count + 2U) {
        return FAT32_INVALID;
    }

    *end_of_chain = false;
    *next = value;
    return FAT32_OK;
}

static enum fat32_result read_fat_entry(const struct fat32_volume *volume,
                                        uint32_t cluster, uint32_t *value) {
    uint64_t byte_offset = (uint64_t)cluster * 4U;
    uint64_t sector_offset = byte_offset / volume->sector_size;
    uint32_t offset = (uint32_t)(byte_offset % volume->sector_size);
    uint64_t active_offset = volume->fat_mirroring
        ? 0 : (uint64_t)volume->active_fat_index * volume->fat_sector_count;
    if (value == 0 || cluster >= volume->cluster_count + 2U
        || sector_offset >= volume->fat_sector_count
        || !block_device_read(volume->device_index,
                              volume->fat_start_lba + active_offset
                                  + sector_offset,
                              1, sector_buffer)) {
        return FAT32_IO_ERROR;
    }
    *value = read_u32_le(sector_buffer + offset) & FAT32_CLUSTER_MASK;
    return FAT32_OK;
}

static enum fat32_result write_fat_entry(const struct fat32_volume *volume,
                                         uint32_t cluster, uint32_t value) {
    uint64_t byte_offset = (uint64_t)cluster * 4U;
    uint64_t sector_offset = byte_offset / volume->sector_size;
    uint32_t offset = (uint32_t)(byte_offset % volume->sector_size);
    uint32_t copies = volume->fat_mirroring ? volume->fat_count : 1;
    if (cluster >= volume->cluster_count + 2U
        || sector_offset >= volume->fat_sector_count) {
        return FAT32_INVALID;
    }
    for (uint32_t copy = 0; copy < copies; copy++) {
        uint32_t fat_index = volume->fat_mirroring
            ? copy : volume->active_fat_index;
        uint64_t lba = volume->fat_start_lba
            + (uint64_t)fat_index * volume->fat_sector_count
            + sector_offset;
        if (!block_device_read(volume->device_index, lba, 1, sector_buffer)) {
            return FAT32_IO_ERROR;
        }
        uint32_t previous = read_u32_le(sector_buffer + offset);
        write_u32_le(sector_buffer + offset,
                     (previous & 0xf0000000U) | (value & FAT32_CLUSTER_MASK));
        if (!block_device_write(volume->device_index, lba, 1, sector_buffer)) {
            return FAT32_IO_ERROR;
        }
    }
    return FAT32_OK;
}

static enum fat32_result allocate_cluster(const struct fat32_volume *volume,
                                          uint32_t *allocated) {
    uint64_t entries_per_sector = volume->sector_size / 4U;
    uint64_t active_offset = volume->fat_mirroring
        ? 0 : (uint64_t)volume->active_fat_index * volume->fat_sector_count;
    for (uint32_t sector = 0; sector < volume->fat_sector_count; sector++) {
        if (!block_device_read(volume->device_index,
                               volume->fat_start_lba + active_offset + sector,
                               1, sector_buffer)) {
            return FAT32_IO_ERROR;
        }
        for (uint32_t index = 0; index < entries_per_sector; index++) {
            uint64_t candidate = (uint64_t)sector * entries_per_sector + index;
            if (candidate < 2 || candidate >= volume->cluster_count + 2U) {
                continue;
            }
            if ((read_u32_le(sector_buffer + index * 4U)
                 & FAT32_CLUSTER_MASK) == 0) {
                enum fat32_result result = write_fat_entry(
                    volume, (uint32_t)candidate, FAT32_CLUSTER_MASK);
                if (result != FAT32_OK) {
                    return result;
                }
                *allocated = (uint32_t)candidate;
                return FAT32_OK;
            }
        }
    }
    return FAT32_LIMIT_EXCEEDED;
}

static enum fat32_result free_cluster_chain(const struct fat32_volume *volume,
                                            uint32_t cluster) {
    if (cluster == 0) {
        return FAT32_OK;
    }
    for (uint32_t step = 0; step < volume->cluster_count; step++) {
        uint32_t next_value;
        if (cluster < 2 || cluster >= volume->cluster_count + 2U) {
            return FAT32_INVALID;
        }
        enum fat32_result result = read_fat_entry(volume, cluster, &next_value);
        if (result != FAT32_OK) {
            return result;
        }
        result = write_fat_entry(volume, cluster, 0);
        if (result != FAT32_OK) {
            return result;
        }
        if (next_value >= FAT32_CLUSTER_EOC_MIN) {
            return block_device_flush(volume->device_index)
                ? FAT32_OK : FAT32_IO_ERROR;
        }
        if (next_value < 2 || next_value >= volume->cluster_count + 2U) {
            return FAT32_INVALID;
        }
        cluster = next_value;
    }
    return FAT32_INVALID;
}

static bool encode_short_name(const char *name, uint8_t raw[11]) {
    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    bool in_extension = false;
    for (uint32_t i = 0; i < 11; i++) {
        raw[i] = ' ';
    }
    if (name == 0 || *name == '\0') {
        return false;
    }
    for (; *name != '\0'; name++) {
        char value = *name;
        if (value == '.') {
            if (in_extension || base_length == 0) {
                return false;
            }
            in_extension = true;
            continue;
        }
        if (value >= 'a' && value <= 'z') {
            value = (char)(value - 'a' + 'A');
        }
        if (!((value >= 'A' && value <= 'Z')
              || (value >= '0' && value <= '9') || value == '_'
              || value == '-' || value == '$' || value == '~')) {
            return false;
        }
        if (in_extension) {
            if (++extension_length > 3) {
                return false;
            }
            raw[8 + extension_length - 1] = (uint8_t)value;
        } else {
            if (++base_length > 8) {
                return false;
            }
            raw[base_length - 1] = (uint8_t)value;
        }
    }
    return base_length != 0 && (!in_extension || extension_length != 0);
}

static enum fat32_result find_root_entry_slot(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const uint8_t raw_name[11],
    uint64_t *slot_lba, uint32_t *slot_offset, uint8_t existing_entry[32],
    bool *exists) {
    uint32_t cluster = directory_cluster;
    bool have_free_slot = false;
    uint64_t free_lba = 0;
    uint32_t free_offset = 0;
    for (uint32_t step = 0; step < volume->cluster_count; step++) {
        for (uint32_t sector = 0; sector < volume->sectors_per_cluster;
             sector++) {
            uint64_t lba;
            if (!cluster_sector_lba(volume, cluster, sector, &lba)
                || !block_device_read(volume->device_index, lba, 1,
                                      sector_buffer)) {
                return FAT32_IO_ERROR;
            }
            for (uint32_t offset = 0; offset < volume->sector_size;
                 offset += FAT32_DIRECTORY_ENTRY_SIZE) {
                uint8_t *entry = sector_buffer + offset;
                if (entry[0] == 0) {
                    if (!have_free_slot) {
                        free_lba = lba;
                        free_offset = offset;
                        have_free_slot = true;
                    }
                    *slot_lba = free_lba;
                    *slot_offset = free_offset;
                    *exists = false;
                    return FAT32_OK;
                }
                if (entry[0] == 0xe5) {
                    if (!have_free_slot) {
                        free_lba = lba;
                        free_offset = offset;
                        have_free_slot = true;
                    }
                    continue;
                }
                bool same = true;
                for (uint32_t i = 0; i < 11; i++) {
                    if (entry[i] != raw_name[i]) {
                        same = false;
                        break;
                    }
                }
                if (same) {
                    *slot_lba = lba;
                    *slot_offset = offset;
                    for (uint32_t i = 0; i < FAT32_DIRECTORY_ENTRY_SIZE; i++) {
                        existing_entry[i] = entry[i];
                    }
                    *exists = true;
                    return FAT32_OK;
                }
            }
        }
        uint32_t next;
        bool end_of_chain;
        enum fat32_result result = next_cluster(volume, cluster, &next,
                                                 &end_of_chain);
        if (result != FAT32_OK) {
            return result;
        }
        if (end_of_chain) {
            if (have_free_slot) {
                *slot_lba = free_lba;
                *slot_offset = free_offset;
                *exists = false;
                return FAT32_OK;
            }
            return FAT32_LIMIT_EXCEEDED;
        }
        cluster = next;
    }
    return FAT32_INVALID;
}

enum fat32_result fat32_write_root_file(
    const struct fat32_volume *volume, const char *short_name,
    const void *buffer, uint32_t bytes) {
    return fat32_write_directory_file(volume, volume->root_cluster,
                                      short_name, buffer, bytes);
}

enum fat32_result fat32_write_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name, const void *buffer, uint32_t bytes) {
    const uint8_t *source = buffer;
    uint8_t raw_name[11];
    uint8_t old_entry[FAT32_DIRECTORY_ENTRY_SIZE];
    uint32_t allocated[FAT32_MAX_FILE_WRITE_BYTES / FAT32_MIN_SECTOR_SIZE];
    uint32_t allocated_count = 0;
    uint32_t old_cluster = 0;
    uint64_t slot_lba;
    uint32_t slot_offset;
    bool exists;

    if (!valid_volume(volume) || directory_cluster < 2
        || directory_cluster >= volume->cluster_count + 2U
        || bytes > FAT32_MAX_FILE_WRITE_BYTES
        || (bytes != 0 && buffer == 0)
        || !encode_short_name(short_name, raw_name)) {
        return FAT32_INVALID;
    }
    enum fat32_result result = find_root_entry_slot(
        volume, directory_cluster, raw_name, &slot_lba, &slot_offset,
        old_entry, &exists);
    if (result != FAT32_OK) {
        return result;
    }
    if (exists) {
        if ((old_entry[11] & FAT32_ATTRIBUTE_DIRECTORY) != 0) {
            return FAT32_INVALID;
        }
        old_cluster = ((uint32_t)read_u16_le(old_entry + 20) << 16)
            | read_u16_le(old_entry + 26);
    }

    uint64_t cluster_bytes = (uint64_t)volume->sector_size
        * volume->sectors_per_cluster;
    uint32_t needed = bytes == 0 ? 0
        : (uint32_t)((bytes + cluster_bytes - 1) / cluster_bytes);
    if (needed > sizeof(allocated) / sizeof(allocated[0])) {
        return FAT32_LIMIT_EXCEEDED;
    }
    for (uint32_t i = 0; i < needed; i++) {
        result = allocate_cluster(volume, &allocated[i]);
        if (result != FAT32_OK) {
            (void)free_cluster_chain(volume,
                                     allocated_count == 0 ? 0 : allocated[0]);
            return result;
        }
        if (allocated_count != 0) {
            result = write_fat_entry(volume, allocated[allocated_count - 1],
                                     allocated[i]);
            if (result != FAT32_OK) {
                (void)free_cluster_chain(volume, allocated[0]);
                (void)write_fat_entry(volume, allocated[i], 0);
                return result;
            }
        }
        allocated_count++;
    }

    uint32_t remaining = bytes;
    uint32_t source_offset = 0;
    for (uint32_t i = 0; i < allocated_count; i++) {
        for (uint32_t sector = 0;
             sector < volume->sectors_per_cluster && remaining != 0; sector++) {
            uint64_t lba;
            if (!cluster_sector_lba(volume, allocated[i], sector, &lba)) {
                (void)free_cluster_chain(volume, allocated[0]);
                return FAT32_INVALID;
            }
            for (uint32_t j = 0; j < volume->sector_size; j++) {
                sector_buffer[j] = 0;
            }
            uint32_t chunk = remaining < volume->sector_size
                ? remaining : volume->sector_size;
            for (uint32_t j = 0; j < chunk; j++) {
                sector_buffer[j] = source[source_offset + j];
            }
            if (!block_device_write(volume->device_index, lba, 1,
                                    sector_buffer)) {
                (void)free_cluster_chain(volume, allocated[0]);
                return FAT32_IO_ERROR;
            }
            source_offset += chunk;
            remaining -= chunk;
        }
    }
    if (!block_device_flush(volume->device_index)) {
        if (allocated_count != 0) {
            (void)free_cluster_chain(volume, allocated[0]);
        }
        return FAT32_IO_ERROR;
    }

    if (!block_device_read(volume->device_index, slot_lba, 1, sector_buffer)) {
        if (allocated_count != 0) {
            (void)free_cluster_chain(volume, allocated[0]);
        }
        return FAT32_IO_ERROR;
    }
    uint8_t *entry = sector_buffer + slot_offset;
    if (!exists) {
        for (uint32_t i = 0; i < FAT32_DIRECTORY_ENTRY_SIZE; i++) {
            entry[i] = 0;
        }
        for (uint32_t i = 0; i < sizeof(raw_name); i++) {
            entry[i] = raw_name[i];
        }
        entry[11] = 0x20;
    }
    uint32_t first_cluster = allocated_count == 0 ? 0 : allocated[0];
    write_u16_le(entry + 20, (uint16_t)(first_cluster >> 16));
    write_u16_le(entry + 26, (uint16_t)first_cluster);
    write_u32_le(entry + 28, bytes);
    if (!block_device_write(volume->device_index, slot_lba, 1, sector_buffer)
        || !block_device_flush(volume->device_index)) {
        if (allocated_count != 0) {
            (void)free_cluster_chain(volume, allocated[0]);
        }
        return FAT32_IO_ERROR;
    }
    if (old_cluster != 0) {
        result = free_cluster_chain(volume, old_cluster);
        if (result != FAT32_OK) {
            return result;
        }
    }
    return FAT32_OK;
}

enum fat32_result fat32_create_root_file(
    const struct fat32_volume *volume, const char *short_name) {
    return fat32_create_directory_file(volume, volume != 0
        ? volume->root_cluster : 0, short_name);
}

static enum fat32_result fat32_create_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name) {
    struct fat32_directory_entry entries[FAT32_MAX_DIRECTORY_ENTRIES];
    uint32_t entry_count = 0;
    if (!valid_volume(volume) || directory_cluster < 2
        || directory_cluster >= volume->cluster_count + 2U) {
        return FAT32_INVALID;
    }
    enum fat32_result result = list_directory(volume, directory_cluster,
        entries, FAT32_MAX_DIRECTORY_ENTRIES, &entry_count);
    if (result != FAT32_OK) {
        return result;
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        if (short_name_equals(entries[i].short_name, short_name)) {
            return FAT32_OK;
        }
    }
    return fat32_write_directory_file(volume, directory_cluster,
                                      short_name, 0, 0);
}

enum fat32_result fat32_create_root_directory(
    const struct fat32_volume *volume, const char *short_name) {
    return fat32_create_directory(volume, volume != 0
                                      ? volume->root_cluster : 0, short_name);
}

enum fat32_result fat32_create_directory(
    const struct fat32_volume *volume, uint32_t parent_cluster,
    const char *short_name) {
    uint8_t raw_name[11];
    uint8_t existing[FAT32_DIRECTORY_ENTRY_SIZE];
    uint64_t slot_lba;
    uint32_t slot_offset;
    bool exists;
    uint32_t cluster;

    if (!valid_volume(volume) || parent_cluster < 2
        || parent_cluster >= volume->cluster_count + 2U
        || !encode_short_name(short_name, raw_name)) {
        return FAT32_INVALID;
    }
    enum fat32_result result = find_root_entry_slot(
        volume, parent_cluster, raw_name, &slot_lba, &slot_offset, existing,
        &exists);
    if (result != FAT32_OK) {
        return result;
    }
    if (exists) {
        return FAT32_INVALID;
    }
    result = allocate_cluster(volume, &cluster);
    if (result != FAT32_OK) {
        return result;
    }
    for (uint32_t sector = 0; sector < volume->sectors_per_cluster; sector++) {
        for (uint32_t i = 0; i < volume->sector_size; i++) {
            sector_buffer[i] = 0;
        }
        if (sector == 0) {
            for (uint32_t i = 0; i < 11; i++) {
                sector_buffer[i] = ' ';
                sector_buffer[FAT32_DIRECTORY_ENTRY_SIZE + i] = ' ';
            }
            sector_buffer[0] = '.';
            sector_buffer[FAT32_DIRECTORY_ENTRY_SIZE] = '.';
            sector_buffer[FAT32_DIRECTORY_ENTRY_SIZE + 1] = '.';
            sector_buffer[11] = FAT32_ATTRIBUTE_DIRECTORY;
            sector_buffer[FAT32_DIRECTORY_ENTRY_SIZE + 11]
                = FAT32_ATTRIBUTE_DIRECTORY;
            write_u16_le(sector_buffer + 20, (uint16_t)(cluster >> 16));
            write_u16_le(sector_buffer + 26, (uint16_t)cluster);
            write_u16_le(sector_buffer + FAT32_DIRECTORY_ENTRY_SIZE + 20,
                         (uint16_t)(parent_cluster >> 16));
            write_u16_le(sector_buffer + FAT32_DIRECTORY_ENTRY_SIZE + 26,
                         (uint16_t)parent_cluster);
        }
        uint64_t lba;
        if (!cluster_sector_lba(volume, cluster, sector, &lba)
            || !block_device_write(volume->device_index, lba, 1,
                                   sector_buffer)) {
            (void)free_cluster_chain(volume, cluster);
            return FAT32_IO_ERROR;
        }
    }
    if (!block_device_flush(volume->device_index)
        || !block_device_read(volume->device_index, slot_lba, 1,
                              sector_buffer)) {
        (void)free_cluster_chain(volume, cluster);
        return FAT32_IO_ERROR;
    }
    uint8_t *entry = sector_buffer + slot_offset;
    for (uint32_t i = 0; i < FAT32_DIRECTORY_ENTRY_SIZE; i++) {
        entry[i] = 0;
    }
    for (uint32_t i = 0; i < sizeof(raw_name); i++) {
        entry[i] = raw_name[i];
    }
    entry[11] = FAT32_ATTRIBUTE_DIRECTORY;
    write_u16_le(entry + 20, (uint16_t)(cluster >> 16));
    write_u16_le(entry + 26, (uint16_t)cluster);
    if (!block_device_write(volume->device_index, slot_lba, 1, sector_buffer)
        || !block_device_flush(volume->device_index)) {
        (void)free_cluster_chain(volume, cluster);
        return FAT32_IO_ERROR;
    }
    return FAT32_OK;
}

enum fat32_result fat32_delete_root_file(
    const struct fat32_volume *volume, const char *short_name) {
    return fat32_delete_directory_file(volume, volume != 0
        ? volume->root_cluster : 0, short_name);
}

enum fat32_result fat32_delete_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name) {
    uint32_t cluster = directory_cluster;
    if (!valid_volume(volume) || directory_cluster < 2
        || directory_cluster >= volume->cluster_count + 2U
        || short_name == 0 || *short_name == '\0') {
        return FAT32_INVALID;
    }
    for (uint32_t step = 0; step < volume->cluster_count; step++) {
        for (uint32_t sector = 0; sector < volume->sectors_per_cluster;
             sector++) {
            uint64_t lba;
            if (!cluster_sector_lba(volume, cluster, sector, &lba)
                || !block_device_read(volume->device_index, lba, 1,
                                      sector_buffer)) {
                return FAT32_IO_ERROR;
            }
            for (uint32_t offset = 0; offset < volume->sector_size;
                 offset += FAT32_DIRECTORY_ENTRY_SIZE) {
                uint8_t *entry = sector_buffer + offset;
                if (entry[0] == 0) {
                    return FAT32_NOT_FOUND;
                }
                char candidate[FAT32_SHORT_NAME_LENGTH];
                make_short_name(entry, candidate);
                if (entry[0] != 0xe5 && short_name_equals(candidate,
                                                           short_name)) {
                    if ((entry[11] & FAT32_ATTRIBUTE_DIRECTORY) != 0) {
                        return FAT32_INVALID;
                    }
                    uint32_t file_cluster =
                        ((uint32_t)read_u16_le(entry + 20) << 16)
                        | read_u16_le(entry + 26);
                    entry[0] = 0xe5;
                    if (!block_device_write(volume->device_index, lba, 1,
                                            sector_buffer)
                        || !block_device_flush(volume->device_index)) {
                        return FAT32_IO_ERROR;
                    }
                    if (file_cluster == 0) {
                        return FAT32_OK;
                    }
                    for (uint32_t chain_step = 0;
                         chain_step < volume->cluster_count; chain_step++) {
                        if (file_cluster < 2
                            || file_cluster >= volume->cluster_count + 2U) {
                            return FAT32_INVALID;
                        }
                        uint32_t next_value;
                        enum fat32_result result = read_fat_entry(
                            volume, file_cluster, &next_value);
                        if (result != FAT32_OK) {
                            return result;
                        }
                        result = write_fat_entry(volume, file_cluster, 0);
                        if (result != FAT32_OK) {
                            return result;
                        }
                        if (next_value >= FAT32_CLUSTER_EOC_MIN) {
                            return block_device_flush(volume->device_index)
                                ? FAT32_OK : FAT32_IO_ERROR;
                        }
                        if (next_value < 2
                            || next_value >= volume->cluster_count + 2U) {
                            return FAT32_INVALID;
                        }
                        file_cluster = next_value;
                    }
                    return FAT32_INVALID;
                }
            }
        }
        uint32_t next;
        bool end_of_chain;
        enum fat32_result result = next_cluster(volume, cluster, &next,
                                                 &end_of_chain);
        if (result != FAT32_OK) {
            return result;
        }
        if (end_of_chain) {
            return FAT32_NOT_FOUND;
        }
        cluster = next;
    }
    return FAT32_INVALID;
}

static bool cluster_sector_lba(const struct fat32_volume *volume,
                               uint32_t cluster, uint32_t sector_in_cluster,
                               uint64_t *lba) {
    uint64_t cluster_offset;

    if (cluster < 2 || cluster >= volume->cluster_count + 2U
        || sector_in_cluster >= volume->sectors_per_cluster
        || (uint64_t)(cluster - 2U)
            > UINT64_MAX / volume->sectors_per_cluster) {
        return false;
    }
    cluster_offset = (uint64_t)(cluster - 2U)
        * volume->sectors_per_cluster + sector_in_cluster;
    if (volume->first_data_lba > UINT64_MAX - cluster_offset) {
        return false;
    }
    *lba = volume->first_data_lba + cluster_offset;
    return true;
}

static void make_short_name(const uint8_t *raw, char *name) {
    uint32_t position = 0;
    uint32_t base_length = 8;
    uint32_t extension_length = 3;

    while (base_length != 0 && raw[base_length - 1] == ' ') {
        base_length--;
    }
    while (extension_length != 0 && raw[8 + extension_length - 1] == ' ') {
        extension_length--;
    }
    for (uint32_t i = 0; i < base_length; i++) {
        uint8_t character = raw[i] == 0x05 && i == 0 ? 0xe5 : raw[i];
        name[position++] = character >= 32 && character <= 126
            ? (char)character : '?';
    }
    if (extension_length != 0) {
        name[position++] = '.';
        for (uint32_t i = 0; i < extension_length; i++) {
            uint8_t character = raw[8 + i];
            name[position++] = character >= 32 && character <= 126
                ? (char)character : '?';
        }
    }
    name[position] = '\0';
}

static bool short_name_equals(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        char a = *left;
        char b = *right;
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static enum fat32_result append_directory_entry(
    const uint8_t *raw, struct fat32_directory_entry *entries,
    uint32_t capacity, uint32_t *entry_count) {
    uint8_t attributes = raw[11];

    if (raw[0] == 0xe5 || attributes == FAT32_ATTRIBUTE_LONG_NAME
        || (attributes & FAT32_ATTRIBUTE_VOLUME_ID) != 0
        || raw[0] == '.') {
        return FAT32_OK;
    }
    if (*entry_count == capacity) {
        return FAT32_LIMIT_EXCEEDED;
    }

    struct fat32_directory_entry *entry = &entries[(*entry_count)++];
    make_short_name(raw, entry->short_name);
    entry->attributes = attributes;
    entry->first_cluster = ((uint32_t)read_u16_le(raw + 20) << 16)
        | read_u16_le(raw + 26);
    entry->size = read_u32_le(raw + 28);
    return FAT32_OK;
}

static enum fat32_result list_directory(
    const struct fat32_volume *volume, uint32_t first_cluster,
    struct fat32_directory_entry *entries, uint32_t capacity,
    uint32_t *entry_count) {
    uint32_t cluster = first_cluster;

    *entry_count = 0;
    for (uint32_t chain_step = 0; chain_step < volume->cluster_count;
         chain_step++) {
        for (uint32_t sector = 0;
             sector < volume->sectors_per_cluster; sector++) {
            uint64_t lba;
            if (!cluster_sector_lba(volume, cluster, sector, &lba)) {
                return FAT32_INVALID;
            }
            if (!block_device_read(volume->device_index, lba, 1,
                                   sector_buffer)) {
                serial_write("FAT32 read: block_device_read failed lba=");
                serial_write_decimal(lba);
                serial_write("\n");
                return FAT32_IO_ERROR;
            }
            for (uint32_t offset = 0; offset < volume->sector_size;
                 offset += FAT32_DIRECTORY_ENTRY_SIZE) {
                const uint8_t *raw = sector_buffer + offset;
                if (raw[0] == 0) {
                    return FAT32_OK;
                }
                enum fat32_result result = append_directory_entry(
                    raw, entries, capacity, entry_count);
                if (result != FAT32_OK) {
                    return result;
                }
            }
        }

        uint32_t next;
        bool end_of_chain;
        enum fat32_result result = next_cluster(volume, cluster, &next,
                                                 &end_of_chain);
        if (result != FAT32_OK) {
            return result;
        }
        if (end_of_chain) {
            return FAT32_OK;
        }
        cluster = next;
    }
    return FAT32_INVALID;
}

enum fat32_result fat32_list_root(
    const struct fat32_volume *volume,
    struct fat32_directory_entry *entries, uint32_t capacity,
    uint32_t *entry_count) {
    if (!valid_volume(volume) || entries == 0 || capacity == 0
        || entry_count == 0) {
        return FAT32_INVALID;
    }
    return list_directory(volume, volume->root_cluster, entries, capacity,
                          entry_count);
}

enum fat32_result fat32_list_directory(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    struct fat32_directory_entry *entries, uint32_t capacity,
    uint32_t *entry_count) {
    if (!valid_volume(volume) || directory_cluster < 2
        || directory_cluster >= volume->cluster_count + 2U
        || entries == 0 || capacity == 0 || entry_count == 0) {
        return FAT32_INVALID;
    }
    return list_directory(volume, directory_cluster, entries, capacity,
                          entry_count);
}

enum fat32_result fat32_find_directory(
    const struct fat32_volume *volume, uint32_t parent_cluster,
    const char *short_name, uint32_t *directory_cluster) {
    uint32_t entry_count = 0;
    if (!valid_volume(volume) || parent_cluster < 2
        || parent_cluster >= volume->cluster_count + 2U || short_name == 0
        || directory_cluster == 0) {
        return FAT32_INVALID;
    }
    enum fat32_result result = list_directory(
        volume, parent_cluster, root_entry_scratch,
        FAT32_MAX_DIRECTORY_ENTRIES, &entry_count);
    if (result != FAT32_OK) {
        return result;
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        if (short_name_equals(root_entry_scratch[i].short_name, short_name)) {
            if ((root_entry_scratch[i].attributes
                 & FAT32_ATTRIBUTE_DIRECTORY) == 0) {
                return FAT32_INVALID;
            }
            if (root_entry_scratch[i].first_cluster < 2
                || root_entry_scratch[i].first_cluster
                       >= volume->cluster_count + 2U) {
                return FAT32_INVALID;
            }
            *directory_cluster = root_entry_scratch[i].first_cluster;
            return FAT32_OK;
        }
    }
    return FAT32_NOT_FOUND;
}

enum fat32_result fat32_parent_directory(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    uint32_t *parent_cluster) {
    uint64_t lba;
    if (!valid_volume(volume) || parent_cluster == 0
        || directory_cluster < 2
        || directory_cluster >= volume->cluster_count + 2U) {
        return FAT32_INVALID;
    }
    if (directory_cluster == volume->root_cluster) {
        *parent_cluster = volume->root_cluster;
        return FAT32_OK;
    }
    if (!cluster_sector_lba(volume, directory_cluster, 0, &lba)
        || !block_device_read(volume->device_index, lba, 1, sector_buffer)) {
        return FAT32_IO_ERROR;
    }
    const uint8_t *entry = sector_buffer + FAT32_DIRECTORY_ENTRY_SIZE;
    if (entry[0] != '.' || entry[1] != '.'
        || (entry[11] & FAT32_ATTRIBUTE_DIRECTORY) == 0) {
        return FAT32_INVALID;
    }
    uint32_t parent = ((uint32_t)read_u16_le(entry + 20) << 16)
        | read_u16_le(entry + 26);
    if (parent == 0) {
        parent = volume->root_cluster;
    }
    if (parent < 2 || parent >= volume->cluster_count + 2U) {
        return FAT32_INVALID;
    }
    *parent_cluster = parent;
    return FAT32_OK;
}

enum fat32_result fat32_read_root_file(
    const struct fat32_volume *volume, const char *short_name,
    void *buffer, uint32_t capacity, uint32_t *bytes_read) {
    serial_write("FAT32 read_root: name=");
    serial_write(short_name);
    serial_write(" root_cluster=");
    serial_write_decimal(volume != 0 ? volume->root_cluster : 999999);
    serial_write("\n");
    return fat32_read_directory_file(volume, volume != 0
        ? volume->root_cluster : 0, short_name, buffer, capacity, bytes_read);
}

enum fat32_result fat32_read_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name, void *buffer, uint32_t capacity,
    uint32_t *bytes_read) {
    uint32_t entry_count = 0;
    uint32_t remaining = UINT32_MAX;
    uint32_t cluster;
    uint8_t *destination = buffer;
    struct fat32_directory_entry *entry = 0;

    serial_write("FAT32 read_dir: dev=");
    serial_write_decimal(volume->device_index);
    serial_write(" sec_size=");
    serial_write_decimal(volume->sector_size);
    serial_write(" spc=");
    serial_write_decimal(volume->sectors_per_cluster);
    serial_write(" cluster_count=");
    serial_write_decimal(volume->cluster_count);
    serial_write(" root_cluster=");
    serial_write_decimal(volume->root_cluster);
    serial_write(" dir_cluster=");
    serial_write_decimal(directory_cluster);
    serial_write(" name=");
    serial_write(short_name);
    serial_write("\n");

    if (!valid_volume(volume) || directory_cluster < 2
        || directory_cluster >= volume->cluster_count + 2U
        || short_name == 0 || buffer == 0
        || bytes_read == 0) {
        serial_write("FAT32 read_dir: INVALID volume or params\n");
        return FAT32_INVALID;
    }
    *bytes_read = 0;
    enum fat32_result result = list_directory(
        volume, directory_cluster, root_entry_scratch,
        FAT32_MAX_DIRECTORY_ENTRIES, &entry_count);
    serial_write("FAT32 list_dir: entry_count=");
    serial_write_decimal(entry_count);
    serial_write(" result=");
    serial_write_decimal(result);
    serial_write("\n");
    if (result != FAT32_OK) {
        return result;
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        if (short_name_equals(root_entry_scratch[i].short_name, short_name)) {
            entry = &root_entry_scratch[i];
            serial_write("FAT32 read_dir: found entry: ");
            serial_write(root_entry_scratch[i].short_name);
            serial_write(" cluster=");
            serial_write_decimal(root_entry_scratch[i].first_cluster);
            serial_write(" size=");
            serial_write_decimal(root_entry_scratch[i].size);
            serial_write("\n");
            break;
        }
    }
    if (entry == 0) {
        serial_write("FAT32 read_dir: entry not found for ");
        serial_write(short_name);
        serial_write("\n");
        return FAT32_NOT_FOUND;
    }
    if ((entry->attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0) {
        return FAT32_INVALID;
    }
    if (entry->size > capacity) {
        return FAT32_LIMIT_EXCEEDED;
    }
    if (entry->size == 0) {
        return FAT32_OK;
    }
    if (entry->first_cluster < 2
        || entry->first_cluster >= volume->cluster_count + 2U) {
        serial_write("FAT32 read_dir: invalid first_cluster=");
        serial_write_decimal(entry->first_cluster);
        serial_write(" cluster_count=");
        serial_write_decimal(volume->cluster_count);
        serial_write("\n");
        return FAT32_INVALID;
    }

    remaining = entry->size;
    cluster = entry->first_cluster;
    for (uint32_t chain_step = 0; chain_step < volume->cluster_count;
         chain_step++) {
        for (uint32_t sector = 0;
             sector < volume->sectors_per_cluster;
             sector++) {
            uint64_t lba;
            if (!cluster_sector_lba(volume, cluster, sector, &lba)) {
                serial_write("FAT32 list_dir: cluster_sector_lba failed cluster=");
                serial_write_decimal(cluster);
                serial_write(" sector=");
                serial_write_decimal(sector);
                serial_write("\n");
                return FAT32_INVALID;
            }
            if (!block_device_read(volume->device_index, lba, 1,
                                   sector_buffer)) {
                serial_write("FAT32 list_dir: block_device_read failed lba=");
                serial_write_decimal(lba);
                serial_write("\n");
                return FAT32_IO_ERROR;
            }
            uint32_t bytes = remaining < volume->sector_size
                ? remaining : volume->sector_size;
            for (uint32_t i = 0; i < bytes; i++) {
                destination[*bytes_read + i] = sector_buffer[i];
            }
            *bytes_read += bytes;
            remaining -= bytes;
        }
        if (remaining == 0) {
            return FAT32_OK;
        }

        uint32_t next;
        bool end_of_chain;
        result = next_cluster(volume, cluster, &next, &end_of_chain);
        serial_write("FAT32 read_dir: next_cluster(cluster=");
        serial_write_decimal(cluster);
        serial_write(") next=");
        serial_write_decimal(next);
        serial_write(" end=");
        serial_write_decimal(end_of_chain ? 1 : 0);
        serial_write(" result=");
        serial_write_decimal(result);
        serial_write("\n");
        if (result != FAT32_OK) {
            return result;
        }
        if (end_of_chain) {
            return FAT32_INVALID;
        }
        cluster = next;
    }
    return FAT32_INVALID;
}
