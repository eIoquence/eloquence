#ifndef ELOQUENCE_X86_64_SERIAL_H
#define ELOQUENCE_X86_64_SERIAL_H

#include <stdbool.h>
#include <stdint.h>

void serial_initialize(void);
void serial_write(const char *text);
bool serial_write_user_char(uint64_t value);
bool serial_try_read_char(char *value);
void serial_write_hex(uint64_t value);
void serial_write_hex32(uint32_t value);
void serial_write_decimal(uint64_t value);

#endif
