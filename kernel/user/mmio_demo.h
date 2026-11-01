#ifndef ELOQUENCE_USER_MMIO_DEMO_H
#define ELOQUENCE_USER_MMIO_DEMO_H

#include <stdbool.h>
#include <stdint.h>

#include "pci.h"

#define USER_MMIO_CODE_ADDRESS 0x00400000ULL
#define USER_MMIO_STACK_ADDRESS 0x007ff000ULL
#define USER_MMIO_STACK_POINTER 0x007ffff8ULL

bool user_mmio_demo_execute(const struct pci_bar_info *bar, uint64_t size,
                            uint64_t *failure_mask);

#endif
