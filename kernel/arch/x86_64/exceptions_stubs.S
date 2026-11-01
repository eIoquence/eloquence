.section .text

.macro EXCEPTION_STUB vector
.global x86_exception_stub_\vector
.type x86_exception_stub_\vector, @function
x86_exception_stub_\vector:
    pushq $\vector
    jmp x86_exception_common
.size x86_exception_stub_\vector, .-x86_exception_stub_\vector
.endm

EXCEPTION_STUB 0
EXCEPTION_STUB 1
EXCEPTION_STUB 2
EXCEPTION_STUB 3
EXCEPTION_STUB 4
EXCEPTION_STUB 5
EXCEPTION_STUB 6
EXCEPTION_STUB 7
EXCEPTION_STUB 8
EXCEPTION_STUB 9
EXCEPTION_STUB 10
EXCEPTION_STUB 11
EXCEPTION_STUB 12
EXCEPTION_STUB 13
EXCEPTION_STUB 14
EXCEPTION_STUB 15
EXCEPTION_STUB 16
EXCEPTION_STUB 17
EXCEPTION_STUB 18
EXCEPTION_STUB 19
EXCEPTION_STUB 20
EXCEPTION_STUB 21
EXCEPTION_STUB 22
EXCEPTION_STUB 23
EXCEPTION_STUB 24
EXCEPTION_STUB 25
EXCEPTION_STUB 26
EXCEPTION_STUB 27
EXCEPTION_STUB 28
EXCEPTION_STUB 29
EXCEPTION_STUB 30
EXCEPTION_STUB 31

.global x86_exception_common
.type x86_exception_common, @function
x86_exception_common:
    cld
    movq (%rsp), %rdi
    andq $-16, %rsp
    call x86_exception_dispatch
    ud2
.size x86_exception_common, .-x86_exception_common

.section .note.GNU-stack,"",@progbits
