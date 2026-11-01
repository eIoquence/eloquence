#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "page_allocator.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)

struct page_allocator_state {
    const struct limine_memmap_response *memory_map;
    uint64_t hhdm_offset;
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

bool page_allocator_initialize(const struct limine_memmap_response *memory_map,
                               uint64_t hhdm_offset) {
    allocator.initialized = false;

    if (memory_map == 0
        || (memory_map->entry_count != 0 && memory_map->entries == 0)) {
        return false;
    }

    for (uint64_t i = 0; i < memory_map->entry_count; i++) {
        const struct limine_memmap_entry *entry = memory_map->entries[i];
        uint64_t end_address;

        if (entry == 0 || !add_u64(entry->base, entry->length, &end_address)) {
            return false;
        }
        if (entry->type == LIMINE_MEMMAP_USABLE
            && end_address > 0
            && hhdm_offset > UINT64_MAX - (end_address - 1)) {
            return false;
        }
    }

    allocator.memory_map = memory_map;
    allocator.hhdm_offset = hhdm_offset;
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

    while (allocator.region_index < allocator.memory_map->entry_count) {
        const struct limine_memmap_entry *entry =
            allocator.memory_map->entries[allocator.region_index];
        uint64_t end_address = entry->base + entry->length;

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            allocator.region_index++;
            allocator.region_started = false;
            continue;
        }

        if (!allocator.region_started) {
            if (!align_up_to_page(entry->base,
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
            uint64_t virtual_address;

            if (!add_u64(allocator.hhdm_offset, physical_address,
                         &virtual_address)) {
                return false;
            }

            allocator.next_physical_address += PAGE_SIZE;
            frame->physical_address = physical_address;
            frame->virtual_address = (void *)(uintptr_t)virtual_address;
            return true;
        }

        allocator.region_index++;
        allocator.region_started = false;
    }

    return false;
}
