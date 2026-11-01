#ifndef ELOQUENCE_PAGE_ALLOCATOR_H
#define ELOQUENCE_PAGE_ALLOCATOR_H

#include <stdbool.h>
#include <stdint.h>

struct limine_memmap_response;

struct page_frame {
    uint64_t physical_address;
    void *virtual_address;
};

bool page_allocator_initialize(const struct limine_memmap_response *memory_map,
                               uint64_t hhdm_offset);
bool page_allocator_allocate(struct page_frame *frame);

#endif
