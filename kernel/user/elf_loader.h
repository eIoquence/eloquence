#ifndef ELOQUENCE_USER_ELF_LOADER_H
#define ELOQUENCE_USER_ELF_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Run one bounded, static x86-64 ELF image synchronously in ring 3. */
bool x86_user_elf_run(const void *image, size_t image_size,
                      uint64_t *exit_status, bool *faulted);
bool x86_user_elf_spawn(const void *image, size_t image_size,
                        uint64_t parent_id, uint64_t *process_id);
bool x86_user_elf_reap(uint64_t process_id);

#endif
