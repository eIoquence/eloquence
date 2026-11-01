#include "arch/irq.h"

arch_irq_state_t arch_irq_save(void) {
    arch_irq_state_t daif;
    __asm__ volatile ("mrs %0, daif; msr daifset, #2; isb"
                      : "=r"(daif) : : "memory");
    return daif;
}

void arch_irq_restore(arch_irq_state_t state) {
    __asm__ volatile ("msr daif, %0; isb" : : "r"(state) : "memory");
}

__attribute__((noreturn)) void arch_cpu_halt(void) {
    __asm__ volatile ("msr daifset, #2" : : : "memory");
    for (;;) {
        __asm__ volatile ("wfe" : : : "memory");
    }
}
