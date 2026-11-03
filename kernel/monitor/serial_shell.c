#include <stdbool.h>
#include <stddef.h>

#include "arch/x86_64/serial.h"
#include "serial_shell.h"

#define SERIAL_SHELL_LINE_CAPACITY 128U

static bool skip_line_feed;

static char read_char(void) {
    char value;
    while (!serial_try_read_char(&value)) {
        __asm__ volatile ("pause");
    }
    return value;
}

static bool read_line(char *line, size_t capacity) {
    size_t length = 0;
    bool too_long = false;

    if (line == 0 || capacity < 2) {
        return false;
    }

    for (;;) {
        char value = read_char();
        if (skip_line_feed) {
            skip_line_feed = false;
            if (value == '\n') {
                continue;
            }
        }

        if (value == '\r' || value == '\n') {
            skip_line_feed = value == '\r';
            serial_write("\n");
            if (too_long) {
                serial_write("Input too long; command discarded.\n");
                line[0] = '\0';
            } else {
                line[length] = '\0';
            }
            return true;
        }

        if (value == '\b' || value == 0x7f) {
            if (length != 0 && !too_long) {
                length--;
                line[length] = '\0';
                serial_write("\b \b");
            }
            continue;
        }
        if (value == 0x15) {
            while (length != 0 && !too_long) {
                length--;
                serial_write("\b \b");
            }
            line[length] = '\0';
            continue;
        }
        if (value < 0x20 || value > 0x7e) {
            continue;
        }
        if (length + 1 >= capacity) {
            if (!too_long) {
                serial_write("\a");
            }
            too_long = true;
            continue;
        }

        line[length++] = value;
        line[length] = '\0';
        char echo[2] = {value, '\0'};
        serial_write(echo);
    }
}

__attribute__((noreturn)) void serial_shell_run(
    serial_shell_command_handler handle_command, void *context) {
    char line[SERIAL_SHELL_LINE_CAPACITY];

    serial_write("\nEloquence serial monitor. Type 'help' for commands.\n");
    for (;;) {
        serial_write("eloquence> ");
        if (!read_line(line, sizeof(line))) {
            serial_write("Could not read command line.\n");
            continue;
        }
        if (handle_command != 0) {
            handle_command(line, context);
        }
    }
}
