#ifndef ELOQUENCE_X86_64_EXCEPTIONS_H
#define ELOQUENCE_X86_64_EXCEPTIONS_H

#include <stdint.h>

void x86_exceptions_initialize(void);
__attribute__((noreturn)) void x86_exception_dispatch(uint64_t vector);

#endif
