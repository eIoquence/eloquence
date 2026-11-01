#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "block_device.h"

#define BLOCK_DEVICE_CAPACITY 16

static struct block_device devices[BLOCK_DEVICE_CAPACITY];
static uint64_t device_count;

static bool bounded_string_valid(const char *text, size_t capacity) {
    if (text == 0) {
        return false;
    }
    for (size_t i = 0; i < capacity; i++) {
        if (text[i] == '\0') {
            return i != 0;
        }
    }
    return false;
}

static bool string_terminated(const char *text, size_t capacity) {
    if (text == 0) {
        return false;
    }
    for (size_t i = 0; i < capacity; i++) {
        if (text[i] == '\0') {
            return true;
        }
    }
    return false;
}

static bool same_name(const char *left, const char *right) {
    for (size_t i = 0; i < BLOCK_DEVICE_NAME_LENGTH; i++) {
        if (left[i] != right[i]) {
            return false;
        }
        if (left[i] == '\0') {
            return true;
        }
    }
    return true;
}

bool block_device_register(const struct block_device *device) {
    uint64_t total_bytes;

    if (device == 0
        || !bounded_string_valid(device->name, sizeof(device->name))
        || !string_terminated(device->model, sizeof(device->model))
        || device->sector_size == 0 || device->sector_count == 0
        || device->read == 0 || (!device->read_only && device->write == 0)
        || (device->sector_count > UINT64_MAX / device->sector_size)) {
        return false;
    }
    total_bytes = device->sector_count * device->sector_size;
    if (total_bytes < device->sector_size) {
        return false;
    }

    for (uint64_t i = 0; i < device_count; i++) {
        if (same_name(devices[i].name, device->name)) {
            return false;
        }
    }
    if (device_count == BLOCK_DEVICE_CAPACITY) {
        return false;
    }

    devices[device_count++] = *device;
    return true;
}

uint64_t block_device_count(void) {
    return device_count;
}

const struct block_device *block_device_get(uint64_t index) {
    return index < device_count ? &devices[index] : 0;
}

static bool validate_request(const struct block_device *device, uint64_t lba,
                             uint32_t count, const void *buffer) {
    uint64_t bytes;

    if (device == 0 || buffer == 0 || count == 0
        || lba >= device->sector_count
        || (uint64_t)count > device->sector_count - lba
        || count > BLOCK_DEVICE_MAX_TRANSFER_BYTES / device->sector_size) {
        return false;
    }
    bytes = (uint64_t)count * device->sector_size;
    return bytes <= BLOCK_DEVICE_MAX_TRANSFER_BYTES;
}

bool block_device_read(uint64_t index, uint64_t lba, uint32_t count,
                       void *buffer) {
    const struct block_device *device = block_device_get(index);

    return validate_request(device, lba, count, buffer)
        && device->read(device->context, lba, count, buffer);
}

bool block_device_write(uint64_t index, uint64_t lba, uint32_t count,
                        const void *buffer) {
    const struct block_device *device = block_device_get(index);

    return validate_request(device, lba, count, buffer)
        && !device->read_only && device->write != 0
        && device->write(device->context, lba, count, buffer);
}

bool block_device_flush(uint64_t index) {
    const struct block_device *device = block_device_get(index);

    return device != 0 && !device->read_only && device->flush != 0
        && device->flush(device->context);
}
