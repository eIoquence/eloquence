#ifndef ELOQUENCE_ARCH_MMIO_H
#define ELOQUENCE_ARCH_MMIO_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/mmio.h"

/* Backends define device-memory attributes and ordering for their architecture. */
bool arch_mmio_read32(const struct mmio_mapping *mapping, uint64_t offset,
                      uint32_t *value);
bool arch_mmio_write32(const struct mmio_mapping *mapping, uint64_t offset,
                       uint32_t value);
bool arch_mmio_unmap(struct mmio_mapping *mapping);

#endif
