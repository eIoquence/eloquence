#ifndef ELOQUENCE_PROCESS_WAITQUEUE_H
#define ELOQUENCE_PROCESS_WAITQUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_WAITQUEUE_MAX 8U
#define KERNEL_WAITQUEUE_ENTRY_MAX 16U

/* A waitqueue holds processes blocked on a condition (e.g. pipe empty/full).
 * Processes are woken one at a time (wake_one) or all (wake_all) when the
 * condition becomes satisfiable. */
struct kernel_waitqueue {
    bool in_use;
    uint64_t entries[KERNEL_WAITQUEUE_ENTRY_MAX];
    uint32_t entry_count;
};

bool kernel_waitqueue_create(uint64_t *queue_id);
bool kernel_waitqueue_destroy(uint64_t queue_id);
bool kernel_waitqueue_block(uint64_t queue_id, uint64_t process_id);
/* Wake one blocked process and return its id (0 if none). The caller must
 * mark it runnable via kernel_process_set_runnable. */
uint64_t kernel_waitqueue_wake_one(uint64_t queue_id);
bool kernel_waitqueue_wake_all(uint64_t queue_id);
uint32_t kernel_waitqueue_count(uint64_t queue_id);

#endif
