#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eloquence/syscall_abi.h"

#define IPC_CHANNEL 0U

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

__attribute__((noreturn)) void _start(void) {
    write_string("ipc-demo: process started, echoing on channel 0\n");
    (void)user_syscall(ELOQUENCE_SYSCALL_IPC_CREATE, IPC_CHANNEL, 0, 0);

    for (;;) {
        uint64_t result = user_syscall(ELOQUENCE_SYSCALL_IPC_RECEIVE,
                                       IPC_CHANNEL, 0, 0);
        uint32_t status = (uint32_t)(result >> 32);
        if (status == ELOQUENCE_SYSCALL_RESULT_OK) {
            char one[2] = {(char)(uint8_t)result, '\0'};
            write_string("ipc-demo: echo ");
            write_string(one);
            write_char('\n');
            (void)user_syscall(ELOQUENCE_SYSCALL_IPC_SEND, IPC_CHANNEL,
                               result, 0);
        } else {
            __asm__ volatile ("pause");
        }
    }
}
