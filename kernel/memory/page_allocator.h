#ifndef ELOQUENCE_PAGE_ALLOCATOR_H
#define ELOQUENCE_PAGE_ALLOCATOR_H

#include <stdbool.h>
#include <stdint.h>

struct page_allocator_region {
    uint64_t base;
    uint64_t length;
    bool usable;
};

struct page_allocator_source {
    const void *context;
    uint64_t region_count;
    bool (*read_region)(const void *context, uint64_t index,
                        struct page_allocator_region *region);
    /* Returns a page-aligned virtual mapping, or NULL when unavailable. */
    void *(*physical_to_virtual)(const void *context,
                                 uint64_t physical_address);
};

struct page_frame {
    uint64_t physical_address;
    void *virtual_address;
    uint32_t allocation_index;
    uint32_t allocation_generation;
};

bool page_allocator_initialize(const struct page_allocator_source *source);
bool page_allocator_allocate(struct page_frame *frame);
bool page_allocator_free(struct page_frame *frame);

#endif
