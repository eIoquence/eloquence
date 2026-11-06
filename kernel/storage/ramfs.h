#ifndef ELOQUENCE_STORAGE_RAMFS_H
#define ELOQUENCE_STORAGE_RAMFS_H

#include <stdbool.h>
#include <stdint.h>

#include "vfs.h"

#define RAMFS_MAX_FILES 8U
#define RAMFS_NAME_MAX 12U
#define RAMFS_FILE_MAX 256U

struct ramfs_state {
    bool used[RAMFS_MAX_FILES];
    char names[RAMFS_MAX_FILES][RAMFS_NAME_MAX + 1];
    uint8_t data[RAMFS_MAX_FILES][RAMFS_FILE_MAX];
    uint32_t sizes[RAMFS_MAX_FILES];
};

bool ramfs_vfs_list_directory(void *context, uint32_t directory,
                              struct vfs_entry *entries, uint32_t capacity,
                              uint32_t *entry_count);
bool ramfs_vfs_read_file(void *context, uint32_t directory,
                         const char *name, void *buffer, uint32_t capacity,
                         uint32_t *bytes_read);
bool ramfs_vfs_create_file(void *context, uint32_t directory,
                           const char *name);
bool ramfs_vfs_delete_file(void *context, uint32_t directory,
                           const char *name);
bool ramfs_vfs_write_file(void *context, uint32_t directory,
                          const char *name, const void *buffer,
                          uint32_t bytes);
bool ramfs_vfs_create_directory(void *context, uint32_t parent,
                                const char *name);
bool ramfs_vfs_find_directory(void *context, uint32_t parent,
                              const char *name, uint32_t *directory);

#endif
