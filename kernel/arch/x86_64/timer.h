#ifndef ELOQUENCE_X86_64_TIMER_H
#define ELOQUENCE_X86_64_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#define X86_TIMER_FREQUENCY_HZ 100U

bool x86_timer_initialize(void);
void x86_timer_interrupt(void);
uint64_t x86_timer_ticks(void);
bool x86_timer_uses_local_apic(void);

#endif
