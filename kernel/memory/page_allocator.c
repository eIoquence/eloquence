#include <stdbool.h>
#include <stdint.h>

#include "page_allocator.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)

struct page_allocator_state {
    struct page_allocator_source source;
    uint64_t region_index;
    uint64_t next_physical_address;
    bool region_started;
    bool initialized;
};

static struct page_allocator_state allocator;

static bool add_u64(uint64_t left, uint64_t right, uint64_t *sum) {
    if (left > UINT64_MAX - right) {
        return false;
    }

    *sum = left + right;
    return true;
}

static bool align_up_to_page(uint64_t address, uint64_t *aligned_address) {
    uint64_t remainder = address & PAGE_MASK;
    uint64_t adjustment = remainder == 0 ? 0 : PAGE_SIZE - remainder;

    return add_u64(address, adjustment, aligned_address);
}

bool page_allocator_initialize(const struct page_allocator_source *source) {
    allocator.initialized = false;

    if (source == 0 || source->read_region == 0
        || source->physical_to_virtual == 0) {
        return false;
    }

    for (uint64_t i = 0; i < source->region_count; i++) {
        struct page_allocator_region region;
        uint64_t end_address;

        if (!source->read_region(source->context, i, &region)
            || !add_u64(region.base, region.length, &end_address)) {
            return false;
        }
    }

    allocator.source = *source;
    allocator.region_index = 0;
    allocator.next_physical_address = 0;
    allocator.region_started = false;
    allocator.initialized = true;
    return true;
}

bool page_allocator_allocate(struct page_frame *frame) {
    if (!allocator.initialized || frame == 0) {
        return false;
    }

    while (allocator.region_index < allocator.source.region_count) {
        struct page_allocator_region region;
        uint64_t end_address;

        if (!allocator.source.read_region(allocator.source.context,
                                          allocator.region_index, &region)
            || !add_u64(region.base, region.length, &end_address)) {
            return false;
        }

        if (!region.usable) {
            allocator.region_index++;
            allocator.region_started = false;
            continue;
        }

        if (!allocator.region_started) {
            if (!align_up_to_page(region.base,
                                  &allocator.next_physical_address)) {
                return false;
            }

            if (allocator.next_physical_address < PAGE_SIZE) {
                allocator.next_physical_address = PAGE_SIZE;
            }
            allocator.region_started = true;
        }

        end_address &= ~PAGE_MASK;
        if (allocator.next_physical_address <= end_address
            && end_address - allocator.next_physical_address >= PAGE_SIZE) {
            uint64_t physical_address = allocator.next_physical_address;
            void *virtual_address = allocator.source.physical_to_virtual(
                allocator.source.context, physical_address);
            if (virtual_address == 0
                || ((uintptr_t)virtual_address & PAGE_MASK) != 0) {
                return false;
            }

            allocator.next_physical_address += PAGE_SIZE;
            frame->physical_address = physical_address;
            frame->virtual_address = virtual_address;
            return true;
        }

        allocator.region_index++;
        allocator.region_started = false;
    }

    return false;
}
