.section .text
.global x86_irq0_stub
.type x86_irq0_stub, @function
x86_irq0_stub:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %r12
    andq $-16, %rsp
    call x86_timer_interrupt
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
.size x86_irq0_stub, .-x86_irq0_stub

.global x86_spurious_stub
.type x86_spurious_stub, @function
x86_spurious_stub:
    iretq
.size x86_spurious_stub, .-x86_spurious_stub

.section .note.GNU-stack,"",@progbits
