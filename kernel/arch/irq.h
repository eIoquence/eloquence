#ifndef ELOQUENCE_ARCH_IRQ_H
#define ELOQUENCE_ARCH_IRQ_H

#include <stdint.h>

typedef uint64_t arch_irq_state_t;

/* Save and mask local interrupts; restore only the saved interrupt state. */
arch_irq_state_t arch_irq_save(void);
void arch_irq_restore(arch_irq_state_t state);
__attribute__((noreturn)) void arch_cpu_halt(void);

#endif
