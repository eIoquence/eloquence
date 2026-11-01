#include <stddef.h>
#include <stdint.h>

#include "serial.h"

#define COM1_BASE 0x3f8

static void port_write(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t port_read(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_write_byte(char value) {
    while ((port_read(COM1_BASE + 5) & 0x20) == 0) {
    }

    port_write(COM1_BASE, (uint8_t)value);
}

void serial_initialize(void) {
    port_write(COM1_BASE + 1, 0x00);
    port_write(COM1_BASE + 3, 0x80);
    port_write(COM1_BASE, 0x01);
    port_write(COM1_BASE + 1, 0x00);
    port_write(COM1_BASE + 3, 0x03);
    port_write(COM1_BASE + 2, 0xc7);
    port_write(COM1_BASE + 4, 0x0b);
}

void serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            serial_write_byte('\r');
        }
        serial_write_byte(*text++);
    }
}

void serial_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    for (int shift = 60; shift >= 0; shift -= 4) {
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
