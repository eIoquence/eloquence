#ifndef ELOQUENCE_PROCESS_SCHEDULER_H
#define ELOQUENCE_PROCESS_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

bool kernel_scheduler_start_root(uint64_t process_id, uint64_t address_space,
                                 uint64_t kernel_stack_top);
bool kernel_scheduler_add(uint64_t process_id, uint64_t address_space,
                          uint64_t kernel_stack_top,
                          uint64_t *initial_context);
bool kernel_scheduler_is_active(void);
bool kernel_scheduler_has_process(uint64_t process_id);
uint64_t *kernel_scheduler_on_timer(uint64_t *context);
uint64_t *kernel_scheduler_on_exit(void);
bool kernel_scheduler_kill(uint64_t process_id);

#endif
