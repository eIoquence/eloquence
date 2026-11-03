#ifndef ELOQUENCE_STORAGE_FAT32_H
#define ELOQUENCE_STORAGE_FAT32_H

#include <stdint.h>

#include "gpt.h"

#define FAT32_MAX_DIRECTORY_ENTRIES 128U
#define FAT32_SHORT_NAME_LENGTH 13U

struct fat32_volume {
    uint64_t device_index;
    uint64_t partition_start_lba;
    uint64_t first_data_lba;
    uint64_t fat_start_lba;
    uint32_t sector_size;
    uint32_t sectors_per_cluster;
    uint32_t fat_sector_count;
    uint32_t cluster_count;
    uint32_t root_cluster;
};

struct fat32_directory_entry {
    char short_name[FAT32_SHORT_NAME_LENGTH];
    uint32_t first_cluster;
    uint32_t size;
    uint8_t attributes;
};

enum fat32_result {
    FAT32_OK,
    FAT32_NOT_FAT32,
    FAT32_IO_ERROR,
    FAT32_INVALID,
    FAT32_LIMIT_EXCEEDED,
    FAT32_NOT_FOUND
};

enum fat32_result fat32_mount(uint64_t device_index,
                              const struct gpt_partition *partition,
                              struct fat32_volume *volume);
enum fat32_result fat32_list_root(
    const struct fat32_volume *volume,
    struct fat32_directory_entry *entries, uint32_t capacity,
    uint32_t *entry_count);
enum fat32_result fat32_read_root_file(
    const struct fat32_volume *volume, const char *short_name,
    void *buffer, uint32_t capacity, uint32_t *bytes_read);

#endif
