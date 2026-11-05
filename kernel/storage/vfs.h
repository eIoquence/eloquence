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
    bool (*list_directory)(void *context, uint32_t directory,
                      struct vfs_entry *entries,
                      uint32_t capacity, uint32_t *entry_count);
    bool (*read_file)(void *context, uint32_t directory, const char *name,
                           void *buffer,
                           uint32_t capacity, uint32_t *bytes_read);
    bool (*create_file)(void *context, uint32_t directory, const char *name);
    bool (*delete_file)(void *context, uint32_t directory, const char *name);
    bool (*write_file)(void *context, uint32_t directory, const char *name,
                            const void *buffer, uint32_t bytes);
    bool (*create_directory)(void *context, uint32_t parent,
                             const char *name);
    bool (*find_directory)(void *context, uint32_t parent, const char *name,
                           uint32_t *directory);
};

bool vfs_mount_root(const struct vfs_operations *operations, void *context);
bool vfs_root_mounted(void);
bool vfs_list_root(struct vfs_entry *entries, uint32_t capacity,
                   uint32_t *entry_count);
bool vfs_list_directory(uint32_t directory, struct vfs_entry *entries,
                        uint32_t capacity, uint32_t *entry_count);
bool vfs_read_file(const char *path, void *buffer, uint32_t capacity,
                   uint32_t *bytes_read);
bool vfs_create_file(const char *path);
bool vfs_delete_file(const char *path);
bool vfs_write_file(const char *path, const void *buffer, uint32_t bytes);
bool vfs_create_directory(const char *path);
bool vfs_create_directory_in(uint32_t parent, const char *name);
bool vfs_find_directory(uint32_t parent, const char *name,
                        uint32_t *directory);
bool vfs_read_file_in(uint32_t directory, const char *name, void *buffer,
                      uint32_t capacity, uint32_t *bytes_read);
bool vfs_create_file_in(uint32_t directory, const char *name);
bool vfs_delete_file_in(uint32_t directory, const char *name);
bool vfs_write_file_in(uint32_t directory, const char *name,
                       const void *buffer, uint32_t bytes);

#endif
