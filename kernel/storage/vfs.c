#include <stddef.h>

#include "vfs.h"

static struct vfs_operations root_operations;
static void *root_context;
static bool root_is_mounted;

bool vfs_mount_root(const struct vfs_operations *operations, void *context) {
    if (root_is_mounted || operations == 0 || context == 0
        || operations->list_directory == 0 || operations->read_file == 0
        || operations->create_file == 0 || operations->delete_file == 0
        || operations->write_file == 0 || operations->create_directory == 0
        || operations->find_directory == 0) {
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
    return vfs_list_directory(0, entries, capacity, entry_count);
}

bool vfs_list_directory(uint32_t directory, struct vfs_entry *entries,
                        uint32_t capacity, uint32_t *entry_count) {
    if (!root_is_mounted || entries == 0 || capacity == 0
        || entry_count == 0) {
        return false;
    }
    return root_operations.list_directory(root_context, directory, entries,
                                          capacity, entry_count);
}

bool vfs_read_file(const char *path, void *buffer, uint32_t capacity,
                   uint32_t *bytes_read) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0 || buffer == 0 || capacity == 0
        || bytes_read == 0) {
        return false;
    }
    return root_operations.read_file(root_context, 0, name, buffer, capacity,
                                     bytes_read);
}

bool vfs_read_file_in(uint32_t directory, const char *name, void *buffer,
                      uint32_t capacity, uint32_t *bytes_read) {
    if (!root_is_mounted || name == 0 || buffer == 0 || capacity == 0
        || bytes_read == 0) {
        return false;
    }
    return root_operations.read_file(root_context, directory, name, buffer,
                                     capacity, bytes_read);
}

bool vfs_create_file(const char *path) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0) {
        return false;
    }
    return root_operations.create_file(root_context, 0, name);
}

bool vfs_create_file_in(uint32_t directory, const char *name) {
    return root_is_mounted && name != 0
        && root_operations.create_file(root_context, directory, name);
}

bool vfs_delete_file(const char *path) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0) {
        return false;
    }
    return root_operations.delete_file(root_context, 0, name);
}

bool vfs_delete_file_in(uint32_t directory, const char *name) {
    return root_is_mounted && name != 0
        && root_operations.delete_file(root_context, directory, name);
}

bool vfs_write_file(const char *path, const void *buffer, uint32_t bytes) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0 || (buffer == 0 && bytes != 0)) {
        return false;
    }
    return root_operations.write_file(root_context, 0, name, buffer, bytes);
}

bool vfs_write_file_in(uint32_t directory, const char *name,
                       const void *buffer, uint32_t bytes) {
    return root_is_mounted && name != 0 && (buffer != 0 || bytes == 0)
        && root_operations.write_file(root_context, directory, name, buffer,
                                      bytes);
}

bool vfs_create_directory(const char *path) {
    const char *name = resolve_root_path(path);
    if (!root_is_mounted || name == 0) {
        return false;
    }
    return root_operations.create_directory(root_context, 0, name);
}

bool vfs_create_directory_in(uint32_t parent, const char *name) {
    return root_is_mounted && name != 0
        && root_operations.create_directory(root_context, parent, name);
}

bool vfs_find_directory(uint32_t parent, const char *name,
                        uint32_t *directory) {
    return root_is_mounted && name != 0 && directory != 0
        && root_operations.find_directory(root_context, parent, name,
                                          directory);
}
