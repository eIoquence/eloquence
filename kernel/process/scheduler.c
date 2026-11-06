#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "domain/domain.h"
#include "process.h"
#include "scheduler.h"
#include "arch/x86_64/user_mode.h"
#include "user/elf_loader.h"
#include "arch/x86_64/timer.h"

#include "net/netif.h"

struct scheduler_task {
    uint64_t process_id;
    uint64_t address_space;
    uint64_t kernel_stack_top;
    uint64_t *context;
    bool active;
};

static struct scheduler_task tasks[KERNEL_PROCESS_MAX];
static uint32_t task_count;
static uint32_t current_task;
static bool scheduler_active;

static int32_t find_task(uint64_t process_id) {
    for (uint32_t i = 0; i < task_count; i++) {
        if (tasks[i].active && tasks[i].process_id == process_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t choose_runnable(uint32_t after) {
    for (uint32_t offset = 1; offset <= task_count; offset++) {
        uint32_t index = (after + offset) % task_count;
        struct kernel_process_info process;
        if (tasks[index].active
            && kernel_process_get(tasks[index].process_id, &process)
            && (process.state == KERNEL_PROCESS_RUNNABLE
                || process.state == KERNEL_PROCESS_RUNNING)
            && tasks[index].context != 0) {
            return (int32_t)index;
        }
    }
    return -1;
}

static void activate_task(uint32_t index) {
    struct scheduler_task *task = &tasks[index];
    current_task = index;
    (void)kernel_process_set_runnable(task->process_id, true);
    x86_user_mode_switch_to(task->address_space, task->kernel_stack_top,
                            task->process_id);
}

static bool reap_unparented_task(uint64_t process_id) {
    struct kernel_process_info process;
    if (!kernel_process_get(process_id, &process) || process.parent_id == 0) {
        return false;
    }
    return x86_user_elf_reap(process_id);
}

bool kernel_scheduler_start_root(uint64_t process_id, uint64_t address_space,
                                 uint64_t kernel_stack_top) {
    if (scheduler_active || process_id == 0 || address_space == 0
        || kernel_stack_top == 0) {
        return false;
    }
    for (uint32_t i = 0; i < KERNEL_PROCESS_MAX; i++) {
        tasks[i] = (struct scheduler_task){0};
    }
    tasks[0] = (struct scheduler_task){
        .process_id = process_id,
        .address_space = address_space,
        .kernel_stack_top = kernel_stack_top,
        .active = true
    };
    task_count = 1;
    current_task = 0;
    scheduler_active = true;
    return true;
}

bool kernel_scheduler_add(uint64_t process_id, uint64_t address_space,
                          uint64_t kernel_stack_top,
                          uint64_t *initial_context) {
    if (!scheduler_active || process_id == 0 || address_space == 0
        || kernel_stack_top == 0 || initial_context == 0
        || find_task(process_id) >= 0) {
        return false;
    }
    uint32_t index = task_count;
    for (uint32_t i = 0; i < task_count; i++) {
        if (!tasks[i].active) {
            index = i;
            break;
        }
    }
    if (index == task_count) {
        if (task_count == KERNEL_PROCESS_MAX) {
            return false;
        }
        task_count++;
    }
    tasks[index] = (struct scheduler_task){
        .process_id = process_id,
        .address_space = address_space,
        .kernel_stack_top = kernel_stack_top,
        .context = initial_context,
        .active = true
    };
    return true;
}

bool kernel_scheduler_is_active(void) {
    return scheduler_active;
}

bool kernel_scheduler_has_process(uint64_t process_id) {
    return scheduler_active && find_task(process_id) >= 0;
}

uint64_t *kernel_scheduler_on_timer(uint64_t *context) {
    if (!scheduler_active || context == 0 || (context[16] & 3U) != 3U) {
        return context;
    }
    netif_poll_all();
    uint64_t process_id = kernel_domain_current_id();
    int32_t index = find_task(process_id);
    if (index < 0) {
        return context;
    }
    tasks[index].context = context;
    (void)kernel_process_set_runnable(process_id, false);
    int32_t next = choose_runnable((uint32_t)index);
    if (next < 0) {
        (void)kernel_process_set_runnable(process_id, true);
        return context;
    }
    activate_task((uint32_t)next);
    return tasks[next].context;
}

uint64_t *kernel_scheduler_on_exit(void) {
    uint64_t process_id = kernel_domain_current_id();
    int32_t index = find_task(process_id);
    if (index < 0) {
        kernel_domain_leave();
        return 0;
    }

    struct kernel_process_info exiting;
    bool is_root = kernel_process_get(process_id, &exiting)
        && exiting.parent_id == 0;
    (void)kernel_process_finish(process_id, x86_timer_ticks(),
                                x86_user_mode_exit_status(),
                                x86_user_mode_exit_faulted());
    tasks[index].active = false;
    if (is_root) {
        for (uint32_t i = 0; i < task_count; i++) {
            if (tasks[i].active) {
                uint64_t child_id = tasks[i].process_id;
                (void)kernel_process_finish(child_id, x86_timer_ticks(),
                                            143, false);
                tasks[i].active = false;
                (void)reap_unparented_task(child_id);
            }
        }
    }
    int32_t next = choose_runnable((uint32_t)index);
    if (next < 0) {
        scheduler_active = false;
        task_count = 0;
        kernel_domain_leave();
        (void)reap_unparented_task(process_id);
        return 0;
    }

    activate_task((uint32_t)next);
    (void)reap_unparented_task(process_id);
    return tasks[next].context;
}

bool kernel_scheduler_kill(uint64_t process_id) {
    int32_t index = find_task(process_id);
    if (index < 0 || (uint32_t)index == current_task) {
        return false;
    }
    if (!kernel_process_finish(process_id, x86_timer_ticks(), 137, false)) {
        return false;
    }
    tasks[index].active = false;
    (void)reap_unparented_task(process_id);
    return true;
}
