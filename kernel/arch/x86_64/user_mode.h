#ifndef ELOQUENCE_X86_64_USER_MODE_H
#define ELOQUENCE_X86_64_USER_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include "paging.h"

bool x86_user_mode_initialize(void);
bool x86_user_mode_run(const struct x86_address_space *address_space,
                       uint64_t entry, uint64_t stack_pointer,
                       uint64_t domain_id, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2,
                       uint64_t *exit_status, bool *faulted);
bool x86_user_mode_run_with_kernel_stack(
    const struct x86_address_space *address_space, uint64_t entry,
    uint64_t stack_pointer, uint64_t domain_id, uint64_t argument0,
    uint64_t argument1, uint64_t argument2, uint64_t kernel_stack_top,
    uint64_t *exit_status, bool *faulted);
void x86_user_mode_set_exit_status(uint64_t status);
uint64_t x86_user_mode_fault_exit(uint64_t vector);
uint64_t x86_user_mode_exit_status(void);
bool x86_user_mode_exit_faulted(void);
void x86_user_mode_switch_to(uint64_t address_space,
                            uint64_t kernel_stack_top,
                            uint64_t process_id);

#endif
