#ifndef ELOQUENCE_X86_64_EXCEPTIONS_H
#define ELOQUENCE_X86_64_EXCEPTIONS_H

#include <stdint.h>

struct x86_exception_frame {
    uint64_t vector;
    uint64_t error_code;
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

void x86_exceptions_initialize(void);
uint64_t x86_exception_dispatch(
    const struct x86_exception_frame *frame);

#endif
