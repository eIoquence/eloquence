#ifndef ELOQUENCE_X86_64_MMIO_H
#define ELOQUENCE_X86_64_MMIO_H

#include <stdbool.h>
#include <stdint.h>

struct limine_memmap_response;

struct x86_mmio_mapping {
    volatile void *address;
    uint64_t physical_address;
    uint64_t length;
    uint32_t generation;
    uint8_t slot;
};

bool x86_mmio_initialize(const struct limine_memmap_response *memory_map,
                         uint64_t hhdm_offset);
bool x86_mmio_map_range(uint64_t physical_address, uint64_t length,
                        struct x86_mmio_mapping *mapping);
bool x86_mmio_unmap(struct x86_mmio_mapping *mapping);

#endif
