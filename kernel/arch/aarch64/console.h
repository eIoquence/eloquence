#ifndef ELOQUENCE_AARCH64_CONSOLE_H
#define ELOQUENCE_AARCH64_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

bool aarch64_console_initialize(const struct limine_framebuffer *framebuffer);
void aarch64_console_write(const char *text);
void aarch64_console_write_u64(uint64_t value);

#endif
