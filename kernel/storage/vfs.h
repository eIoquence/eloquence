#ifndef ELOQUENCE_STORAGE_VFS_H
#define ELOQUENCE_STORAGE_VFS_H

#include <stdbool.h>
#include <stdint.h>

#define VFS_NAME_CAPACITY 13U
#define VFS_MAX_DIRECTORY_ENTRIES 128U

struct vfs_entry {
    char name[VFS_NAME_CAPACITY];
    uint64_t size;
    bool directory;
};

struct vfs_operations {
    bool (*list_root)(void *context, struct vfs_entry *entries,
                      uint32_t capacity, uint32_t *entry_count);
    bool (*read_root_file)(void *context, const char *name, void *buffer,
                           uint32_t capacity, uint32_t *bytes_read);
};

bool vfs_mount_root(const struct vfs_operations *operations, void *context);
bool vfs_root_mounted(void);
bool vfs_list_root(struct vfs_entry *entries, uint32_t capacity,
                   uint32_t *entry_count);
bool vfs_read_file(const char *path, void *buffer, uint32_t capacity,
                   uint32_t *bytes_read);

#endif
