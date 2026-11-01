#ifndef ELOQUENCE_STORAGE_BLOCK_DEVICE_H
#define ELOQUENCE_STORAGE_BLOCK_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#define BLOCK_DEVICE_NAME_LENGTH 16
#define BLOCK_DEVICE_MODEL_LENGTH 41
#define BLOCK_DEVICE_MAX_TRANSFER_BYTES (128U * 1024U)

struct block_device {
    char name[BLOCK_DEVICE_NAME_LENGTH];
    char model[BLOCK_DEVICE_MODEL_LENGTH];
    uint32_t sector_size;
    uint64_t sector_count;
    bool read_only;
    void *context;
    bool (*read)(void *context, uint64_t lba, uint32_t count, void *buffer);
    bool (*write)(void *context, uint64_t lba, uint32_t count,
                  const void *buffer);
    bool (*flush)(void *context);
};

bool block_device_register(const struct block_device *device);
uint64_t block_device_count(void);
const struct block_device *block_device_get(uint64_t index);
bool block_device_read(uint64_t index, uint64_t lba, uint32_t count,
                       void *buffer);
/* A successful write may still be in volatile device cache until flush succeeds. */
bool block_device_write(uint64_t index, uint64_t lba, uint32_t count,
                        const void *buffer);
bool block_device_flush(uint64_t index);

#endif
