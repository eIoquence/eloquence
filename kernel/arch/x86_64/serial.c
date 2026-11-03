#include <stddef.h>
#include <stdint.h>

#include "io.h"
#include "serial.h"

#define COM1_BASE 0x3f8

static void serial_write_byte(char value) {
    while ((x86_inb(COM1_BASE + 5) & 0x20) == 0) {
    }

    x86_outb(COM1_BASE, (uint8_t)value);
}

void serial_initialize(void) {
    x86_outb(COM1_BASE + 1, 0x00);
    x86_outb(COM1_BASE + 3, 0x80);
    x86_outb(COM1_BASE, 0x01);
    x86_outb(COM1_BASE + 1, 0x00);
    x86_outb(COM1_BASE + 3, 0x03);
    x86_outb(COM1_BASE + 2, 0xc7);
    x86_outb(COM1_BASE + 4, 0x0b);
}

void serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            serial_write_byte('\r');
        }
        serial_write_byte(*text++);
    }
}

bool serial_write_user_char(uint64_t value) {
    if (value > 0x7f || (value < 0x20 && value != '\n'
                         && value != '\r' && value != '\t')) {
        return false;
    }
    if (value == '\n') {
        serial_write_byte('\r');
    }
    serial_write_byte((char)value);
    return true;
}

bool serial_try_read_char(char *value) {
    if (value == 0 || (x86_inb(COM1_BASE + 5) & 0x01) == 0) {
        return false;
    }

    *value = (char)x86_inb(COM1_BASE);
    return true;
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_write_byte(digits[(value >> shift) & 0xf]);
    }
}

void serial_write_hex32(uint32_t value) {
    static const char digits[] = "0123456789abcdef";

    for (int shift = 28; shift >= 0; shift -= 4) {
        serial_write_byte(digits[(value >> shift) & 0xf]);
    }
}

void serial_write_decimal(uint64_t value) {
    char digits[20];
    size_t length = 0;

    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (length > 0) {
        serial_write_byte(digits[--length]);
    }
}
