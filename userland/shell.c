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

static size_t text_length(const char *text);

static bool pack_filename(const char *text, uint64_t *base,
                          uint64_t *extension) {
    size_t base_length = 0;
    size_t extension_length = 0;
    bool in_extension = false;
    uint64_t packed_base = 0;
    uint64_t packed_extension = 0;

    if (*text == '\0') {
        return false;
    }
    for (; *text != '\0'; text++) {
        char value = *text;
        if (value == '.') {
            if (in_extension || base_length == 0) {
                return false;
            }
            in_extension = true;
            continue;
        }
        if (value >= 'a' && value <= 'z') {
            value = (char)(value - 'a' + 'A');
        }
        if (!((value >= 'A' && value <= 'Z')
              || (value >= '0' && value <= '9') || value == '_'
              || value == '-' || value == '$' || value == '~')) {
            return false;
        }
        if (in_extension) {
            if (++extension_length > 3) {
                return false;
            }
            packed_extension |= (uint64_t)(uint8_t)value
                                << ((extension_length - 1) * 8);
        } else {
            if (++base_length > 8) {
                return false;
            }
            packed_base |= (uint64_t)(uint8_t)value
                           << ((base_length - 1) * 8);
        }
    }
    if (base_length == 0 || (in_extension && extension_length == 0)) {
        return false;
    }
    *base = packed_base;
    *extension = packed_extension;
    return true;
}

static bool write_text_file(const char *filename, const char *text) {
    uint64_t packed_base;
    uint64_t packed_extension;
    if (!pack_filename(filename, &packed_base, &packed_extension)) {
        write_string("Usage: echo TEXT > SHORTNAME.EXT\n");
        return false;
    }
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_FS_WRITE_BEGIN,
                                   packed_base, packed_extension, 0);
    if ((uint32_t)(result >> 32) != ELOQUENCE_SYSCALL_RESULT_OK) {
        write_string("Could not begin file write.\n");
        return false;
    }

    size_t length = text_length(text);
    for (size_t offset = 0; offset < length; offset += 8) {
        uint64_t packed = 0;
        uint64_t count = length - offset < 8 ? length - offset : 8;
        for (uint64_t i = 0; i < count; i++) {
            packed |= (uint64_t)(uint8_t)text[offset + i] << (i * 8);
        }
        result = user_syscall(ELOQUENCE_SYSCALL_FS_WRITE_DATA,
                              packed, count, 0);
        if ((uint32_t)(result >> 32) != ELOQUENCE_SYSCALL_RESULT_OK) {
            write_string("File write buffer rejected data.\n");
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_WRITE_ABORT, 0, 0, 0);
            return false;
        }
    }
    (void)user_syscall(ELOQUENCE_SYSCALL_FS_WRITE_DATA, '\n', 1, 0);
    result = user_syscall(ELOQUENCE_SYSCALL_FS_WRITE_COMMIT, 0, 0, 0);
    if ((uint32_t)(result >> 32) != ELOQUENCE_SYSCALL_RESULT_OK) {
        write_string("Could not save file.\n");
        return false;
    }
    write_string("Wrote ");
    write_string(filename);
    write_char('\n');
    return true;
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
        "bootinfo", "ls", "cat", "stat", "touch", "rm", "mkdir", "pwd", "cd",
        "ps", "kill", "exec", "about", "exit"
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

static bool parse_process_id(const char *text, uint64_t *process_id) {
    uint64_t value = 0;
    if (*text == '\0' || process_id == 0) {
        return false;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9'
            || value > (UINT64_MAX - (uint64_t)(*text - '0')) / 10) {
            return false;
        }
        value = value * 10 + (uint64_t)(*text - '0');
    }
    *process_id = value;
    return true;
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
            write_string("Commands: help, clear, echo TEXT > FILE, uname, uptime, version, sysinfo, meminfo, cpuinfo, devinfo, irqinfo, bootinfo, ls, cat FILE, stat FILE, touch FILE, rm FILE, mkdir DIR, pwd, cd /, ps, exec FILE.ELF, kill PID, about, exit\n");
        } else if (text_equals(line, "clear")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_CONSOLE_CLEAR, 0, 0, 0);
        } else if (command_is(line, "echo")) {
            char *text = line + 4;
            while (*text == ' ' || *text == '\t') {
                text++;
            }
            char *redirect = text;
            while (*redirect != '\0' && *redirect != '>') {
                redirect++;
            }
            if (*redirect == '>') {
                char *filename = redirect + 1;
                while (redirect > text
                       && (redirect[-1] == ' ' || redirect[-1] == '\t')) {
                    redirect--;
                }
                *redirect = '\0';
                while (*filename == ' ' || *filename == '\t') {
                    filename++;
                }
                char *end = filename + text_length(filename);
                while (end > filename && (end[-1] == ' ' || end[-1] == '\t')) {
                    *--end = '\0';
                }
                (void)write_text_file(filename, text);
                continue;
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
        } else if (text_equals(line, "ps")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_PROCESS_LIST, 0, 0, 0);
        } else if (command_is(line, "kill")) {
            const char *argument = line + 4;
            uint64_t process_id;
            while (*argument == ' ' || *argument == '\t') {
                argument++;
            }
            if (!parse_process_id(argument, &process_id)) {
                write_string("Usage: kill PID\n");
            } else {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_PROCESS_KILL,
                                               process_id, 0, 0);
                if ((uint32_t)(result >> 32)
                    != ELOQUENCE_SYSCALL_RESULT_OK) {
                    write_string("kill: PID must be the current process\n");
                }
            }
        } else if (command_is(line, "exec")) {
            const char *filename = line + 4;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string("Usage: exec SHORTNAME.ELF\n");
            } else {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_PROCESS_EXEC,
                                               packed_base,
                                               packed_extension, 0);
                if ((uint32_t)(result >> 32)
                    != ELOQUENCE_SYSCALL_RESULT_OK) {
                    write_string("exec: failed\n");
                }
            }
        } else if (text_equals(line, "ls")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_LIST, 0, 1, 0);
        } else if (text_equals(line, "pwd")) {
            write_string("/\n");
        } else if (text_equals(line, "cd /")) {
            write_string("/\n");
        } else if (command_is(line, "cd")) {
            write_string("Only the root directory is available.\n");
        } else if (command_is(line, "cat")) {
            const char *filename = line + 3;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
            write_string("Usage: cat SHORTNAME.EXT (FAT 8.3 format)\n");
            } else {
                (void)user_syscall(ELOQUENCE_SYSCALL_FS_CAT, packed_base,
                                   packed_extension, 0);
            }
        } else if (command_is(line, "stat")) {
            const char *filename = line + 4;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string("Usage: stat SHORTNAME.EXT (FAT 8.3 format)\n");
            } else {
                (void)user_syscall(ELOQUENCE_SYSCALL_FS_STAT, packed_base,
                                   packed_extension, 0);
            }
        } else if (command_is(line, "touch") || command_is(line, "rm")) {
            bool remove_file = line[0] == 'r';
            const char *filename = line + (remove_file ? 2 : 5);
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string(remove_file ? "Usage: rm SHORTNAME.EXT\n"
                                         : "Usage: touch SHORTNAME.EXT\n");
            } else {
                (void)user_syscall(remove_file ? ELOQUENCE_SYSCALL_FS_DELETE
                                               : ELOQUENCE_SYSCALL_FS_TOUCH,
                                   packed_base, packed_extension, 0);
            }
        } else if (command_is(line, "mkdir")) {
            const char *dirname = line + 5;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*dirname == ' ' || *dirname == '\t') {
                dirname++;
            }
            if (!pack_filename(dirname, &packed_base, &packed_extension)) {
                write_string("Usage: mkdir SHORTNAME\n");
            } else {
                (void)user_syscall(ELOQUENCE_SYSCALL_FS_MKDIR, packed_base,
                                   packed_extension, 0);
            }
        } else if (command_is(line, "cp") || command_is(line, "mv")) {
            write_string("Command is not implemented yet.\n");
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
