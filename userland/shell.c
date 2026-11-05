#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eloquence/syscall_abi.h"

#define SHELL_LINE_CAPACITY 80U
#define TIMER_FREQUENCY_HZ 100U
#define SHELL_VERSION "Eloquence shell 0.1.0 (x86_64)"

static inline uint64_t user_syscall(uint64_t number, uint64_t argument0,
                                    uint64_t argument1, uint64_t argument2) {
    uint64_t result;

    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(argument0), "S"(argument1),
                        "d"(argument2)
                      : "rcx", "r11", "memory");
    return result;
}

static void write_char(char value) {
    (void)user_syscall(ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR,
                       (uint8_t)value, 0, 0);
}

static void write_string(const char *text) {
    while (*text != '\0') {
        write_char(*text++);
    }
}

static void erase_character(void) {
    write_char('\b');
    write_char(' ');
    write_char('\b');
}

static void write_unsigned(uint64_t value) {
    char digits[20];
    size_t length = 0;

    do {
        digits[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (length != 0) {
        write_char(digits[--length]);
    }
}

static bool command_is(const char *line, const char *name) {
    while (*name != '\0' && *line == *name) {
        line++;
        name++;
    }
    return *name == '\0' && (*line == '\0' || *line == ' ' || *line == '\t');
}

static bool text_equals(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void read_line(char *line, size_t capacity) {
    size_t length = 0;
    bool skip_line_feed = false;

    for (;;) {
        uint64_t result = user_syscall(ELOQUENCE_SYSCALL_CONSOLE_READ_CHAR,
                                       0, 0, 0);
        uint32_t status = (uint32_t)(result >> 32);
        if (status == ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK) {
            __asm__ volatile ("pause");
            continue;
        }
        if (status != ELOQUENCE_SYSCALL_RESULT_OK) {
            continue;
        }

        char value = (char)(uint8_t)result;
        if (skip_line_feed) {
            skip_line_feed = false;
            if (value == '\n') {
                continue;
            }
        }
        if (value == '\r' || value == '\n') {
            skip_line_feed = value == '\r';
            line[length] = '\0';
            write_char('\n');
            return;
        }
        if (value == '\b' || (uint8_t)value == 0x7f) {
            if (length != 0) {
                line[--length] = '\0';
                erase_character();
            }
            continue;
        }
        if ((uint8_t)value == 0x15) {
            while (length != 0) {
                line[--length] = '\0';
                erase_character();
            }
            continue;
        }
        if (value < 0x20 || value > 0x7e || length + 1 >= capacity) {
            continue;
        }

        line[length++] = value;
        write_char(value);
    }
}

__attribute__((noreturn)) void _start(void) {
    char line[SHELL_LINE_CAPACITY];

    write_string("Eloquence user shell. Type 'help'.\n");
    for (;;) {
        write_string("user> ");
        read_line(line, sizeof(line));
        if (text_equals(line, "help")) {
            write_string("Commands: help, clear, echo TEXT, uname, uptime, version, about, exit\n");
        } else if (text_equals(line, "clear")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_CONSOLE_CLEAR, 0, 0, 0);
        } else if (command_is(line, "echo")) {
            const char *text = line + 4;
            while (*text == ' ' || *text == '\t') {
                text++;
            }
            write_string(text);
            write_char('\n');
        } else if (text_equals(line, "uname")) {
            write_string("Eloquence x86_64\n");
        } else if (text_equals(line, "uptime")) {
            uint64_t ticks = user_syscall(ELOQUENCE_SYSCALL_UPTIME_TICKS,
                                          0, 0, 0);
            write_string("Uptime: ");
            write_unsigned(ticks / TIMER_FREQUENCY_HZ);
            write_string(" seconds (");
            write_unsigned(ticks);
            write_string(" ticks)\n");
        } else if (text_equals(line, "version")) {
            write_string(SHELL_VERSION "\n");
        } else if (text_equals(line, "about")) {
            write_string("Ring-3 shell; serial input/output via syscalls.\n");
        } else if (text_equals(line, "exit")) {
            user_syscall(ELOQUENCE_SYSCALL_EXIT, 0, 0, 0);
            for (;;) {
                __asm__ volatile ("pause");
            }
        } else if (line[0] != '\0') {
            write_string("Unknown command. Type 'help'.\n");
        }
    }
}
