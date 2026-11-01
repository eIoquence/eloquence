.section .text

.macro EXCEPTION_STUB vector, has_error_code=0
.global x86_exception_stub_\vector
.type x86_exception_stub_\vector, @function
x86_exception_stub_\vector:
.if !\has_error_code
    pushq $0
.endif
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
EXCEPTION_STUB 8, 1
EXCEPTION_STUB 9
EXCEPTION_STUB 10, 1
EXCEPTION_STUB 11, 1
EXCEPTION_STUB 12, 1
EXCEPTION_STUB 13, 1
EXCEPTION_STUB 14, 1
EXCEPTION_STUB 15
EXCEPTION_STUB 16
EXCEPTION_STUB 17, 1
EXCEPTION_STUB 18
EXCEPTION_STUB 19
EXCEPTION_STUB 20
EXCEPTION_STUB 21, 1
EXCEPTION_STUB 22
EXCEPTION_STUB 23
EXCEPTION_STUB 24
EXCEPTION_STUB 25
EXCEPTION_STUB 26
EXCEPTION_STUB 27
EXCEPTION_STUB 28
EXCEPTION_STUB 29, 1
EXCEPTION_STUB 30, 1
EXCEPTION_STUB 31

.global x86_exception_common
.type x86_exception_common, @function
x86_exception_common:
    cld
    movq %rsp, %rdi
    testb $3, 24(%rsp)
    jz .Lexception_kernel_gs_ready
    swapgs
.Lexception_kernel_gs_ready:
    andq $-16, %rsp
    call x86_exception_dispatch
    cmpq $-1, %rax
    je x86_user_exception_return
    ud2
.size x86_exception_common, .-x86_exception_common

.section .note.GNU-stack,"",@progbits
