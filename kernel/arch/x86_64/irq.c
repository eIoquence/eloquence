#include "arch/irq.h"

arch_irq_state_t arch_irq_save(void) {
    arch_irq_state_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void arch_irq_restore(arch_irq_state_t state) {
    if ((state & (1ULL << 9)) != 0) {
        __asm__ volatile ("sti" : : : "memory");
    } else {
        __asm__ volatile ("cli" : : : "memory");
    }
}

__attribute__((noreturn)) void arch_cpu_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
