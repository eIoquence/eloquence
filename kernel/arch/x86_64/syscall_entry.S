.section .text

.global x86_syscall_entry
.type x86_syscall_entry, @function
x86_syscall_entry:
    swapgs
    movq %rsp, %gs:0
    movq %gs:8, %rsp

    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    pushq %rcx
    pushq %r11

    movq %rsp, %rdi
    subq $8, %rsp
    call x86_syscall_dispatch
    addq $8, %rsp
    cmpq $-1, %rax
    je .Lreturn_to_kernel
    movq %rax, 64(%rsp)

    popq %r11
    popq %rcx
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    addq $8, %rsp

    movq %gs:0, %rsp
    swapgs
    sysretq

.Lreturn_to_kernel:
    swapgs
    jmp .Lrestore_kernel_context

.global x86_user_exception_return
x86_user_exception_return:
    swapgs
.Lrestore_kernel_context:
    movq x86_user_kernel_cr3(%rip), %rax
    movq %rax, %cr3
    movq x86_user_return_stack(%rip), %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq
.size x86_syscall_entry, .-x86_syscall_entry

.global x86_user_enter_asm
.type x86_user_enter_asm, @function
x86_user_enter_asm:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, x86_user_return_stack(%rip)

    movq %r9, %r11
    movq %rdi, %r9
    movq %rsi, %r10
    movq %rdx, %rdi
    movq %rcx, %rsi
    movq %r8, %rdx

    pushq $0x1b
    pushq %r10
    pushq %r11
    pushq $0x23
    pushq %r9
    cmpb $0, x86_user_entry_nested(%rip)
    je 1f
    swapgs
1:
    iretq
.size x86_user_enter_asm, .-x86_user_enter_asm

.section .note.GNU-stack,"",@progbits
