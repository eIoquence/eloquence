#include "eloquence/syscall_abi.h"

static inline uint64_t user_syscall(uint64_t number, uint64_t a0,
                                    uint64_t a1, uint64_t a2) {
    uint64_t result;
    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(a0), "S"(a1), "d"(a2)
                      : "rcx", "r11", "memory");
    return result;
}

extern "C" void _start(void) {
    for (;;) {
        user_syscall(ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR, 'A', 0, 0);
        user_syscall(ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR, (uint64_t)'\n', 0, 0);
        for (volatile uint64_t i = 0; i < 100000; i++);
    }
}
