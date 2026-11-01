#ifndef ELOQUENCE_RISCV64_CONSOLE_H
#define ELOQUENCE_RISCV64_CONSOLE_H

#include <stdint.h>

void riscv64_console_write(const char *text);
void riscv64_console_write_u64(uint64_t value);

#endif
