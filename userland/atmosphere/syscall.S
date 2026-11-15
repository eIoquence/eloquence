.section .text
.global sys1_wrapper
.type sys1_wrapper, @function
sys1_wrapper:
    /* num in %rdi (1st C arg), a0 in %rsi (2nd C arg) */
    mov %rdi, %rax    /* syscall number → rax */
    mov %rsi, %rdi    /* arg0 → rdi */
    syscall
    ret

.global sys6_wrapper
.type sys6_wrapper, @function
sys6_wrapper:
    /* num in %rdi, a0 in %rsi, a1 in %rdx, a2 in %rcx, a3 in %r8, a4 in %r9 */
    mov %rdi, %rax    /* syscall number → rax */
    mov %rsi, %rdi    /* a0 → rdi */
    mov %rdx, %rsi    /* a1 → rsi */
    mov %rcx, %rdx    /* a2 → rdx */
    mov %r8, %r10     /* a3 → r10 (syscall uses r10, not rcx) */
    mov %r9, %r8      /* a4 → r8 */
    syscall
    ret
