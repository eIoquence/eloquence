#include "arch/irq.h"

arch_irq_state_t arch_irq_save(void) {
    uint32_t status;
    __asm__ volatile ("di %0; ehb" : "=r"(status) : : "memory");
    return status;
}

void arch_irq_restore(arch_irq_state_t state) {
    if ((state & 1) != 0) {
        __asm__ volatile ("ei; ehb" : : : "memory");
    }
}

__attribute__((noreturn)) void arch_cpu_halt(void) {
    __asm__ volatile ("di; ehb" : : : "memory");
    for (;;) {
        __asm__ volatile ("wait" : : : "memory");
    }
}
