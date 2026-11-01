#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "arch/mmio.h"
#include "mmio.h"
#include "io.h"
#include "paging.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)
#define PAGE_ADDRESS_MASK 0x000ffffffffff000ULL

#define MMIO_WINDOW_SIZE (64ULL * 1024 * 1024)
#define MMIO_WINDOW_ALIGNMENT (1ULL << 39)
#define MMIO_WINDOW_PAGES (MMIO_WINDOW_SIZE / PAGE_SIZE)
#define MMIO_BITMAP_WORDS (MMIO_WINDOW_PAGES / 64)
#define MMIO_MAX_MAPPINGS 32
#define KERNEL_VIRTUAL_BASE 0xffffffff80000000ULL

struct mmio_slot {
    uint64_t physical_base;
    uint64_t requested_physical_address;
    uint64_t length;
    uint32_t virtual_page;
    uint32_t page_count;
    uint32_t generation;
    bool active;
};

static const struct limine_memmap_response *active_memory_map;
static uint64_t active_hhdm_offset;
static uint64_t mmio_window_base;
static struct x86_address_space mmio_address_space;
static uint64_t mmio_page_bitmap[MMIO_BITMAP_WORDS];
static struct mmio_slot mmio_slots[MMIO_MAX_MAPPINGS];
static bool mmio_initialized;

static bool add_u64(uint64_t left, uint64_t right, uint64_t *sum) {
    if (left > UINT64_MAX - right) {
        return false;
    }

    *sum = left + right;
    return true;
}

static bool align_up(uint64_t value, uint64_t alignment, uint64_t *aligned) {
    uint64_t remainder = value & (alignment - 1);
    uint64_t adjustment = remainder == 0 ? 0 : alignment - remainder;
    return add_u64(value, adjustment, aligned);
}

static bool map_page(uint64_t virtual_address, uint64_t physical_address) {
    return x86_address_space_map_page(&mmio_address_space, virtual_address,
                                      physical_address,
                                      X86_PAGE_WRITABLE | X86_PAGE_UNCACHED);
}

static bool page_matches(uint64_t virtual_address,
                         uint64_t physical_address) {
    return x86_address_space_page_matches(&mmio_address_space, virtual_address,
                                          physical_address);
}

static bool unmap_page(uint64_t virtual_address,
                       uint64_t physical_address) {
    return x86_address_space_unmap_page(&mmio_address_space, virtual_address,
                                        physical_address);
}

static bool type_is_hhdm_mapped(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        case LIMINE_MEMMAP_FRAMEBUFFER:
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        case LIMINE_MEMMAP_ACPI_NVS:
        case LIMINE_MEMMAP_RESERVED_MAPPED:
            return true;
        default:
            return false;
    }
}

static bool find_mmio_window_base(uint64_t *base_out) {
    uint64_t maximum_virtual_end = active_hhdm_offset;

    for (uint64_t i = 0; i < active_memory_map->entry_count; i++) {
        const struct limine_memmap_entry *entry = active_memory_map->entries[i];
        uint64_t physical_end;
        uint64_t mapped_end;
        uint64_t virtual_end;

        if (entry == 0) {
            return false;
        }
        if (!type_is_hhdm_mapped(entry->type) || entry->length == 0) {
            continue;
        }
        if (!add_u64(entry->base, entry->length, &physical_end)
            || !align_up(physical_end, PAGE_SIZE, &mapped_end)
            || !add_u64(active_hhdm_offset, mapped_end, &virtual_end)) {
            return false;
        }
        if (virtual_end > maximum_virtual_end) {
            maximum_virtual_end = virtual_end;
        }
    }

    if (!align_up(maximum_virtual_end, MMIO_WINDOW_ALIGNMENT, base_out)
        || *base_out < 0xffff800000000000ULL
        || (*base_out >> 48) != 0xffff
        || (*base_out & (1ULL << 47)) == 0
        || *base_out >= KERNEL_VIRTUAL_BASE
        || MMIO_WINDOW_SIZE > KERNEL_VIRTUAL_BASE - *base_out) {
        return false;
    }

    return true;
}

static bool overlaps_hhdm_mapping(uint64_t physical_base, uint64_t length) {
    uint64_t physical_end;

    if (!add_u64(physical_base, length, &physical_end)) {
        return true;
    }

    for (uint64_t i = 0; i < active_memory_map->entry_count; i++) {
        const struct limine_memmap_entry *entry = active_memory_map->entries[i];
        uint64_t entry_end;

        if (entry == 0) {
            return true;
        }
        if (!type_is_hhdm_mapped(entry->type) || entry->length == 0) {
            continue;
        }
        if (!add_u64(entry->base, entry->length, &entry_end)) {
            return true;
        }

        uint64_t mapped_base = entry->base & ~PAGE_MASK;
        uint64_t mapped_end;
        if (!add_u64(entry_end, PAGE_MASK, &mapped_end)) {
            return true;
        }
        mapped_end &= ~PAGE_MASK;

        if (physical_base < mapped_end && mapped_base < physical_end) {
            return true;
        }
    }

    return false;
}

static bool bitmap_page_is_set(uint32_t page) {
    return (mmio_page_bitmap[page / 64] & (1ULL << (page % 64))) != 0;
}

static void bitmap_set_range(uint32_t start, uint32_t count, bool value) {
    for (uint32_t page = start; page < start + count; page++) {
        uint64_t mask = 1ULL << (page % 64);
        if (value) {
            mmio_page_bitmap[page / 64] |= mask;
        } else {
            mmio_page_bitmap[page / 64] &= ~mask;
        }
    }
}

static bool find_virtual_run(uint32_t page_count, uint32_t *start_out) {
    if (page_count == 0 || page_count > MMIO_WINDOW_PAGES) {
        return false;
    }

    for (uint32_t start = 0; start <= MMIO_WINDOW_PAGES - page_count; start++) {
        uint32_t page;

        for (page = 0; page < page_count; page++) {
            if (bitmap_page_is_set(start + page)) {
                break;
            }
        }
        if (page == page_count) {
            *start_out = start;
            return true;
        }
        start += page;
    }

    return false;
}

static bool physical_range_is_mapped(uint64_t physical_base,
                                     uint32_t page_count) {
    for (uint8_t i = 0; i < MMIO_MAX_MAPPINGS; i++) {
        const struct mmio_slot *slot = &mmio_slots[i];
        uint64_t candidate_end;
        uint64_t existing_end;

        if (!slot->active) {
            continue;
        }

        candidate_end = physical_base + (uint64_t)page_count * PAGE_SIZE;
        existing_end = slot->physical_base
            + (uint64_t)slot->page_count * PAGE_SIZE;
        if (physical_base < existing_end && slot->physical_base < candidate_end) {
            return true;
        }
    }

    return false;
}

bool x86_mmio_initialize(const struct limine_memmap_response *memory_map,
                         uint64_t hhdm_offset) {
    if (mmio_initialized || memory_map == 0
        || (memory_map->entry_count != 0 && memory_map->entries == 0)
        || x86_paging_hhdm_offset() != hhdm_offset
        || !x86_address_space_current(&mmio_address_space)) {
        return false;
    }

    active_memory_map = memory_map;
    active_hhdm_offset = hhdm_offset;
    if (!find_mmio_window_base(&mmio_window_base)) {
        active_memory_map = 0;
        return false;
    }

    mmio_initialized = true;
    return true;
}

bool x86_mmio_map_range(uint64_t physical_address, uint64_t length,
                        struct mmio_mapping *mapping) {
    uint64_t physical_page;
    uint64_t physical_end;
    uint64_t span;
    uint64_t rounded_span;
    uint64_t virtual_page_address;
    uint32_t page_count;
    uint32_t virtual_page;
    uint8_t slot_index;
    uint8_t free_slot = MMIO_MAX_MAPPINGS;
    uint64_t irq_flags;

    if (!mmio_initialized || mapping == 0 || length == 0
        || !add_u64(physical_address, length, &physical_end)) {
        return false;
    }

    physical_page = physical_address & ~PAGE_MASK;
    span = length + (physical_address - physical_page);
    if (span < length || span > UINT64_MAX - PAGE_MASK) {
        return false;
    }
    rounded_span = (span + PAGE_MASK) & ~PAGE_MASK;
    if (rounded_span == 0 || rounded_span > MMIO_WINDOW_SIZE
        || !add_u64(physical_page, rounded_span, &physical_end)
        || (physical_page & ~PAGE_ADDRESS_MASK) != 0
        || ((physical_end - 1) & ~PAGE_ADDRESS_MASK) != 0
        || physical_page == 0
        || overlaps_hhdm_mapping(physical_page, rounded_span)) {
        return false;
    }

    page_count = (uint32_t)(rounded_span / PAGE_SIZE);
    irq_flags = x86_irq_save();
    if (physical_range_is_mapped(physical_page, page_count)) {
        x86_irq_restore(irq_flags);
        return false;
    }

    for (slot_index = 0; slot_index < MMIO_MAX_MAPPINGS; slot_index++) {
        if (!mmio_slots[slot_index].active) {
            free_slot = slot_index;
            break;
        }
    }
    if (free_slot == MMIO_MAX_MAPPINGS
        || !find_virtual_run(page_count, &virtual_page)) {
        x86_irq_restore(irq_flags);
        return false;
    }

    virtual_page_address = mmio_window_base + (uint64_t)virtual_page * PAGE_SIZE;
    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t virtual_address = virtual_page_address
            + (uint64_t)page * PAGE_SIZE;
        uint64_t mapped_physical = physical_page + (uint64_t)page * PAGE_SIZE;

        if (!map_page(virtual_address, mapped_physical)) {
            for (uint32_t rollback = 0; rollback < page; rollback++) {
                uint64_t rollback_virtual = virtual_page_address
                    + (uint64_t)rollback * PAGE_SIZE;
                uint64_t rollback_physical = physical_page
                    + (uint64_t)rollback * PAGE_SIZE;
                unmap_page(rollback_virtual, rollback_physical);
            }
            x86_irq_restore(irq_flags);
            return false;
        }
    }

    bitmap_set_range(virtual_page, page_count, true);
    struct mmio_slot *slot = &mmio_slots[free_slot];
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    slot->physical_base = physical_page;
    slot->requested_physical_address = physical_address;
    slot->length = length;
    slot->virtual_page = virtual_page;
    slot->page_count = page_count;
    slot->active = true;

    mapping->address = (volatile void *)(uintptr_t)(virtual_page_address
                     + (physical_address - physical_page));
    mapping->physical_address = physical_address;
    mapping->length = length;
    mapping->backend_cookie = ((uint64_t)slot->generation << 8) | free_slot;

    x86_irq_restore(irq_flags);
    return true;
}

bool x86_mmio_unmap(struct mmio_mapping *mapping) {
    uint8_t slot_index;
    uint32_t generation;
    struct mmio_slot *slot;
    uint64_t virtual_base;
    uint64_t irq_flags;

    if (!mmio_initialized || mapping == 0 || mapping->address == 0) {
        return false;
    }

    slot_index = (uint8_t)mapping->backend_cookie;
    generation = (uint32_t)(mapping->backend_cookie >> 8);
    if (slot_index >= MMIO_MAX_MAPPINGS || generation == 0) {
        return false;
    }

    irq_flags = x86_irq_save();
    slot = &mmio_slots[slot_index];
    if (!slot->active || slot->generation != generation
        || slot->length != mapping->length
        || mapping->physical_address != slot->requested_physical_address) {
        x86_irq_restore(irq_flags);
        return false;
    }

    virtual_base = mmio_window_base + (uint64_t)slot->virtual_page * PAGE_SIZE;
    if (mapping->address != (volatile void *)(uintptr_t)(virtual_base
            + (slot->requested_physical_address - slot->physical_base))) {
        x86_irq_restore(irq_flags);
        return false;
    }
    for (uint32_t page = 0; page < slot->page_count; page++) {
        uint64_t virtual_address = virtual_base + (uint64_t)page * PAGE_SIZE;
        uint64_t physical_address = slot->physical_base
            + (uint64_t)page * PAGE_SIZE;

        if (!page_matches(virtual_address, physical_address)) {
            x86_irq_restore(irq_flags);
            return false;
        }
    }

    for (uint32_t page = 0; page < slot->page_count; page++) {
        uint64_t virtual_address = virtual_base + (uint64_t)page * PAGE_SIZE;
        uint64_t physical_address = slot->physical_base
            + (uint64_t)page * PAGE_SIZE;

        unmap_page(virtual_address, physical_address);
    }

    bitmap_set_range(slot->virtual_page, slot->page_count, false);
    slot->active = false;
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    *mapping = (struct mmio_mapping){0};

    x86_irq_restore(irq_flags);
    return true;
}

bool arch_mmio_read32(const struct mmio_mapping *mapping, uint64_t offset,
                      uint32_t *value) {
    if (mapping == 0 || mapping->address == 0 || value == 0
        || (offset & 3) != 0 || offset > mapping->length
        || mapping->length - offset < sizeof(*value)) {
        return false;
    }

    volatile uint32_t *register_address = (volatile uint32_t *)
        ((uintptr_t)mapping->address + offset);
    __asm__ volatile ("" : : : "memory");
    *value = *register_address;
    __asm__ volatile ("" : : : "memory");
    return true;
}

bool arch_mmio_write32(const struct mmio_mapping *mapping, uint64_t offset,
                       uint32_t value) {
    if (mapping == 0 || mapping->address == 0 || (offset & 3) != 0
        || offset > mapping->length
        || mapping->length - offset < sizeof(value)) {
        return false;
    }

    volatile uint32_t *register_address = (volatile uint32_t *)
        ((uintptr_t)mapping->address + offset);
    __asm__ volatile ("" : : : "memory");
    *register_address = value;
    __asm__ volatile ("" : : : "memory");
    return true;
}

bool arch_mmio_unmap(struct mmio_mapping *mapping) {
    return x86_mmio_unmap(mapping);
}
