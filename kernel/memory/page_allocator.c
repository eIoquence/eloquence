#include <stdbool.h>
#include <stdint.h>

#include "page_allocator.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)
#define PAGE_ALLOCATOR_MAX_FRAMES 8192U
#define PAGE_ALLOCATOR_NO_INDEX UINT32_MAX

struct page_allocation_record {
    uint64_t physical_address;
    uintptr_t virtual_address;
    uint32_t generation;
    uint32_t next_free;
    bool allocated;
};

struct page_allocator_state {
    struct page_allocator_source source;
    uint64_t region_index;
    uint64_t next_physical_address;
    uint32_t allocation_count;
    uint32_t free_record_head;
    bool region_started;
    bool initialized;
};

static struct page_allocator_state allocator;
static struct page_allocation_record allocation_records[
    PAGE_ALLOCATOR_MAX_FRAMES];

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
    if (allocator.initialized || source == 0 || source->read_region == 0
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
    allocator.allocation_count = 0;
    allocator.free_record_head = PAGE_ALLOCATOR_NO_INDEX;
    allocator.region_started = false;
    allocator.initialized = true;
    return true;
}

bool page_allocator_allocate(struct page_frame *frame) {
    if (!allocator.initialized || frame == 0) {
        return false;
    }

    uint32_t record_index;
    if (allocator.free_record_head != PAGE_ALLOCATOR_NO_INDEX) {
        record_index = allocator.free_record_head;
        struct page_allocation_record *record =
            &allocation_records[record_index];
        allocator.free_record_head = record->next_free;
        record->next_free = PAGE_ALLOCATOR_NO_INDEX;
        record->allocated = true;
        record->generation++;
        if (record->generation == 0) {
            record->generation = 1;
        }
        frame->physical_address = record->physical_address;
        frame->virtual_address = (void *)record->virtual_address;
        frame->allocation_index = record_index;
        frame->allocation_generation = record->generation;
        for (uint64_t offset = 0; offset < PAGE_SIZE; offset++) {
            ((uint8_t *)frame->virtual_address)[offset] = 0;
        }
        return true;
    }
    if (allocator.allocation_count == PAGE_ALLOCATOR_MAX_FRAMES) {
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

            record_index = allocator.allocation_count++;
            struct page_allocation_record *record =
                &allocation_records[record_index];
            record->physical_address = physical_address;
            record->virtual_address = (uintptr_t)virtual_address;
            record->generation = 1;
            record->next_free = PAGE_ALLOCATOR_NO_INDEX;
            record->allocated = true;
            allocator.next_physical_address += PAGE_SIZE;
            frame->physical_address = physical_address;
            frame->virtual_address = virtual_address;
            frame->allocation_index = record_index;
            frame->allocation_generation = record->generation;
            for (uint64_t offset = 0; offset < PAGE_SIZE; offset++) {
                ((uint8_t *)virtual_address)[offset] = 0;
            }
            return true;
        }

        allocator.region_index++;
        allocator.region_started = false;
    }

    return false;
}

bool page_allocator_free(struct page_frame *frame) {
    if (!allocator.initialized || frame == 0
        || frame->allocation_index >= allocator.allocation_count) {
        return false;
    }

    struct page_allocation_record *record =
        &allocation_records[frame->allocation_index];
    if (!record->allocated
        || record->generation != frame->allocation_generation
        || record->physical_address != frame->physical_address
        || record->virtual_address != (uintptr_t)frame->virtual_address) {
        return false;
    }

    record->allocated = false;
    record->next_free = allocator.free_record_head;
    allocator.free_record_head = frame->allocation_index;
    *frame = (struct page_frame){0};
    return true;
}
