#include <stddef.h>

#include "vfs.h"

struct vfs_mount {
    char path[VFS_MOUNT_PATH_MAX];
    struct vfs_operations operations;
    void *context;
    bool active;
};

static struct vfs_mount mounts[VFS_MOUNT_COUNT_MAX];

static bool vfs_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static uint32_t vfs_length(const char *text) {
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static void vfs_store_text(char *destination, const char *source,
                           uint32_t capacity) {
    uint32_t index = 0;
    while (source[index] != '\0' && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static const char *resolve_root_path(const char *path) {
    uint32_t length = 0;
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

/* Reads the next path component from *cursor and copies it into buffer.
 * Advances *cursor past the component and any following slash separators.
 * Returns false for empty components or names longer than the buffer. */
static bool vfs_next_component(const char **cursor, char *buffer) {
    const char *component = *cursor;
    const char *search = component;
    uint32_t length;
    while (*search != '\0' && *search != '/') {
        search++;
    }
    length = (uint32_t)(uintptr_t)(search - component);
    if (length == 0 || length >= VFS_NAME_CAPACITY) {
        return false;
    }
    for (uint32_t i = 0; i < length; i++) {
        buffer[i] = component[i];
    }
    buffer[length] = '\0';
    while (*search == '/') {
        search++;
    }
    *cursor = search;
    return true;
}

/* Resolves a full path to the mount that owns it; *rest gets the path
 * portion below the mount point. */
static struct vfs_mount *vfs_resolve(const char *path, const char **rest) {
    struct vfs_mount *chosen = 0;
    uint32_t best_length = 0;
    if (path == 0 || path[0] != '/') {
        return 0;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_COUNT_MAX; i++) {
        const char *candidate_rest = 0;
        uint32_t length = 0;
        if (!mounts[i].active) {
            continue;
        }
        while (mounts[i].path[length] != '\0'
               && mounts[i].path[length] == path[length]) {
            length++;
        }
        if (mounts[i].path[length] != '\0') {
            continue;
        }
        if (mounts[i].path[1] == '\0') {
            candidate_rest = path + 1;
        } else if (path[length] == '\0') {
            candidate_rest = "";
        } else if (path[length] == '/') {
            candidate_rest = path + length + 1;
        } else {
            continue;
        }
        if (chosen == 0 || length > best_length) {
            chosen = &mounts[i];
            best_length = length;
            *rest = candidate_rest;
        }
    }
    return chosen;
}

/* Walks every component under *rest as a directory, leaving the final
 * parent directory id in *directory. Used to list directories by path. */
static bool vfs_walk_directories(struct vfs_mount *mount, const char *rest,
                                 uint32_t *directory) {
    uint32_t parent = 0;
    const char *cursor = rest;
    char component[VFS_NAME_CAPACITY];
    while (*cursor != '\0') {
        if (!vfs_next_component(&cursor, component)
            || !mount->operations.find_directory(mount->context, parent,
                                                 component, &parent)) {
            return false;
        }
    }
    *directory = parent;
    return true;
}

/* Walks all but the last component under *rest as directories; the last
 * component is returned through *name and its parent in *directory. */
static bool vfs_walk_to_file(struct vfs_mount *mount, const char *rest,
                             uint32_t *directory, const char **name) {
    uint32_t parent = 0;
    const char *cursor = rest;
    char component[VFS_NAME_CAPACITY];
    const char *candidate = 0;
    while (*cursor != '\0') {
        candidate = cursor;
        if (!vfs_next_component(&cursor, component)) {
            return false;
        }
        if (*cursor != '\0') {
            if (!mount->operations.find_directory(mount->context, parent,
                                                  component, &parent)) {
                return false;
            }
        }
    }
    if (candidate == 0) {
        return false;
    }
    *directory = parent;
    *name = candidate;
    return true;
}

bool vfs_mount_root(const struct vfs_operations *operations, void *context) {
    if (operations == 0 || context == 0 || operations->list_directory == 0
        || operations->read_file == 0 || operations->create_file == 0
        || operations->delete_file == 0 || operations->write_file == 0
        || operations->create_directory == 0
        || operations->find_directory == 0) {
        return false;
    }
    mounts[0].operations = *operations;
    mounts[0].context = context;
    mounts[0].active = true;
    mounts[0].path[0] = '/';
    mounts[0].path[1] = '\0';
    return true;
}

bool vfs_mount(const char *path, const struct vfs_operations *operations,
               void *context) {
    if (path == 0 || path[0] != '/' || path[1] == '\0' || operations == 0
        || context == 0 || operations->list_directory == 0
        || operations->read_file == 0 || operations->create_file == 0
        || operations->delete_file == 0 || operations->write_file == 0
        || operations->create_directory == 0
        || operations->find_directory == 0) {
        return false;
    }
    if (vfs_length(path) >= VFS_MOUNT_PATH_MAX) {
        return false;
    }
    for (uint32_t i = 0; i < VFS_MOUNT_COUNT_MAX; i++) {
        if (mounts[i].active && vfs_equal(mounts[i].path, path)) {
            return false;
        }
    }
    for (uint32_t i = 1; i < VFS_MOUNT_COUNT_MAX; i++) {
        if (!mounts[i].active) {
            vfs_store_text(mounts[i].path, path, VFS_MOUNT_PATH_MAX);
            mounts[i].operations = *operations;
            mounts[i].context = context;
            mounts[i].active = true;
            return true;
        }
    }
    return false;
}

uint32_t vfs_mount_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < VFS_MOUNT_COUNT_MAX; i++) {
        if (mounts[i].active) {
            count++;
        }
    }
    return count;
}

bool vfs_mount_path(uint32_t index, char *buffer, uint32_t capacity) {
    if (index >= VFS_MOUNT_COUNT_MAX || !mounts[index].active || buffer == 0
        || capacity == 0) {
        return false;
    }
    vfs_store_text(buffer, mounts[index].path, capacity);
    return true;
}

bool vfs_root_mounted(void) {
    return mounts[0].active;
}

bool vfs_list_root(struct vfs_entry *entries, uint32_t capacity,
                   uint32_t *entry_count) {
    return vfs_list_directory(0, entries, capacity, entry_count);
}

bool vfs_list_directory(uint32_t directory, struct vfs_entry *entries,
                        uint32_t capacity, uint32_t *entry_count) {
    if (!mounts[0].active || entries == 0 || capacity == 0
        || entry_count == 0) {
        return false;
    }
    return mounts[0].operations.list_directory(mounts[0].context, directory,
                                               entries, capacity,
                                               entry_count);
}

bool vfs_list_path(const char *path, struct vfs_entry *entries,
                   uint32_t capacity, uint32_t *entry_count) {
    const char *rest = 0;
    struct vfs_mount *mount = vfs_resolve(path, &rest);
    if (mount == 0 || entries == 0 || capacity == 0 || entry_count == 0) {
        return false;
    }
    if (rest[0] == '\0') {
        return mount->operations.list_directory(mount->context, 0, entries,
                                                capacity, entry_count);
    }
    uint32_t directory = 0;
    if (!vfs_walk_directories(mount, rest, &directory)) {
        return false;
    }
    return mount->operations.list_directory(mount->context, directory,
                                            entries, capacity, entry_count);
}

bool vfs_read_file(const char *path, void *buffer, uint32_t capacity,
                   uint32_t *bytes_read) {
    const char *name = resolve_root_path(path);
    if (!mounts[0].active || name == 0 || buffer == 0 || capacity == 0
        || bytes_read == 0) {
        return false;
    }
    return mounts[0].operations.read_file(mounts[0].context, 0, name, buffer,
                                          capacity, bytes_read);
}

bool vfs_read_path(const char *path, void *buffer, uint32_t capacity,
                   uint32_t *bytes_read) {
    const char *rest = 0;
    struct vfs_mount *mount = vfs_resolve(path, &rest);
    if (mount == 0 || buffer == 0 || capacity == 0 || bytes_read == 0
        || rest[0] == '\0') {
        return false;
    }
    uint32_t directory = 0;
    const char *name = 0;
    if (!vfs_walk_to_file(mount, rest, &directory, &name)) {
        return false;
    }
    return mount->operations.read_file(mount->context, directory, name,
                                       buffer, capacity, bytes_read);
}

bool vfs_read_file_in(uint32_t directory, const char *name, void *buffer,
                      uint32_t capacity, uint32_t *bytes_read) {
    if (!mounts[0].active || name == 0 || buffer == 0 || capacity == 0
        || bytes_read == 0) {
        return false;
    }
    return mounts[0].operations.read_file(mounts[0].context, directory, name,
                                          buffer, capacity, bytes_read);
}

bool vfs_create_file(const char *path) {
    const char *name = resolve_root_path(path);
    if (!mounts[0].active || name == 0) {
        return false;
    }
    return mounts[0].operations.create_file(mounts[0].context, 0, name);
}

bool vfs_create_path(const char *path) {
    const char *rest = 0;
    struct vfs_mount *mount = vfs_resolve(path, &rest);
    if (mount == 0 || rest[0] == '\0') {
        return false;
    }
    uint32_t directory = 0;
    const char *name = 0;
    if (!vfs_walk_to_file(mount, rest, &directory, &name)) {
        return false;
    }
    return mount->operations.create_file(mount->context, directory, name);
}

bool vfs_create_file_in(uint32_t directory, const char *name) {
    return mounts[0].active && name != 0
        && mounts[0].operations.create_file(mounts[0].context, directory,
                                            name);
}

bool vfs_delete_file(const char *path) {
    const char *name = resolve_root_path(path);
    if (!mounts[0].active || name == 0) {
        return false;
    }
    return mounts[0].operations.delete_file(mounts[0].context, 0, name);
}

bool vfs_delete_path(const char *path) {
    const char *rest = 0;
    struct vfs_mount *mount = vfs_resolve(path, &rest);
    if (mount == 0 || rest[0] == '\0') {
        return false;
    }
    uint32_t directory = 0;
    const char *name = 0;
    if (!vfs_walk_to_file(mount, rest, &directory, &name)) {
        return false;
    }
    return mount->operations.delete_file(mount->context, directory, name);
}

bool vfs_delete_file_in(uint32_t directory, const char *name) {
    return mounts[0].active && name != 0
        && mounts[0].operations.delete_file(mounts[0].context, directory,
                                            name);
}

bool vfs_write_file(const char *path, const void *buffer, uint32_t bytes) {
    const char *name = resolve_root_path(path);
    if (!mounts[0].active || name == 0 || (buffer == 0 && bytes != 0)) {
        return false;
    }
    return mounts[0].operations.write_file(mounts[0].context, 0, name,
                                           buffer, bytes);
}

bool vfs_write_path(const char *path, const void *buffer, uint32_t bytes) {
    const char *rest = 0;
    struct vfs_mount *mount = vfs_resolve(path, &rest);
    if (mount == 0 || rest[0] == '\0' || (buffer == 0 && bytes != 0)) {
        return false;
    }
    uint32_t directory = 0;
    const char *name = 0;
    if (!vfs_walk_to_file(mount, rest, &directory, &name)) {
        return false;
    }
    return mount->operations.write_file(mount->context, directory, name,
                                        buffer, bytes);
}

bool vfs_write_file_in(uint32_t directory, const char *name,
                       const void *buffer, uint32_t bytes) {
    return mounts[0].active && name != 0 && (buffer != 0 || bytes == 0)
        && mounts[0].operations.write_file(mounts[0].context, directory,
                                           name, buffer, bytes);
}

bool vfs_create_directory(const char *path) {
    const char *name = resolve_root_path(path);
    if (!mounts[0].active || name == 0) {
        return false;
    }
    return mounts[0].operations.create_directory(mounts[0].context, 0, name);
}

bool vfs_create_directory_in(uint32_t parent, const char *name) {
    return mounts[0].active && name != 0
        && mounts[0].operations.create_directory(mounts[0].context, parent,
                                                 name);
}

bool vfs_find_directory(uint32_t parent, const char *name,
                        uint32_t *directory) {
    return mounts[0].active && name != 0 && directory != 0
        && mounts[0].operations.find_directory(mounts[0].context, parent,
                                               name, directory);
}
