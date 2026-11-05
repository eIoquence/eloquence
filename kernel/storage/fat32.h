#ifndef ELOQUENCE_STORAGE_FAT32_H
#define ELOQUENCE_STORAGE_FAT32_H

#include <stdint.h>
#include <stdbool.h>

#include "gpt.h"

#define FAT32_MAX_DIRECTORY_ENTRIES 128U
#define FAT32_SHORT_NAME_LENGTH 13U
#define FAT32_MAX_FILE_WRITE_BYTES 16384U

struct fat32_volume {
    uint64_t device_index;
    uint64_t partition_start_lba;
    uint64_t first_data_lba;
    uint64_t fat_start_lba;
    uint32_t sector_size;
    uint32_t sectors_per_cluster;
    uint32_t fat_sector_count;
    uint32_t fat_count;
    uint32_t active_fat_index;
    bool fat_mirroring;
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
enum fat32_result fat32_list_directory(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    struct fat32_directory_entry *entries, uint32_t capacity,
    uint32_t *entry_count);
enum fat32_result fat32_find_directory(
    const struct fat32_volume *volume, uint32_t parent_cluster,
    const char *short_name, uint32_t *directory_cluster);
enum fat32_result fat32_parent_directory(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    uint32_t *parent_cluster);
enum fat32_result fat32_read_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name, void *buffer, uint32_t capacity,
    uint32_t *bytes_read);
enum fat32_result fat32_write_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name, const void *buffer, uint32_t bytes);
enum fat32_result fat32_create_directory(
    const struct fat32_volume *volume, uint32_t parent_cluster,
    const char *short_name);
enum fat32_result fat32_delete_directory_file(
    const struct fat32_volume *volume, uint32_t directory_cluster,
    const char *short_name);
enum fat32_result fat32_read_root_file(
    const struct fat32_volume *volume, const char *short_name,
    void *buffer, uint32_t capacity, uint32_t *bytes_read);
enum fat32_result fat32_create_root_file(
    const struct fat32_volume *volume, const char *short_name);
enum fat32_result fat32_delete_root_file(
    const struct fat32_volume *volume, const char *short_name);
enum fat32_result fat32_write_root_file(
    const struct fat32_volume *volume, const char *short_name,
    const void *buffer, uint32_t bytes);
enum fat32_result fat32_create_root_directory(
    const struct fat32_volume *volume, const char *short_name);

#endif
