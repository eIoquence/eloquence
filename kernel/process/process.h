#ifndef ELOQUENCE_PROCESS_PROCESS_H
#define ELOQUENCE_PROCESS_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_PROCESS_MAX 16U

enum kernel_process_state {
    KERNEL_PROCESS_UNUSED,
    KERNEL_PROCESS_RUNNING,
    KERNEL_PROCESS_EXITED,
    KERNEL_PROCESS_FAULTED
};

struct kernel_process_info {
    uint64_t process_id;
    uint64_t started_at_tick;
    uint64_t finished_at_tick;
    uint64_t exit_status;
    enum kernel_process_state state;
};

uint64_t kernel_process_start(uint64_t process_id, uint64_t tick);
bool kernel_process_finish(uint64_t process_id, uint64_t tick,
                           uint64_t exit_status, bool faulted);
uint32_t kernel_process_snapshot(struct kernel_process_info *entries,
                                 uint32_t capacity);
bool kernel_process_is_running(uint64_t process_id);

#endif
