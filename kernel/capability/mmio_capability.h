#ifndef ELOQUENCE_MMIO_CAPABILITY_H
#define ELOQUENCE_MMIO_CAPABILITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/mmio.h"

enum mmio_capability_rights {
    MMIO_CAPABILITY_READ32 = 1U << 0,
    MMIO_CAPABILITY_WRITE32 = 1U << 1
};

enum mmio_capability_operation {
    MMIO_CAPABILITY_OP_GRANT,
    MMIO_CAPABILITY_OP_READ32,
    MMIO_CAPABILITY_OP_WRITE32,
    MMIO_CAPABILITY_OP_REVOKE
};

enum mmio_capability_result {
    MMIO_CAPABILITY_OK,
    MMIO_CAPABILITY_INVALID_ARGUMENT,
    MMIO_CAPABILITY_TABLE_FULL,
    MMIO_CAPABILITY_INVALID_HANDLE,
    MMIO_CAPABILITY_STALE_HANDLE,
    MMIO_CAPABILITY_ACCESS_DENIED,
    MMIO_CAPABILITY_OUT_OF_RANGE,
    MMIO_CAPABILITY_MISALIGNED,
    MMIO_CAPABILITY_MAPPING_ERROR
};

struct mmio_capability_handle {
    uint32_t generation;
    uint8_t slot;
};

struct mmio_capability_audit_event {
    uint64_t sequence;
    uint64_t actor_domain;
    uint64_t owner_domain;
    uint64_t offset;
    uint32_t generation;
    uint8_t slot;
    enum mmio_capability_operation operation;
    enum mmio_capability_result result;
};

enum mmio_capability_result mmio_capability_grant(
    uint64_t owner_domain, struct mmio_mapping *mapping, uint32_t rights,
    struct mmio_capability_handle *handle);
enum mmio_capability_result mmio_capability_read32(
    struct mmio_capability_handle handle, uint64_t offset, uint32_t *value);
enum mmio_capability_result mmio_capability_write32(
    struct mmio_capability_handle handle, uint64_t offset, uint32_t value);
enum mmio_capability_result mmio_capability_revoke(
    struct mmio_capability_handle handle);
enum mmio_capability_result mmio_capability_revoke_kernel(
    struct mmio_capability_handle handle);
size_t mmio_capability_audit_read(struct mmio_capability_audit_event *events,
                                  size_t capacity);
uint64_t mmio_capability_audit_overflow_count(void);

#endif
