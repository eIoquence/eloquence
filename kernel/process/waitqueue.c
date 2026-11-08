#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "waitqueue.h"

static struct kernel_waitqueue waitqueues[KERNEL_WAITQUEUE_MAX];
static bool waitqueue_initialized = false;

static void waitqueue_ensure_init(void) {
    if (waitqueue_initialized) {
        return;
    }
    for (uint32_t i = 0; i < KERNEL_WAITQUEUE_MAX; i++) {
        waitqueues[i].in_use = false;
        waitqueues[i].entry_count = 0;
        for (uint32_t j = 0; j < KERNEL_WAITQUEUE_ENTRY_MAX; j++) {
            waitqueues[i].entries[j] = 0;
        }
    }
    waitqueue_initialized = true;
}

static int32_t waitqueue_alloc_slot(void) {
    for (uint32_t i = 0; i < KERNEL_WAITQUEUE_MAX; i++) {
        if (!waitqueues[i].in_use) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool waitqueue_has_process(uint64_t queue_id, uint64_t process_id) {
    for (uint32_t i = 0; i < waitqueues[queue_id].entry_count; i++) {
        if (waitqueues[queue_id].entries[i] == process_id) {
            return true;
        }
    }
    return false;
}

bool kernel_waitqueue_create(uint64_t *queue_id) {
    if (queue_id == 0) {
        return false;
    }
    waitqueue_ensure_init();
    int32_t slot = waitqueue_alloc_slot();
    if (slot < 0) {
        return false;
    }
    waitqueues[slot].in_use = true;
    waitqueues[slot].entry_count = 0;
    *queue_id = (uint64_t)slot;
    return true;
}

bool kernel_waitqueue_destroy(uint64_t queue_id) {
    waitqueue_ensure_init();
    if (queue_id >= KERNEL_WAITQUEUE_MAX || !waitqueues[queue_id].in_use) {
        return false;
    }
    waitqueues[queue_id].in_use = false;
    waitqueues[queue_id].entry_count = 0;
    return true;
}

bool kernel_waitqueue_block(uint64_t queue_id, uint64_t process_id) {
    waitqueue_ensure_init();
    if (queue_id >= KERNEL_WAITQUEUE_MAX
        || !waitqueues[queue_id].in_use
        || process_id == 0) {
        return false;
    }
    if (waitqueue_has_process(queue_id, process_id)) {
        return true; /* already blocked */
    }
    if (waitqueues[queue_id].entry_count >= KERNEL_WAITQUEUE_ENTRY_MAX) {
        return false; /* queue full */
    }
    waitqueues[queue_id].entries[waitqueues[queue_id].entry_count++] =
        process_id;
    return true;
}

uint64_t kernel_waitqueue_wake_one(uint64_t queue_id) {
    waitqueue_ensure_init();
    if (queue_id >= KERNEL_WAITQUEUE_MAX || !waitqueues[queue_id].in_use
        || waitqueues[queue_id].entry_count == 0) {
        return 0;
    }
    /* Remove the first waiting process (FIFO) and return its id. */
    uint64_t process_id = waitqueues[queue_id].entries[0];
    for (uint32_t i = 1; i < waitqueues[queue_id].entry_count; i++) {
        waitqueues[queue_id].entries[i - 1] = waitqueues[queue_id].entries[i];
    }
    waitqueues[queue_id].entry_count--;
    return process_id;
}

bool kernel_waitqueue_wake_all(uint64_t queue_id) {
    waitqueue_ensure_init();
    if (queue_id >= KERNEL_WAITQUEUE_MAX || !waitqueues[queue_id].in_use) {
        return false;
    }
    bool woke = false;
    while (waitqueues[queue_id].entry_count > 0) {
        waitqueues[queue_id].entry_count--;
        woke = true;
    }
    return woke;
}

uint32_t kernel_waitqueue_count(uint64_t queue_id) {
    waitqueue_ensure_init();
    if (queue_id >= KERNEL_WAITQUEUE_MAX || !waitqueues[queue_id].in_use) {
        return 0;
    }
    return waitqueues[queue_id].entry_count;
}
