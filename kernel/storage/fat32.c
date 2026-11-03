#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "block_device.h"
#include "fat32.h"

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

static uint16_t read_u16_le(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
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
    volume->fat_start_lba = partition->first_lba + reserved_sectors
        + (uint64_t)active_fat * fat_size_32;
    volume->sector_size = device->sector_size;
    volume->sectors_per_cluster = sectors_per_cluster;
    volume->fat_sector_count = fat_size_32;
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
    if (device == 0
        || volume->fat_start_lba > UINT64_MAX - fat_sector_offset
        || !block_device_read(volume->device_index,
                              volume->fat_start_lba + fat_sector_offset,
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

enum fat32_result fat32_read_root_file(
    const struct fat32_volume *volume, const char *short_name,
    void *buffer, uint32_t capacity, uint32_t *bytes_read) {
    uint32_t entry_count = 0;
    uint32_t remaining;
    uint32_t cluster;
    uint8_t *destination = buffer;
    struct fat32_directory_entry *entry = 0;

    if (!valid_volume(volume) || short_name == 0 || buffer == 0
        || bytes_read == 0) {
        return FAT32_INVALID;
    }
    *bytes_read = 0;
    enum fat32_result result = list_directory(
        volume, volume->root_cluster, root_entry_scratch,
        FAT32_MAX_DIRECTORY_ENTRIES, &entry_count);
    if (result != FAT32_OK) {
        return result;
    }
    for (uint32_t i = 0; i < entry_count; i++) {
        if (short_name_equals(root_entry_scratch[i].short_name, short_name)) {
            entry = &root_entry_scratch[i];
            break;
        }
    }
    if (entry == 0) {
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
        return FAT32_INVALID;
    }

    remaining = entry->size;
    cluster = entry->first_cluster;
    for (uint32_t chain_step = 0; chain_step < volume->cluster_count;
         chain_step++) {
        for (uint32_t sector = 0;
             sector < volume->sectors_per_cluster && remaining != 0;
             sector++) {
            uint64_t lba;
            if (!cluster_sector_lba(volume, cluster, sector, &lba)) {
                return FAT32_INVALID;
            }
            if (!block_device_read(volume->device_index, lba, 1,
                                   sector_buffer)) {
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
