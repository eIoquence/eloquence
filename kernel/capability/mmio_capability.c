#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/mmio.h"
#include "capability/mmio_capability.h"

#define CAPABILITY_SLOT_COUNT 32
#define AUDIT_EVENT_COUNT 64
#define VALID_RIGHTS (MMIO_CAPABILITY_READ32 | MMIO_CAPABILITY_WRITE32)

struct mmio_capability_slot {
    struct mmio_mapping mapping;
    uint64_t owner_domain;
    uint32_t rights;
    uint32_t generation;
    bool active;
};

static struct mmio_capability_slot capability_slots[CAPABILITY_SLOT_COUNT];
static struct mmio_capability_audit_event audit_events[AUDIT_EVENT_COUNT];
static uint64_t audit_sequence;
static size_t audit_count;

static void audit_record(enum mmio_capability_operation operation,
                         enum mmio_capability_result result,
                         uint64_t actor_domain, uint64_t owner_domain,
                         struct mmio_capability_handle handle,
                         uint64_t offset) {
    struct mmio_capability_audit_event *event =
        &audit_events[audit_sequence % AUDIT_EVENT_COUNT];

    event->sequence = audit_sequence++;
    event->actor_domain = actor_domain;
    event->owner_domain = owner_domain;
    event->offset = offset;
    event->generation = handle.generation;
    event->slot = handle.slot;
    event->operation = operation;
    event->result = result;

    if (audit_count < AUDIT_EVENT_COUNT) {
        audit_count++;
    }
}

static enum mmio_capability_result lookup_capability(
    uint64_t actor_domain, struct mmio_capability_handle handle,
    enum mmio_capability_operation operation, uint64_t offset,
    struct mmio_capability_slot **slot_out) {
    if (handle.slot >= CAPABILITY_SLOT_COUNT) {
        audit_record(operation, MMIO_CAPABILITY_INVALID_HANDLE, actor_domain,
                     0, handle, offset);
        return MMIO_CAPABILITY_INVALID_HANDLE;
    }

    struct mmio_capability_slot *slot = &capability_slots[handle.slot];
    if (!slot->active || slot->generation != handle.generation) {
        audit_record(operation, MMIO_CAPABILITY_STALE_HANDLE, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_STALE_HANDLE;
    }
    if (slot->owner_domain != actor_domain) {
        audit_record(operation, MMIO_CAPABILITY_ACCESS_DENIED, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_ACCESS_DENIED;
    }

    *slot_out = slot;
    return MMIO_CAPABILITY_OK;
}

static enum mmio_capability_result validate_access(
    struct mmio_capability_slot *slot, uint64_t offset, uint32_t right,
    enum mmio_capability_operation operation,
    struct mmio_capability_handle handle, uint64_t actor_domain) {
    if ((slot->rights & right) == 0) {
        audit_record(operation, MMIO_CAPABILITY_ACCESS_DENIED, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_ACCESS_DENIED;
    }
    if ((offset & 3) != 0) {
        audit_record(operation, MMIO_CAPABILITY_MISALIGNED, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_MISALIGNED;
    }
    if (offset > slot->mapping.length
        || slot->mapping.length - offset < sizeof(uint32_t)) {
        audit_record(operation, MMIO_CAPABILITY_OUT_OF_RANGE, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_OUT_OF_RANGE;
    }

    return MMIO_CAPABILITY_OK;
}

enum mmio_capability_result mmio_capability_grant(
    uint64_t owner_domain, struct mmio_mapping *mapping, uint32_t rights,
    struct mmio_capability_handle *handle) {
    struct mmio_capability_handle invalid = {0};
    uint8_t slot_index;

    if (owner_domain == 0 || mapping == 0 || handle == 0
        || mapping->address == 0 || mapping->length < sizeof(uint32_t)
        || rights == 0 || (rights & ~VALID_RIGHTS) != 0) {
        audit_record(MMIO_CAPABILITY_OP_GRANT,
                     MMIO_CAPABILITY_INVALID_ARGUMENT, 0,
                     owner_domain, invalid, 0);
        return MMIO_CAPABILITY_INVALID_ARGUMENT;
    }

    for (slot_index = 0; slot_index < CAPABILITY_SLOT_COUNT; slot_index++) {
        if (!capability_slots[slot_index].active) {
            break;
        }
    }
    if (slot_index == CAPABILITY_SLOT_COUNT) {
        audit_record(MMIO_CAPABILITY_OP_GRANT,
                     MMIO_CAPABILITY_TABLE_FULL, 0,
                     owner_domain, invalid, 0);
        return MMIO_CAPABILITY_TABLE_FULL;
    }

    struct mmio_capability_slot *slot = &capability_slots[slot_index];
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    slot->mapping = *mapping;
    slot->owner_domain = owner_domain;
    slot->rights = rights;
    slot->active = true;

    handle->slot = slot_index;
    handle->generation = slot->generation;
    *mapping = (struct mmio_mapping){0};

    audit_record(MMIO_CAPABILITY_OP_GRANT, MMIO_CAPABILITY_OK, 0,
                 owner_domain, *handle, 0);
    return MMIO_CAPABILITY_OK;
}

enum mmio_capability_result mmio_capability_read32(
    uint64_t actor_domain, struct mmio_capability_handle handle,
    uint64_t offset, uint32_t *value) {
    struct mmio_capability_slot *slot;
    enum mmio_capability_result result;

    if (value == 0) {
        audit_record(MMIO_CAPABILITY_OP_READ32,
                     MMIO_CAPABILITY_INVALID_ARGUMENT, actor_domain, 0,
                     handle, offset);
        return MMIO_CAPABILITY_INVALID_ARGUMENT;
    }

    result = lookup_capability(actor_domain, handle,
                               MMIO_CAPABILITY_OP_READ32, offset, &slot);
    if (result != MMIO_CAPABILITY_OK) {
        return result;
    }
    result = validate_access(slot, offset, MMIO_CAPABILITY_READ32,
                             MMIO_CAPABILITY_OP_READ32, handle, actor_domain);
    if (result != MMIO_CAPABILITY_OK) {
        return result;
    }

    if (!arch_mmio_read32(&slot->mapping, offset, value)) {
        audit_record(MMIO_CAPABILITY_OP_READ32,
                     MMIO_CAPABILITY_MAPPING_ERROR, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_MAPPING_ERROR;
    }
    audit_record(MMIO_CAPABILITY_OP_READ32, MMIO_CAPABILITY_OK, actor_domain,
                 slot->owner_domain, handle, offset);
    return MMIO_CAPABILITY_OK;
}

enum mmio_capability_result mmio_capability_write32(
    uint64_t actor_domain, struct mmio_capability_handle handle,
    uint64_t offset, uint32_t value) {
    struct mmio_capability_slot *slot;
    enum mmio_capability_result result =
        lookup_capability(actor_domain, handle, MMIO_CAPABILITY_OP_WRITE32,
                          offset, &slot);

    if (result != MMIO_CAPABILITY_OK) {
        return result;
    }
    result = validate_access(slot, offset, MMIO_CAPABILITY_WRITE32,
                             MMIO_CAPABILITY_OP_WRITE32, handle, actor_domain);
    if (result != MMIO_CAPABILITY_OK) {
        return result;
    }

    if (!arch_mmio_write32(&slot->mapping, offset, value)) {
        audit_record(MMIO_CAPABILITY_OP_WRITE32,
                     MMIO_CAPABILITY_MAPPING_ERROR, actor_domain,
                     slot->owner_domain, handle, offset);
        return MMIO_CAPABILITY_MAPPING_ERROR;
    }
    audit_record(MMIO_CAPABILITY_OP_WRITE32, MMIO_CAPABILITY_OK, actor_domain,
                 slot->owner_domain, handle, offset);
    return MMIO_CAPABILITY_OK;
}

enum mmio_capability_result mmio_capability_revoke(
    uint64_t actor_domain, struct mmio_capability_handle handle) {
    struct mmio_capability_slot *slot;
    enum mmio_capability_result result =
        lookup_capability(actor_domain, handle, MMIO_CAPABILITY_OP_REVOKE, 0,
                          &slot);

    if (result != MMIO_CAPABILITY_OK) {
        return result;
    }
    if (!arch_mmio_unmap(&slot->mapping)) {
        audit_record(MMIO_CAPABILITY_OP_REVOKE,
                     MMIO_CAPABILITY_MAPPING_ERROR, actor_domain,
                     slot->owner_domain, handle, 0);
        return MMIO_CAPABILITY_MAPPING_ERROR;
    }

    slot->active = false;
    slot->owner_domain = 0;
    slot->rights = 0;
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    audit_record(MMIO_CAPABILITY_OP_REVOKE, MMIO_CAPABILITY_OK, actor_domain,
                 actor_domain, handle, 0);
    return MMIO_CAPABILITY_OK;
}

size_t mmio_capability_audit_read(struct mmio_capability_audit_event *events,
                                  size_t capacity) {
    if (events == 0 || capacity == 0) {
        return 0;
    }

    size_t count = audit_count < capacity ? audit_count : capacity;
    uint64_t first_sequence = audit_sequence - audit_count;
    if (count < audit_count) {
        first_sequence += audit_count - count;
    }

    for (size_t i = 0; i < count; i++) {
        uint64_t sequence = first_sequence + i;
        events[i] = audit_events[sequence % AUDIT_EVENT_COUNT];
    }

    return count;
}
