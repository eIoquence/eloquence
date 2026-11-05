#include <stddef.h>

#include "vfs.h"

static struct vfs_operations root_operations;
static void *root_context;
static bool root_is_mounted;

bool vfs_mount_root(const struct vfs_operations *operations, void *context) {
    if (root_is_mounted || operations == 0 || context == 0
        || operations->list_root == 0 || operations->read_root_file == 0
        || operations->create_root_file == 0
        || operations->delete_root_file == 0
        || operations->write_root_file == 0
        || operations->create_root_directory == 0) {
        return false;
    }
    root_operations = *operations;
    root_context = context;
    root_is_mounted = true;
    return true;
}

static const char *resolve_root_path(const char *path) {
    size_t length = 0;
    if (path == 0 || path[0] != '/') {
        return 0;
    }
    while (path[length] != '\0' && length < VFS_NAME_CAPACITY) {
        if (path[length] == '/' && length != 0) {
            return 0;
        }
        length++;
    }
    return path[length] == '\0' && length >= 2 ? path + 1 : 0;
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
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0 || buffer == 0 || capacity == 0
        || bytes_read == 0) {
        return false;
    }
    return root_operations.read_root_file(root_context, name, buffer,
                                          capacity, bytes_read);
}

bool vfs_create_file(const char *path) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0) {
        return false;
    }
    return root_operations.create_root_file(root_context, name);
}

bool vfs_delete_file(const char *path) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0) {
        return false;
    }
    return root_operations.delete_root_file(root_context, name);
}

bool vfs_write_file(const char *path, const void *buffer, uint32_t bytes) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0 || (buffer == 0 && bytes != 0)) {
        return false;
    }
    return root_operations.write_root_file(root_context, name, buffer, bytes);
}

bool vfs_create_directory(const char *path) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0) {
        return false;
    }
    return root_operations.create_root_directory(root_context, name);
}
