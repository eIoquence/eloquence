#ifndef ELOQUENCE_X86_64_SYSCALL_H
#define ELOQUENCE_X86_64_SYSCALL_H

#include <stdint.h>

struct x86_syscall_frame {
    uint64_t r11;
    uint64_t rcx;
    uint64_t r9;
    uint64_t r8;
    uint64_t r10;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;
};

uint64_t x86_syscall_dispatch(const struct x86_syscall_frame *frame);

#endif
