#ifndef ELOQUENCE_USER_ELF_LOADER_H
#define ELOQUENCE_USER_ELF_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Run one bounded, static x86-64 ELF image synchronously in ring 3. */
bool x86_user_elf_run(const void *image, size_t image_size,
                      uint64_t *exit_status, bool *faulted);

#endif
