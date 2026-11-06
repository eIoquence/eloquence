#include <stdbool.h>
#include <stdint.h>

#include "ramfs.h"

static bool ramfs_equals(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void ramfs_store_text(char *destination, const char *source,
                             uint32_t capacity) {
    uint32_t index = 0;
    while (source[index] != '\0' && index + 1 < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void ramfs_copy_bytes(void *destination, const void *source,
                             uint32_t count) {
    const uint8_t *source_bytes = source;
    uint8_t *destination_bytes = destination;
    for (uint32_t i = 0; i < count; i++) {
        destination_bytes[i] = source_bytes[i];
    }
}

static int32_t ramfs_find_name(struct ramfs_state *state, const char *name) {
    if (name == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (state->used[i] && ramfs_equals(state->names[i], name)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool ramfs_name_valid(const char *name) {
    uint32_t length = 0;
    if (name == 0) {
        return false;
    }
    while (name[length] != '\0') {
        if (length >= RAMFS_NAME_MAX) {
            return false;
        }
        length++;
    }
    return length > 0;
}

static int32_t ramfs_alloc_slot(struct ramfs_state *state) {
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!state->used[i]) {
            return (int32_t)i;
        }
    }
    return -1;
}

bool ramfs_vfs_list_directory(void *context, uint32_t directory,
                              struct vfs_entry *entries, uint32_t capacity,
                              uint32_t *entry_count) {
    struct ramfs_state *state = context;
    uint32_t count = 0;
    if (directory != 0 || state == 0 || entries == 0 || capacity == 0
        || entry_count == 0) {
        return false;
    }
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!state->used[i]) {
            continue;
        }
        if (count >= capacity) {
            break;
        }
        ramfs_store_text(entries[count].name, state->names[i],
                         VFS_NAME_CAPACITY);
        entries[count].size = state->sizes[i];
        entries[count].directory = false;
        count++;
    }
    *entry_count = count;
    return true;
}

bool ramfs_vfs_read_file(void *context, uint32_t directory, const char *name,
                         void *buffer, uint32_t capacity,
                         uint32_t *bytes_read) {
    struct ramfs_state *state = context;
    int32_t index = state != 0 ? ramfs_find_name(state, name) : -1;
    uint32_t size;
    if (directory != 0 || index < 0 || buffer == 0 || bytes_read == 0) {
        return false;
    }
    size = state->sizes[index];
    if (size > capacity) {
        size = capacity;
    }
    ramfs_copy_bytes(buffer, state->data[index], size);
    *bytes_read = size;
    return true;
}

static bool ramfs_insert(struct ramfs_state *state, const char *name) {
    int32_t index = ramfs_alloc_slot(state);
    if (index < 0) {
        return false;
    }
    state->used[index] = true;
    ramfs_store_text(state->names[index], name, RAMFS_NAME_MAX + 1);
    state->sizes[index] = 0;
    return true;
}

bool ramfs_vfs_create_file(void *context, uint32_t directory, const char *name) {
    struct ramfs_state *state = context;
    if (directory != 0 || !ramfs_name_valid(name) || state == 0) {
        return false;
    }
    if (ramfs_find_name(state, name) >= 0) {
        return true;
    }
    return ramfs_insert(state, name);
}

bool ramfs_vfs_delete_file(void *context, uint32_t directory, const char *name) {
    struct ramfs_state *state = context;
    int32_t index = state != 0 ? ramfs_find_name(state, name) : -1;
    if (directory != 0 || index < 0) {
        return false;
    }
    state->used[index] = false;
    state->sizes[index] = 0;
    return true;
}

bool ramfs_vfs_write_file(void *context, uint32_t directory, const char *name,
                          const void *buffer, uint32_t bytes) {
    struct ramfs_state *state = context;
    int32_t index = state != 0 ? ramfs_find_name(state, name) : -1;
    if (directory != 0 || index < 0) {
        return false;
    }
    if (bytes > RAMFS_FILE_MAX) {
        bytes = RAMFS_FILE_MAX;
    }
    if (bytes != 0) {
        ramfs_copy_bytes(state->data[index], buffer, bytes);
    }
    state->sizes[index] = bytes;
    return true;
}

bool ramfs_vfs_create_directory(void *context, uint32_t parent,
                                const char *name) {
    (void)parent;
    return ramfs_vfs_create_file(context, parent, name);
}

bool ramfs_vfs_find_directory(void *context, uint32_t parent,
                              const char *name, uint32_t *directory) {
    (void)parent;
    struct ramfs_state *state = context;
    if (state == 0 || name == 0 || directory == 0) {
        return false;
    }
    int32_t index = ramfs_find_name(state, name);
    if (index < 0) {
        return false;
    }
    *directory = (uint32_t)index;
    return true;
}
