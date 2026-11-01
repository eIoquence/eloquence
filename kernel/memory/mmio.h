#ifndef ELOQUENCE_MEMORY_MMIO_H
#define ELOQUENCE_MEMORY_MMIO_H

#include <stdint.h>

struct mmio_mapping {
    volatile void *address;
    uint64_t physical_address;
    uint64_t length;
    uint64_t backend_cookie;
};

#endif
