#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eloquence/syscall_abi.h"

#define SHELL_LINE_CAPACITY 80U
#define SHELL_PIPELINE_CAPACITY 256U
#define SHELL_SCRIPT_CAPACITY 2048U
#define SHELL_SCRIPT_VARIABLES 8U
#define TIMER_FREQUENCY_HZ 100U
#define SHELL_VERSION "Eloquence shell 0.1.0 (x86_64)"
#define SHELL_ENV_CAPACITY 8U
#define SHELL_ALIAS_CAPACITY 8U
static char shell_alias_names[SHELL_ALIAS_CAPACITY][16];
static char shell_alias_values[SHELL_ALIAS_CAPACITY][64];
static size_t shell_alias_count = 0;
static char shell_env_names[SHELL_ENV_CAPACITY][16];
static char shell_env_values[SHELL_ENV_CAPACITY][64];
static size_t shell_env_count = 0;

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
        "bootinfo", "mmio", "ls", "cat", "stat", "touch", "rm", "mkdir", "pwd", "cd",
        "ps", "jobs", "wait", "kill", "exec", "spawn", "script",
        "about", "exit", "reboot", "shutdown", "env", "export", "alias", "unalias", "history",
        "caps", "cap-add", "cap-drop", "mounts", "ram-ls", "ram-cat", "ram-touch", "ram-rm", "ram-write"
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

static bool parse_number(const char *text, uint64_t *value) {
    uint64_t result = 0;
    if (text == 0 || *text == '\0') {
        return false;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9'
            || result > (UINT64_MAX - (uint64_t)(*text - '0')) / 10) {
            return false;
        }
        result = result * 10 + (uint64_t)(*text - '0');
    }
    *value = result;
    return true;
}

static char *skip_spaces(char *text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

static uint32_t cap_bits_from_name(const char *name) {
    if (text_equals(name, "system")) {
        return 0x01U;
    }
    if (text_equals(name, "admin")) {
        return 0x02U;
    }
    if (text_equals(name, "mmio")) {
        return 0x04U;
    }
    if (text_equals(name, "fs")) {
        return 0x08U;
    }
    if (text_equals(name, "ipc")) {
        return 0x10U;
    }
    return 0U;
}

static void cap_print_bits(uint32_t bits) {
    static const char *const names[] = { "system", "admin", "mmio", "fs", "ipc" };
    static const uint32_t masks[] = { 0x01U, 0x02U, 0x04U, 0x08U, 0x10U };
    bool first = true;
    for (size_t i = 0; i < 5; i++) {
        if ((bits & masks[i]) != 0) {
            if (!first) {
                write_char(' ');
            }
            write_string(names[i]);
            first = false;
        }
    }
    if (first) {
        write_string("(none)");
    }
    write_char('\n');
}

static void append_text(char *buffer, size_t capacity, size_t *length,
                        const char *text) {
    while (*text != '\0' && *length + 1 < capacity) {
        buffer[(*length)++] = *text++;
    }
    buffer[*length] = '\0';
}

static bool run_pipeline(char *line) {
    char output[SHELL_PIPELINE_CAPACITY];
    char input[SHELL_PIPELINE_CAPACITY];
    char *stages[8];
    size_t stage_count = 0;
    char *cursor = line;
    output[0] = '\0';
    input[0] = '\0';

    while (stage_count < sizeof(stages) / sizeof(stages[0])) {
        stages[stage_count++] = cursor;
        while (*cursor != '\0' && *cursor != '|') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        *cursor++ = '\0';
    }
    if (*cursor != '\0' || stage_count < 2) {
        write_string("Pipeline: up to 8 stages are supported.\n");
        return true;
    }

    for (size_t stage = 0; stage < stage_count; stage++) {
        char *command = skip_spaces(stages[stage]);
        char *end = command + text_length(command);
        while (end > command && (end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        output[0] = '\0';
        size_t output_length = 0;
        if (command_is(command, "echo")) {
            append_text(output, sizeof(output), &output_length,
                        skip_spaces(command + 4));
            append_text(output, sizeof(output), &output_length, "\n");
        } else if (text_equals(command, "uname")) {
            append_text(output, sizeof(output), &output_length,
                        "Eloquence x86_64\n");
        } else if (text_equals(command, "version")) {
            append_text(output, sizeof(output), &output_length,
                        SHELL_VERSION "\n");
        } else if (command_is(command, "cat")) {
            char *filename = skip_spaces(command + 3);
            uint64_t packed_base;
            uint64_t packed_extension;
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string("Usage: cat SHORTNAME.EXT\n");
                return true;
            }
            uint32_t offset = 0;
            for (;;) {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_FS_READ_CHUNK,
                                               packed_base, packed_extension,
                                               offset);
                uint32_t status = (uint32_t)(result >> 32);
                if (status == ELOQUENCE_SYSCALL_RESULT_END_OF_FILE) {
                    break;
                }
                if (status != ELOQUENCE_SYSCALL_RESULT_OK) {
                    write_string("cat: read failed\n");
                    return true;
                }
                uint32_t data = (uint32_t)result;
                uint32_t count = (data >> 30) & 3U;
                if (count == 0) {
                    break;
                }
                for (uint32_t i = 0; i < count; i++) {
                    char one[2] = {(char)(uint8_t)((data >> (i * 8)) & 0xff), '\0'};
                    append_text(output, sizeof(output), &output_length, one);
                }
                offset += count;
            }
        } else if (command_is(command, "grep")) {
            char *pattern = skip_spaces(command + 4);
            if (*pattern == '\0' || stage == 0) {
                write_string("Usage: producer | grep TEXT\n");
                return true;
            }
            size_t pattern_length = text_length(pattern);
            const char *line_start = input;
            while (*line_start != '\0') {
                const char *line_end = line_start;
                while (*line_end != '\0' && *line_end != '\n') {
                    line_end++;
                }
                size_t line_length = (size_t)(line_end - line_start);
                bool found = false;
                for (size_t i = 0; i + pattern_length <= line_length; i++) {
                    size_t j = 0;
                    while (j < pattern_length && line_start[i + j] == pattern[j]) {
                        j++;
                    }
                    if (j == pattern_length) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    for (size_t i = 0; i < line_length; i++) {
                        char one[2] = {line_start[i], '\0'};
                        append_text(output, sizeof(output), &output_length, one);
                    }
                    append_text(output, sizeof(output), &output_length, "\n");
                }
                line_start = *line_end == '\n' ? line_end + 1 : line_end;
            }
        } else if (text_equals(command, "wc")) {
            size_t bytes = text_length(input);
            size_t lines = 0;
            size_t words = 0;
            bool in_word = false;
            for (size_t i = 0; i < bytes; i++) {
                char value = input[i];
                if (value == '\n') {
                    lines++;
                }
                if (value == ' ' || value == '\t' || value == '\n') {
                    in_word = false;
                } else if (!in_word) {
                    words++;
                    in_word = true;
                }
            }
            append_text(output, sizeof(output), &output_length, "lines ");
            // Format numbers locally because pipeline output stays buffered.
            char number[21];
            size_t digits = 0;
            uint64_t value = lines;
            do { number[digits++] = (char)('0' + value % 10); value /= 10; }
            while (value != 0);
            while (digits != 0) { char one[2] = {number[--digits], '\0'};
                append_text(output, sizeof(output), &output_length, one); }
            append_text(output, sizeof(output), &output_length, " words ");
            value = words;
            digits = 0;
            do { number[digits++] = (char)('0' + value % 10); value /= 10; }
            while (value != 0);
            while (digits != 0) { char one[2] = {number[--digits], '\0'};
                append_text(output, sizeof(output), &output_length, one); }
            append_text(output, sizeof(output), &output_length, " bytes ");
            value = bytes;
            digits = 0;
            do { number[digits++] = (char)('0' + value % 10); value /= 10; }
            while (value != 0);
            while (digits != 0) { char one[2] = {number[--digits], '\0'};
                append_text(output, sizeof(output), &output_length, one); }
            append_text(output, sizeof(output), &output_length, "\n");
        } else {
            write_string("Pipeline supports echo, uname, version, grep and wc.\n");
            return true;
        }
        size_t input_length = text_length(output);
        for (size_t i = 0; i <= input_length; i++) {
            input[i] = output[i];
        }
    }
    write_string(output);
    return true;
}

struct shell_script_variable {
    bool used;
    char name[16];
    char value[SHELL_LINE_CAPACITY];
};

static bool variable_name_char(char value) {
    return (value >= 'A' && value <= 'Z')
        || (value >= 'a' && value <= 'z')
        || (value >= '0' && value <= '9') || value == '_';
}

static void run_script_line(char *line,
                            struct shell_script_variable *variables) {
    char *command = skip_spaces(line);
    if (*command == '\0' || *command == '#') {
        return;
    }
    if (command_is(command, "set")) {
        char *assignment = skip_spaces(command + 3);
        char *separator = assignment;
        size_t name_length = 0;
        while (*separator != '\0' && *separator != '=' && *separator != ' '
               && *separator != '\t') {
            if (!variable_name_char(*separator) || name_length + 1 >= 16) {
                write_string("script: invalid variable name\n");
                return;
            }
            separator++;
            name_length++;
        }
        if (*separator != '=' || name_length == 0) {
            write_string("Usage: set NAME=VALUE\n");
            return;
        }
        size_t slot = SHELL_SCRIPT_VARIABLES;
        for (size_t i = 0; i < SHELL_SCRIPT_VARIABLES; i++) {
            if (variables[i].used) {
                size_t j = 0;
                while (j < name_length
                       && variables[i].name[j] == assignment[j]) {
                    j++;
                }
                if (j == name_length && variables[i].name[j] == '\0') {
                    slot = i;
                    break;
                }
            }
            if (!variables[i].used && slot == SHELL_SCRIPT_VARIABLES) {
                slot = i;
            }
        }
        if (slot == SHELL_SCRIPT_VARIABLES) {
            write_string("script: variable table is full\n");
            return;
        }
        variables[slot].used = true;
        for (size_t i = 0; i < name_length; i++) {
            variables[slot].name[i] = assignment[i];
        }
        variables[slot].name[name_length] = '\0';
        char *value = separator + 1;
        size_t value_length = text_length(value);
        if (value_length >= sizeof(variables[slot].value)) {
            value_length = sizeof(variables[slot].value) - 1;
        }
        for (size_t i = 0; i < value_length; i++) {
            variables[slot].value[i] = value[i];
        }
        variables[slot].value[value_length] = '\0';
        return;
    }
    for (char *cursor = command; *cursor != '\0'; cursor++) {
        if (*cursor == '|') {
            (void)run_pipeline(command);
            return;
        }
    }
    if (command_is(command, "echo")) {
        write_string(skip_spaces(command + 4));
        write_char('\n');
    } else if (text_equals(command, "uname")) {
        write_string("Eloquence x86_64\n");
    } else if (text_equals(command, "version")) {
        write_string(SHELL_VERSION "\n");
    } else if (text_equals(command, "sysinfo")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_SYSINFO);
    } else if (text_equals(command, "meminfo")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_MEMINFO);
    } else if (text_equals(command, "cpuinfo")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_CPUINFO);
    } else if (text_equals(command, "devinfo")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_DEVINFO);
    } else if (text_equals(command, "irqinfo")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_IRQINFO);
    } else if (text_equals(command, "bootinfo")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_BOOTINFO);
    } else if (text_equals(command, "mmio")) {
        system_report(ELOQUENCE_SYSTEM_REPORT_MMIO);
    } else if (text_equals(command, "reboot")) {
        write_string("Rebooting...\n");
        uint64_t result = user_syscall(ELOQUENCE_SYSCALL_SYSTEM_CONTROL, 1, 0, 0);
        if ((uint32_t)(result >> 32) != 0) {
            write_string("reboot: denied (missing cap system)\n");
        }
    } else if (text_equals(command, "shutdown")) {
        write_string("Shutting down...\n");
        uint64_t result = user_syscall(ELOQUENCE_SYSCALL_SYSTEM_CONTROL, 0, 0, 0);
        if ((uint32_t)(result >> 32) != 0) {
            write_string("shutdown: denied (missing cap system)\n");
        }
    } else {
        write_string("script: unsupported command: ");
        write_string(command);
        write_char('\n');
    }
}

static bool expand_script_text(const char *source,
                               struct shell_script_variable *variables,
                               char expanded[SHELL_LINE_CAPACITY]) {
    size_t output_length = 0;
    for (size_t i = 0; source[i] != '\0';) {
        if (source[i] != '$') {
            if (output_length + 1 >= SHELL_LINE_CAPACITY) {
                return false;
            }
            expanded[output_length++] = source[i++];
            continue;
        }
        size_t name_start = i + 1;
        size_t name_end = name_start;
        while (variable_name_char(source[name_end])) {
            name_end++;
        }
        if (name_end == name_start) {
            if (output_length + 1 >= SHELL_LINE_CAPACITY) {
                return false;
            }
            expanded[output_length++] = source[i++];
            continue;
        }
        char name[16];
        size_t name_length = name_end - name_start;
        if (name_length >= sizeof(name)) {
            return false;
        }
        for (size_t j = 0; j < name_length; j++) {
            name[j] = source[name_start + j];
        }
        name[name_length] = '\0';
        const char *value = "";
        for (size_t j = 0; j < SHELL_SCRIPT_VARIABLES; j++) {
            if (variables[j].used && text_equals(variables[j].name, name)) {
                value = variables[j].value;
                break;
            }
        }
        size_t value_length = text_length(value);
        if (output_length + value_length >= SHELL_LINE_CAPACITY) {
            return false;
        }
        for (size_t j = 0; j < value_length; j++) {
            expanded[output_length++] = value[j];
        }
        i = name_end;
    }
    expanded[output_length] = '\0';
    return true;
}

static void run_script_commands(char *line,
                                struct shell_script_variable *variables) {
    char expanded[SHELL_LINE_CAPACITY];
    char *command_start = line;
    while (true) {
        char *separator = command_start;
        while (*separator != '\0' && *separator != ';') {
            separator++;
        }
        bool more = *separator == ';';
        if (more) {
            *separator = '\0';
        }
        if (!expand_script_text(command_start, variables, expanded)) {
            write_string("script: expanded command exceeds variable limits\n");
            return;
        }
        run_script_line(expanded, variables);
        if (!more) {
            return;
        }
        command_start = separator + 1;
    }
}

static bool script_condition(char *expression,
                             struct shell_script_variable *variables,
                             bool *result) {
    char expanded[SHELL_LINE_CAPACITY];
    if (!expand_script_text(skip_spaces(expression), variables, expanded)) {
        return false;
    }
    char *equals = expanded;
    while (*equals != '\0' && *equals != '=') {
        equals++;
    }
    if (*equals != '=') {
        return false;
    }
    *equals++ = '\0';
    *result = text_equals(skip_spaces(expanded), skip_spaces(equals));
    return true;
}

static bool parse_repeat_count(const char *text, uint32_t *count) {
    uint32_t value = 0;
    if (text == 0 || count == 0 || *text == '\0') {
        return false;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        value = value * 10 + (uint32_t)(*text - '0');
        if (value > 16) {
            return false;
        }
    }
    if (value == 0) {
        return false;
    }
    *count = value;
    return true;
}

struct shell_script_control {
    bool loop;
    bool parent_active;
    bool condition;
    bool else_seen;
    uint32_t remaining;
    size_t body_line;
};

static void run_script(const char *filename) {
    uint64_t packed_base;
    uint64_t packed_extension;
    char contents[SHELL_SCRIPT_CAPACITY];
    struct shell_script_variable variables[SHELL_SCRIPT_VARIABLES];
    size_t line_offsets[128];
    uint8_t line_lengths[128];
    struct shell_script_control controls[8];
    size_t line_count = 0;
    size_t length = 0;
    uint64_t offset = 0;
    for (size_t i = 0; i < SHELL_SCRIPT_VARIABLES; i++) {
        variables[i].used = false;
        variables[i].name[0] = '\0';
        variables[i].value[0] = '\0';
    }
    if (!pack_filename(filename, &packed_base, &packed_extension)) {
        write_string("Usage: script SHORTNAME.TXT\n");
        return;
    }
    for (;;) {
        uint64_t result = user_syscall(ELOQUENCE_SYSCALL_FS_READ_CHUNK,
                                       packed_base, packed_extension, offset);
        uint32_t status = (uint32_t)(result >> 32);
        if (status == ELOQUENCE_SYSCALL_RESULT_END_OF_FILE) {
            break;
        }
        uint32_t data = (uint32_t)result;
        uint32_t count = data >> 30;
        if (status != ELOQUENCE_SYSCALL_RESULT_OK || count == 0
            || length + count > sizeof(contents)) {
            write_string("script: read failed or script exceeds 2 KiB\n");
            return;
        }
        for (size_t i = 0; i < count; i++) {
            char value = (char)(data >> (i * 8));
            if (value == '\0') {
                write_string("script: NUL bytes are not valid text\n");
                return;
            }
            contents[length++] = value;
        }
        offset += count;
    }
    size_t line_start = 0;
    for (size_t i = 0; i <= length; i++) {
        if (i == length || contents[i] == '\n' || contents[i] == '\r') {
            size_t line_length = i - line_start;
            if (line_length >= SHELL_LINE_CAPACITY
                || line_count == sizeof(line_offsets) / sizeof(line_offsets[0])) {
                write_string("script: line or command count limit exceeded\n");
                return;
            }
            line_offsets[line_count] = line_start;
            line_lengths[line_count++] = (uint8_t)line_length;
            if (i < length && contents[i] == '\r' && i + 1 < length
                && contents[i + 1] == '\n') {
                i++;
            }
            line_start = i + 1;
        }
    }

    bool active = true;
    size_t control_depth = 0;
    for (size_t line_index = 0; line_index < line_count; line_index++) {
        char line[SHELL_LINE_CAPACITY];
        size_t line_length = line_lengths[line_index];
        for (size_t j = 0; j < line_length; j++) {
            line[j] = contents[line_offsets[line_index] + j];
        }
        line[line_length] = '\0';
        char *command = skip_spaces(line);
        if (*command == '\0' || *command == '#') {
            continue;
        }
        if (command_is(command, "if")) {
            bool condition = false;
            if (control_depth == sizeof(controls) / sizeof(controls[0])
                || !script_condition(skip_spaces(command + 2), variables,
                                     &condition)) {
                write_string("script: invalid or deeply nested if expression\n");
                return;
            }
            controls[control_depth++] = (struct shell_script_control){
                .loop = false,
                .parent_active = active,
                .condition = condition
            };
            active = active && condition;
            continue;
        }
        if (text_equals(command, "else")) {
            if (control_depth == 0 || controls[control_depth - 1].loop
                || controls[control_depth - 1].else_seen) {
                write_string("script: unexpected else\n");
                return;
            }
            struct shell_script_control *frame = &controls[control_depth - 1];
            frame->else_seen = true;
            active = frame->parent_active && !frame->condition;
            continue;
        }
        if (text_equals(command, "endif")) {
            if (control_depth == 0 || controls[control_depth - 1].loop) {
                write_string("script: unexpected endif\n");
                return;
            }
            active = controls[--control_depth].parent_active;
            continue;
        }
        if (command_is(command, "repeat")) {
            uint32_t count = 0;
            if (control_depth == sizeof(controls) / sizeof(controls[0])
                || !parse_repeat_count(skip_spaces(command + 6), &count)) {
                write_string("Usage: repeat COUNT (1-16)\n");
                return;
            }
            controls[control_depth++] = (struct shell_script_control){
                .loop = true,
                .parent_active = active,
                .remaining = active ? count : 0,
                .body_line = line_index + 1
            };
            continue;
        }
        if (text_equals(command, "endrepeat")) {
            if (control_depth == 0 || !controls[control_depth - 1].loop) {
                write_string("script: unexpected endrepeat\n");
                return;
            }
            struct shell_script_control *frame = &controls[control_depth - 1];
            if (frame->remaining > 1) {
                frame->remaining--;
                line_index = frame->body_line - 1;
            } else {
                active = frame->parent_active;
                control_depth--;
            }
            continue;
        }
        if (active) {
            run_script_commands(line, variables);
        }
    }
    if (control_depth != 0) {
        write_string("script: unterminated control block\n");
    }
}

static bool change_directory_component(char *argument, char *path,
                                      size_t capacity) {
    uint64_t result;
    bool to_root = text_equals(argument, "/");
    bool to_parent = text_equals(argument, "..");
    if (to_root) {
        result = user_syscall(ELOQUENCE_SYSCALL_FS_CHDIR,
                              ELOQUENCE_FS_CHDIR_ROOT, 0, 0);
    } else if (to_parent) {
        result = user_syscall(ELOQUENCE_SYSCALL_FS_CHDIR,
                              ELOQUENCE_FS_CHDIR_PARENT, 0, 0);
    } else {
        uint64_t packed_base;
        uint64_t packed_extension;
        if (!pack_filename(argument, &packed_base, &packed_extension)) {
            write_string("Usage: cd / | .. | SHORTNAME\n");
            return false;
        }
        result = user_syscall(ELOQUENCE_SYSCALL_FS_CHDIR, packed_base,
                              packed_extension, 0);
    }
    if ((uint32_t)(result >> 32) != ELOQUENCE_SYSCALL_RESULT_OK) {
        write_string("cd: directory not found or inaccessible\n");
        return false;
    }
    if (to_root) {
        path[0] = '/';
        path[1] = '\0';
    } else if (to_parent) {
        size_t length = text_length(path);
        if (length > 1) {
            while (length > 1 && path[length - 1] != '/') {
                length--;
            }
            if (length > 1) {
                path[length - 1] = '\0';
            } else {
                path[1] = '\0';
            }
        }
    } else {
        size_t length = text_length(path);
        size_t name_length = text_length(argument);
        size_t separator = length == 1 ? 0 : 1;
        if (length + separator + name_length + 1 > capacity) {
            write_string("cd: path exceeds shell path limit\n");
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_CHDIR,
                               ELOQUENCE_FS_CHDIR_PARENT, 0, 0);
            return false;
        }
        if (separator != 0) {
            path[length++] = '/';
        }
        for (size_t i = 0; i < name_length; i++) {
            path[length++] = argument[i];
        }
        path[length] = '\0';
    }
    return true;
}

static bool change_directory(char *argument, char *path, size_t capacity) {
    char *cursor = argument;
    if (*cursor == '\0') {
        write_string("Usage: cd /PATH | PATH | ..\n");
        return false;
    }
    if (*cursor == '/') {
        if (!change_directory_component("/", path, capacity)) {
            return false;
        }
        cursor++;
    }
    while (*cursor != '\0') {
        char *separator = cursor;
        while (*separator != '\0' && *separator != '/') {
            separator++;
        }
        bool more = *separator == '/';
        if (more) {
            *separator = '\0';
        }
        if (*cursor != '\0' && !text_equals(cursor, ".")
            && !change_directory_component(cursor, path, capacity)) {
            return false;
        }
        if (!more) {
            break;
        }
        cursor = separator + 1;
    }
    return true;
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

static void read_line(char *line, size_t capacity,
                      char history[][SHELL_LINE_CAPACITY],
                      size_t history_count, size_t *history_pos) {
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
                if (history_count != 0 && *history_pos > 0) {
                    (*history_pos)--;
                    recall_line(line, &length, capacity, history[*history_pos]);
                }
            }
            if (escape_state == 2 && value == 'B') {
                if (*history_pos + 1 < history_count) {
                    (*history_pos)++;
                    recall_line(line, &length, capacity, history[*history_pos]);
                } else {
                    while (length != 0) { length--; erase_character(); }
                    line[0] = '\0';
                }
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
    char history[8][SHELL_LINE_CAPACITY];
    for (size_t hi = 0; hi < 8; hi++) history[hi][0] = '\0';
    /* Eloquence-branded aliases */
    const char *names[] = {"print", "list", "show", "time", "info", "memory", "devices"};
    const char *vals[] = {"echo", "ls", "cat", "uptime", "sysinfo", "meminfo", "devinfo"};
    for (size_t i = 0; i < 7; i++) {
        size_t slot = shell_alias_count++;
        for (size_t j = 0; names[i][j] != '\0' && j + 1 < 16; j++) {
            shell_alias_names[slot][j] = names[i][j];
            shell_alias_names[slot][j + 1] = '\0';
        }
        for (size_t j = 0; vals[i][j] != '\0' && j + 1 < 64; j++) {
            shell_alias_values[slot][j] = vals[i][j];
            shell_alias_values[slot][j + 1] = '\0';
        }
    }
    size_t history_count = 0;
    size_t history_pos = 0;
    char current_path[128] = "/";

    write_string("Eloquence user shell. Type 'help'.\n");
    for (;;) {
        write_string("user:");
        write_string(current_path);
        write_string("> ");
        history_pos = history_count;
        read_line(line, sizeof(line), history, history_count, &history_pos);
        if (line[0] != '\0' && history_count < 8) {
            size_t length = text_length(line);
            for (size_t i = 0; i <= length && i + 1 < SHELL_LINE_CAPACITY; i++) {
                history[history_count][i] = line[i];
            }
            history[history_count][length] = '\0';
            history_count++;
        }
        /* alias expansion */
        {
            char *cursor = line;
            while (*cursor == ' ' || *cursor == '\t') cursor++;
            char *name_end = cursor;
            while (*name_end != '\0' && *name_end != ' ' && *name_end != '\t') name_end++;
            char saved = *name_end;
            *name_end = '\0';
            for (size_t i = 0; i < shell_alias_count; i++) {
                if (text_equals(shell_alias_names[i], cursor)) {
                    char rest[SHELL_LINE_CAPACITY];
                    size_t ri = 0;
                    char *r = name_end;
                    while (*r != '\0' && ri + 1 < SHELL_LINE_CAPACITY) {
                        rest[ri++] = *r++;
                    }
                    rest[ri] = '\0';
                    *name_end = saved;
                    /* rebuild line */
                    char buffer[SHELL_LINE_CAPACITY];
                    size_t bi = 0;
                    for (size_t j = 0; shell_alias_values[i][j] != '\0' && bi + 1 < SHELL_LINE_CAPACITY; j++) {
                        buffer[bi++] = shell_alias_values[i][j];
                    }
                    for (size_t j = 0; rest[j] != '\0' && bi + 1 < SHELL_LINE_CAPACITY; j++) {
                        buffer[bi++] = rest[j];
                    }
                    buffer[bi] = '\0';
                    for (size_t j = 0; j <= bi; j++) {
                        line[j] = buffer[j];
                    }
                    break;
                }
            }
            *name_end = saved;
        }
        /* redirect < transformation */
        {
            char *lt = line;
            while (*lt != '\0' && *lt != '<') lt++;
            if (*lt == '<') {
                *lt = '\0';
                char *file = lt + 1;
                while (*file == ' ' || *file == '\t') file++;
                char *cmd = line;
                while (*cmd == ' ' || *cmd == '\t') cmd++;
                char buffer[SHELL_LINE_CAPACITY];
                size_t bi = 0;
                const char *prefix = "cat ";
                for (size_t i = 0; prefix[i] != '\0' && bi + 1 < SHELL_LINE_CAPACITY; i++) {
                    buffer[bi++] = prefix[i];
                }
                for (size_t i = 0; file[i] != '\0' && bi + 1 < SHELL_LINE_CAPACITY; i++) {
                    buffer[bi++] = file[i];
                }
                if (bi + 1 < SHELL_LINE_CAPACITY) buffer[bi++] = '|';
                if (bi + 1 < SHELL_LINE_CAPACITY) buffer[bi++] = ' ';
                for (size_t i = 0; cmd[i] != '\0' && bi + 1 < SHELL_LINE_CAPACITY; i++) {
                    buffer[bi++] = cmd[i];
                }
                buffer[bi] = '\0';
                for (size_t i = 0; i <= bi; i++) line[i] = buffer[i];
            }
        }
        /* elo (sudo-like) prefix stripping */
        if (command_is(line, "elo")) {
            char *rest = line + 3;
            while (*rest == ' ' || *rest == '\t') rest++;
            size_t ri = 0;
            while (rest[ri] != '\0') {
                line[ri] = rest[ri];
                ri++;
            }
            line[ri] = '\0';
        }
        bool has_pipe = false;
        for (size_t i = 0; line[i] != '\0'; i++) {
            if (line[i] == '|') {
                has_pipe = true;
                break;
            }
        }
        if (has_pipe) {
            (void)run_pipeline(line);
            continue;
        }
        if (text_equals(line, "help")) {
            write_string("Commands: help, clear, echo, uname, uptime, version, reboot, shutdown, sysinfo, meminfo, cpuinfo, devinfo, irqinfo, bootinfo, mmio, ls, cat, stat, touch, rm, mkdir, pwd, cd, cp, mv, ps, jobs, wait PID, exec, spawn, kill, script FILE.TXT, env, export, alias, unalias, history, elo, eloquence, eos-info, eos-process, eos-fs, eos-diagnostics, caps, cap-add NAME, cap-drop NAME, mounts, ram-ls, ram-touch NAME, ram-rm NAME, ram-cat NAME, ram-write NAME TEXT, about, exit; pipelines: echo|uname|version|grep|wc\n");
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
        } else if (command_is(line, "script")) {
            run_script(skip_spaces(line + 6));
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
        } else if (text_equals(line, "mmio")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_MMIO);
        } else if (text_equals(line, "ps")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_PROCESS_LIST, 0, 0, 0);
        } else if (text_equals(line, "jobs")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_PROCESS_LIST, 0, 0, 0);
        } else if (command_is(line, "wait")) {
            const char *argument = line + 4;
            uint64_t process_id;
            while (*argument == ' ' || *argument == '\t') {
                argument++;
            }
            if (!parse_process_id(argument, &process_id)) {
                write_string("Usage: wait PID (a child started by spawn)\n");
            } else {
                uint64_t result;
                do {
                    result = user_syscall(ELOQUENCE_SYSCALL_PROCESS_WAIT,
                                          process_id, 0, 0);
                    if ((uint32_t)(result >> 32)
                        == ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK) {
                        __asm__ volatile ("pause");
                    }
                } while ((uint32_t)(result >> 32)
                         == ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK);
                if ((uint32_t)(result >> 32)
                    != ELOQUENCE_SYSCALL_RESULT_OK) {
                    write_string("wait: PID is not a child process\n");
                } else {
                    write_string("Process ");
                    write_unsigned(process_id);
                    write_string(" exited with status ");
                    write_unsigned((uint32_t)result);
                    write_char('\n');
                }
            }
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
                    write_string("kill: no live process with that PID\n");
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
        } else if (command_is(line, "spawn")) {
            const char *filename = line + 5;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string("Usage: spawn SHORTNAME.ELF\n");
            } else {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_PROCESS_SPAWN,
                                               packed_base,
                                               packed_extension, 0);
                if ((uint32_t)(result >> 32)
                    != ELOQUENCE_SYSCALL_RESULT_OK) {
                    write_string("spawn: failed\n");
                } else {
                    write_string("Spawned process ");
                    write_unsigned((uint32_t)result);
                    write_char('\n');
                }
            }
        } else if (text_equals(line, "ls")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_LIST_CWD, 0, 0, 0);
        } else if (text_equals(line, "pwd")) {
            write_string(current_path);
            write_char('\n');
        } else if (command_is(line, "cd")) {
            (void)change_directory(skip_spaces(line + 2), current_path,
                                   sizeof(current_path));
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
            bool move_file = line[0] == 'm';
            char *cursor = line + 2;
            char *source_name = cursor;
            char *destination_name;
            uint64_t source_base;
            uint64_t source_extension;
            uint64_t destination_base;
            uint64_t destination_extension;
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
                cursor++;
            }
            while (*cursor == ' ' || *cursor == '\t') {
                *cursor++ = '\0';
            }
            destination_name = cursor;
            if (!pack_filename(source_name, &source_base, &source_extension)
                || !pack_filename(destination_name, &destination_base,
                                 &destination_extension)) {
                write_string(move_file ? "Usage: mv SRC.EXT DST.EXT\n"
                                      : "Usage: cp SRC.EXT DST.EXT\n");
            } else {
                uint64_t result = user_syscall(
                    ELOQUENCE_SYSCALL_FS_COPY_BEGIN, source_base,
                    source_extension, move_file ? 1 : 0);
                if ((uint32_t)(result >> 32) != ELOQUENCE_SYSCALL_RESULT_OK) {
                    write_string(move_file ? "mv: could not read source\n"
                                          : "cp: could not read source\n");
                } else {
                    result = user_syscall(ELOQUENCE_SYSCALL_FS_COPY_COMMIT,
                                          destination_base, destination_extension,
                                          0);
                    if ((uint32_t)(result >> 32)
                        != ELOQUENCE_SYSCALL_RESULT_OK) {
                        write_string(move_file ? "mv: could not write destination\n"
                                              : "cp: could not write destination\n");
                    } else {
                        write_string(move_file ? "Moved " : "Copied ");
                        write_string(source_name);
                        write_string(" to ");
                        write_string(destination_name);
                        write_char('\n');
                    }
                }
            }
        } else if (text_equals(line, "reboot")) {
            write_string("Rebooting...\n");
            uint64_t result = user_syscall(ELOQUENCE_SYSCALL_SYSTEM_CONTROL,
                                           1, 0, 0);
            if ((uint32_t)(result >> 32) != 0) {
                write_string("reboot: denied (missing cap system)\n");
            }
        } else if (text_equals(line, "shutdown")) {
            write_string("Shutting down...\n");
            uint64_t result = user_syscall(ELOQUENCE_SYSCALL_SYSTEM_CONTROL,
                                           0, 0, 0);
            if ((uint32_t)(result >> 32) != 0) {
                write_string("shutdown: denied (missing cap system)\n");
            }
        } else if (text_equals(line, "env")) {
            for (size_t i = 0; i < shell_env_count; i++) {
                write_string(shell_env_names[i]);
                write_char('=');
                write_string(shell_env_values[i]);
                write_char('\n');
            }
        } else if (command_is(line, "export")) {
            char *cursor = line + 6;
            char *name = cursor;
            char *value;
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' && *cursor != '=') {
                cursor++;
            }
            while (*cursor == ' ' || *cursor == '\t') {
                *cursor++ = '\0';
            }
            if (*cursor == '=') {
                *cursor++ = '\0';
            }
            value = cursor;
            if (name[0] == '\0' || shell_env_count >= SHELL_ENV_CAPACITY) {
                write_string("Usage: export NAME=VALUE\n");
            } else {
                size_t slot = shell_env_count++;
                for (size_t i = 0; name[i] != '\0' && i + 1 < 16; i++) {
                    shell_env_names[slot][i] = name[i];
                    shell_env_names[slot][i + 1] = '\0';
                }
                for (size_t i = 0; value[i] != '\0' && i + 1 < 64; i++) {
                    shell_env_values[slot][i] = value[i];
                    shell_env_values[slot][i + 1] = '\0';
                }
            }
        } else if (command_is(line, "alias")) {
            char *cursor = line + 5;
            while (*cursor == ' ' || *cursor == '\t') cursor++;
            if (*cursor == '\0') {
                for (size_t i = 0; i < shell_alias_count; i++) {
                    write_string(shell_alias_names[i]);
                    write_string("=");
                    write_string(shell_alias_values[i]);
                    write_char('\n');
                }
            } else {
                char *eq = cursor;
                while (*eq != '\0' && *eq != '=') eq++;
                if (*eq != '=') {
                    write_string("Usage: alias NAME=COMMAND\n");
                } else {
                    *eq = '\0';
                    char *value = eq + 1;
                    if (shell_alias_count < SHELL_ALIAS_CAPACITY) {
                        size_t slot = shell_alias_count++;
                        for (size_t i = 0; cursor[i] != '\0' && i + 1 < 16; i++) {
                            shell_alias_names[slot][i] = cursor[i];
                            shell_alias_names[slot][i + 1] = '\0';
                        }
                        for (size_t i = 0; value[i] != '\0' && i + 1 < 64; i++) {
                            shell_alias_values[slot][i] = value[i];
                            shell_alias_values[slot][i + 1] = '\0';
                        }
                    }
                }
            }
        } else if (command_is(line, "unalias")) {
            char *name = line + 7;
            while (*name == ' ' || *name == '\t') name++;
            for (size_t i = 0; i < shell_alias_count; i++) {
                if (text_equals(shell_alias_names[i], name)) {
                    for (size_t j = i + 1; j < shell_alias_count; j++) {
                        for (size_t k = 0; k < 16; k++) shell_alias_names[j-1][k] = shell_alias_names[j][k];
                        for (size_t k = 0; k < 64; k++) shell_alias_values[j-1][k] = shell_alias_values[j][k];
                    }
                    shell_alias_count--;
                    break;
                }
            }
        } else if (text_equals(line, "history")) {
            for (size_t i = 0; i < history_count; i++) {
                write_unsigned(i + 1);
                write_string("  ");
                write_string(history[i]);
                write_char('\n');
            }
        } else if (text_equals(line, "eloquence")) {
            write_string("Eloquence shell 0.1.0 (x86_64)\n");
            write_string("Architecture: x86-64\n");
            write_string("Kernel: experimental exokernel\n");
            write_string("Subsystems: shell, VFS, process, memory, drivers\n");
        } else if (text_equals(line, "eos-info")) {
            write_string(SHELL_VERSION "\n");
            system_report(ELOQUENCE_SYSTEM_REPORT_SYSINFO);
            system_report(ELOQUENCE_SYSTEM_REPORT_MEMINFO);
        } else if (text_equals(line, "eos-process")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_PROCESS_LIST, 0, 0, 0);
        } else if (text_equals(line, "eos-fs")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_LIST_CWD, 0, 0, 0);
        } else if (text_equals(line, "eos-diagnostics")) {
            system_report(ELOQUENCE_SYSTEM_REPORT_IRQINFO);
            system_report(ELOQUENCE_SYSTEM_REPORT_DEVINFO);
            system_report(ELOQUENCE_SYSTEM_REPORT_BOOTINFO);
        } else if (command_is(line, "ipc-open")) {
            char *arg = skip_spaces(line + 8);
            uint64_t channel = 0;
            if (!parse_number(arg, &channel) || channel > 3) {
                write_string("Usage: ipc-open N (0-3)\n");
            } else {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_IPC_CREATE,
                                               channel, 0, 0);
                if ((uint32_t)(result >> 32) != 0) {
                    write_string("ipc: open failed\n");
                }
            }
        } else if (command_is(line, "ipc-send")) {
            char *arg = line + 8;
            while (*arg == ' ' || *arg == '\t') arg++;
            char channel_text[8];
            size_t ci = 0;
            while (*arg >= '0' && *arg <= '9' && ci + 1 < 8) {
                channel_text[ci++] = *arg++;
            }
            channel_text[ci] = '\0';
            while (*arg == ' ' || *arg == '\t') arg++;
            uint64_t channel = 0;
            if (!parse_number(channel_text, &channel) || channel > 3
                || *arg == '\0') {
                write_string("Usage: ipc-send N MSG (channel 0-3)\n");
            } else {
                bool ok = true;
                for (size_t i = 0; arg[i] != '\0'; i++) {
                    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_IPC_SEND,
                                                   channel,
                                                   (uint64_t)(uint8_t)arg[i],
                                                   0);
                    if ((uint32_t)(result >> 32) != 0) {
                        ok = false;
                        break;
                    }
                }
                (void)user_syscall(ELOQUENCE_SYSCALL_IPC_SEND,
                                   channel, '\n', 0);
                if (!ok) {
                    write_string("ipc: channel full\n");
                }
            }
        } else if (command_is(line, "ipc-recv")) {
            char *arg = skip_spaces(line + 8);
            uint64_t channel = 0;
            if (!parse_number(arg, &channel) || channel > 3) {
                write_string("Usage: ipc-recv N (0-3)\n");
            } else {
                for (;;) {
                    uint64_t result = user_syscall(
                        ELOQUENCE_SYSCALL_IPC_RECEIVE, channel, 0, 0);
                    uint32_t status = (uint32_t)(result >> 32);
                    if (status != 0) {
                        break;
                    }
                    char one[2] = {(char)(uint8_t)result, '\0'};
                    write_string(one);
                }
            }
        } else if (text_equals(line, "ram-ls")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                               ELOQUENCE_RAM_OP_LIST, 0, 0);
        } else if (text_equals(line, "mounts")) {
            (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                               ELOQUENCE_RAM_OP_MOUNTS, 0, 0);
        } else if (command_is(line, "ram-touch") || command_is(line, "ram-rm")) {
            bool create = command_is(line, "ram-touch");
            const char *filename = line + (create ? 9 : 6);
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string(create ? "Usage: ram-touch SHORTNAME.EXT\n"
                                    : "Usage: ram-rm SHORTNAME.EXT\n");
            } else {
                (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                                   create ? ELOQUENCE_RAM_OP_TOUCH
                                          : ELOQUENCE_RAM_OP_DELETE,
                                   packed_base, packed_extension);
            }
        } else if (command_is(line, "ram-cat")) {
            const char *filename = line + 7;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*filename == ' ' || *filename == '\t') {
                filename++;
            }
            if (!pack_filename(filename, &packed_base, &packed_extension)) {
                write_string("Usage: ram-cat SHORTNAME.EXT\n");
            } else {
                (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                                   ELOQUENCE_RAM_OP_CAT, packed_base,
                                   packed_extension);
            }
        } else if (command_is(line, "ram-write")) {
            const char *cursor = line + 9;
            char namebuf[24];
            size_t ni = 0;
            uint64_t packed_base;
            uint64_t packed_extension;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t'
                   && ni + 1 < sizeof(namebuf)) {
                namebuf[ni++] = *cursor++;
            }
            namebuf[ni] = '\0';
            if (ni == 0 || !pack_filename(namebuf, &packed_base,
                                          &packed_extension)) {
                write_string("Usage: ram-write SHORTNAME.EXT TEXT\n");
            } else {
                while (*cursor == ' ' || *cursor == '\t') {
                    cursor++;
                }
                if (*cursor == '\0') {
                    write_string("Usage: ram-write SHORTNAME.EXT TEXT\n");
                } else {
                    (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                                       ELOQUENCE_RAM_OP_WRITE_BEGIN,
                                       packed_base, packed_extension);
                    while (*cursor != '\0') {
                        (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                                           ELOQUENCE_RAM_OP_WRITE_CHAR,
                                           (uint64_t)(uint8_t)*cursor++, 0);
                    }
                    (void)user_syscall(ELOQUENCE_SYSCALL_FS_RAM_CONTROL,
                                       ELOQUENCE_RAM_OP_WRITE_COMMIT, 0, 0);
                }
            }
        } else if (text_equals(line, "caps")) {
            uint64_t result = user_syscall(ELOQUENCE_SYSCALL_CAP_QUERY, 0, 0, 0);
            if ((uint32_t)(result >> 32) != 0) {
                write_string("caps: query failed\n");
            } else {
                cap_print_bits((uint32_t)result);
            }
        } else if (command_is(line, "cap-drop")) {
            char *arg = skip_spaces(line + 8);
            uint32_t bits = cap_bits_from_name(arg);
            if (bits == 0U) {
                write_string("Usage: cap-drop system|admin|mmio|fs|ipc\n");
            } else {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_CAP_REVOKE,
                                               0, bits, 0);
                if ((uint32_t)(result >> 32) != 0) {
                    write_string("cap-drop: denied (need admin)\n");
                }
            }
        } else if (command_is(line, "cap-add")) {
            char *arg = skip_spaces(line + 7);
            uint32_t bits = cap_bits_from_name(arg);
            if (bits == 0U) {
                write_string("Usage: cap-add system|admin|mmio|fs|ipc\n");
            } else {
                uint64_t result = user_syscall(ELOQUENCE_SYSCALL_CAP_GRANT,
                                               0, bits, 0);
                if ((uint32_t)(result >> 32) != 0) {
                    write_string("cap-add: denied (need admin)\n");
                }
            }
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
