#include <stddef.h>
#include <stdint.h>

#include "console.h"

enum {
    SBI_LEGACY_CONSOLE_PUTCHAR = 1
};

static void sbi_console_putchar(char character) {
    register long argument0 __asm__("a0") = (unsigned char)character;
    register long extension_id __asm__("a7") = SBI_LEGACY_CONSOLE_PUTCHAR;

    __asm__ volatile ("ecall"
                      : "+r"(argument0)
                      : "r"(extension_id)
                      : "a1", "memory");
}

void riscv64_console_write(const char *text) {
    if (text == 0) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            sbi_console_putchar('\r');
        }
        sbi_console_putchar(text[i]);
    }
}

void riscv64_console_write_u64(uint64_t value) {
    char digits[20];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0);

    while (count > 0) {
        sbi_console_putchar(digits[--count]);
    }
}
