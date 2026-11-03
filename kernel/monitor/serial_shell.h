#ifndef ELOQUENCE_MONITOR_SERIAL_SHELL_H
#define ELOQUENCE_MONITOR_SERIAL_SHELL_H

typedef void (*serial_shell_command_handler)(const char *line,
                                             void *context);

__attribute__((noreturn)) void serial_shell_run(
    serial_shell_command_handler handle_command, void *context);

#endif
