#include "arch/irq.h"

arch_irq_state_t arch_irq_save(void) {
    arch_irq_state_t status;
    __asm__ volatile ("csrrc %0, sstatus, %1"
                      : "=r"(status) : "r"((arch_irq_state_t)2)
                      : "memory");
    return status;
}

void arch_irq_restore(arch_irq_state_t state) {
    if ((state & 2) != 0) {
        __asm__ volatile ("csrsi sstatus, 2" : : : "memory");
    }
}

__attribute__((noreturn)) void arch_cpu_halt(void) {
    __asm__ volatile ("csrci sstatus, 2" : : : "memory");
    for (;;) {
        __asm__ volatile ("wfi" : : : "memory");
    }
}
