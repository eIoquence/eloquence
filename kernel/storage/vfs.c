#include <stddef.h>

#include "vfs.h"

static struct vfs_operations root_operations;
static void *root_context;
static bool root_is_mounted;

bool vfs_mount_root(const struct vfs_operations *operations, void *context) {
    if (root_is_mounted || operations == 0 || context == 0
        || operations->list_root == 0 || operations->read_root_file == 0) {
        return false;
    }
    root_operations = *operations;
    root_context = context;
    root_is_mounted = true;
    return true;
}

bool vfs_root_mounted(void) {
    return root_is_mounted;
}

bool vfs_list_root(struct vfs_entry *entries, uint32_t capacity,
                   uint32_t *entry_count) {
    if (!root_is_mounted || entries == 0 || capacity == 0
        || entry_count == 0) {
        return false;
    }
    return root_operations.list_root(root_context, entries, capacity,
                                     entry_count);
}

bool vfs_read_file(const char *path, void *buffer, uint32_t capacity,
                   uint32_t *bytes_read) {
    size_t length = 0;

    if (!root_is_mounted || path == 0 || buffer == 0 || capacity == 0
        || bytes_read == 0 || path[0] != '/') {
        return false;
    }
    while (path[length] != '\0' && length < VFS_NAME_CAPACITY) {
        if (path[length] == '/' && length != 0) {
            return false;
        }
        length++;
    }
    if (path[length] != '\0' || length < 2) {
        return false;
    }
    return root_operations.read_root_file(root_context, path + 1, buffer,
                                          capacity, bytes_read);
}
