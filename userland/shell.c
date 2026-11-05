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

static void system_report(uint64_t report) {
    (void)user_syscall(ELOQUENCE_SYSCALL_SYSTEM_REPORT, report, 0, 0);
}

static bool command_is(const char *line, const char *name) {
    while (*name != '\0' && *line == *name) {
        line++;
        name++;
    }
    return *name == '\0' && (*line == '\0' || *line == ' ' || *line == '\t');
}

static size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool has_prefix(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return false;
        }
    }
    return true;
}

static void recall_line(char *line, size_t *length, size_t capacity,
                        const char *history) {
    while (*length != 0) {
        (*length)--;
        erase_character();
    }
    while (history[*length] != '\0' && *length + 1 < capacity) {
        line[*length] = history[*length];
        write_char(line[*length]);
        (*length)++;
    }
    line[*length] = '\0';
}

static void complete_command(char *line, size_t *length, size_t capacity) {
    static const char *const commands[] = {
        "help", "clear", "echo", "uname", "uptime", "version",
        "sysinfo", "meminfo", "cpuinfo", "devinfo", "irqinfo",
        "bootinfo", "about", "exit"
    };
    const char *match = 0;
    size_t matches = 0;

    if (*length == 0) {
        return;
    }
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (has_prefix(commands[i], line)) {
            match = commands[i];
            matches++;
        }
    }
    if (matches != 1) {
        return;
    }
    match += *length;
    while (*match != '\0' && *length + 1 < capacity) {
        line[*length] = *match++;
        write_char(line[(*length)++]);
    }
    line[*length] = '\0';
}

static bool text_equals(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static void read_line(char *line, size_t capacity, const char *history) {
    size_t length = 0;
    bool skip_line_feed = false;
    uint8_t escape_state = 0;

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
        if (escape_state != 0) {
            if (escape_state == 1 && value == '[') {
                escape_state = 2;
                continue;
            }
            if (escape_state == 2 && value == 'A') {
                recall_line(line, &length, capacity, history);
            }
            escape_state = 0;
            continue;
        }
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
        if ((uint8_t)value == 0x1b) {
            escape_state = 1;
            continue;
        }
        if (value == '\t') {
            complete_command(line, &length, capacity);
            continue;
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
    char history[SHELL_LINE_CAPACITY] = {0};

    write_string("Eloquence user shell. Type 'help'.\n");
    for (;;) {
        write_string("user> ");
        read_line(line, sizeof(line), history);
        if (line[0] != '\0') {
            size_t length = text_length(line);
            for (size_t i = 0; i <= length; i++) {
                history[i] = line[i];
            }
        }
        if (text_equals(line, "help")) {
            write_string("Commands: help, clear, echo TEXT, uname, uptime, version, sysinfo, meminfo, cpuinfo, devinfo, irqinfo, bootinfo, about, exit\n");
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
        } else if (text_equals(line, "sysinfo")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_SYSINFO);
        } else if (text_equals(line, "meminfo")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_MEMINFO);
        } else if (text_equals(line, "cpuinfo")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_CPUINFO);
        } else if (text_equals(line, "devinfo")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_DEVINFO);
        } else if (text_equals(line, "irqinfo")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_IRQINFO);
        } else if (text_equals(line, "bootinfo")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_BOOTINFO);
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
