#include <stdbool.h>
#include <stdint.h>

#include "process.h"

static struct kernel_process_info processes[KERNEL_PROCESS_MAX];
static uint32_t process_count;

uint64_t kernel_process_start(uint64_t process_id, uint64_t tick) {
    if (process_id == 0) {
        return 0;
    }
    if (process_count == KERNEL_PROCESS_MAX) {
        for (uint32_t i = 1; i < process_count; i++) {
            processes[i - 1] = processes[i];
        }
        process_count--;
    }
    processes[process_count++] = (struct kernel_process_info){
        .process_id = process_id,
        .started_at_tick = tick,
        .state = KERNEL_PROCESS_RUNNING
    };
    return process_id;
}

bool kernel_process_finish(uint64_t process_id, uint64_t tick,
                           uint64_t exit_status, bool faulted) {
    for (uint32_t i = 0; i < process_count; i++) {
        if (processes[i].process_id == process_id
            && processes[i].state == KERNEL_PROCESS_RUNNING) {
            processes[i].finished_at_tick = tick;
            processes[i].exit_status = exit_status;
            processes[i].state = faulted ? KERNEL_PROCESS_FAULTED
                                         : KERNEL_PROCESS_EXITED;
            return true;
        }
    }
    return false;
}

uint32_t kernel_process_snapshot(struct kernel_process_info *entries,
                                 uint32_t capacity) {
    if (entries == 0 || capacity == 0) {
        return 0;
    }
    uint32_t count = process_count < capacity ? process_count : capacity;
    for (uint32_t i = 0; i < count; i++) {
        entries[i] = processes[i];
    }
    return count;
}

bool kernel_process_is_running(uint64_t process_id) {
    for (uint32_t i = 0; i < process_count; i++) {
        if (processes[i].process_id == process_id
            && processes[i].state == KERNEL_PROCESS_RUNNING) {
            return true;
        }
    }
    return false;
}
