#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

#include "memory/page_allocator.h"
#include "mmio.h"
#include "io.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)
#define PAGE_ENTRY_COUNT 512
#define PAGE_ADDRESS_MASK 0x000ffffffffff000ULL
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_LARGE (1ULL << 7)
#define PAGE_NO_EXECUTE (1ULL << 63)

#define EFER_MSR 0xc0000080U
#define EFER_NO_EXECUTE_ENABLE (1ULL << 11)
#define CR4_LA57 (1ULL << 12)

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
static uint64_t *page_table_root;
static uint64_t mmio_leaf_flags;
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

static uint64_t x86_read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t x86_read_cr4(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static uint64_t x86_read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static bool x86_has_pat(void) {
    uint32_t leaf = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    __asm__ volatile ("cpuid"
                      : "+a"(leaf), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1U << 16)) != 0;
}

static uint64_t *page_table_from_physical(uint64_t physical_address) {
    uint64_t virtual_address;

    if ((physical_address & PAGE_MASK) != 0
        || !add_u64(active_hhdm_offset, physical_address, &virtual_address)) {
        return 0;
    }

    return (uint64_t *)(uintptr_t)virtual_address;
}

static bool page_table_allocate(uint64_t **table_out) {
    struct page_frame frame;
    uint64_t *table;

    if (!page_allocator_allocate(&frame)) {
        return false;
    }

    table = frame.virtual_address;
    for (uint64_t i = 0; i < PAGE_ENTRY_COUNT; i++) {
        table[i] = 0;
    }

    *table_out = table;
    return true;
}

static bool locate_page_entry(uint64_t virtual_address, bool create,
                              uint64_t **entry_out) {
    static const uint8_t shifts[] = {39, 30, 21};
    uint64_t *table = page_table_root;

    for (uint8_t level = 0; level < sizeof(shifts); level++) {
        uint64_t index = (virtual_address >> shifts[level]) & 0x1ff;
        uint64_t entry = table[index];

        if ((entry & PAGE_PRESENT) == 0) {
            uint64_t *new_table;
            if (!create || !page_table_allocate(&new_table)) {
                return false;
            }

            uint64_t physical_address =
                (uint64_t)(uintptr_t)new_table - active_hhdm_offset;
            if ((physical_address & PAGE_MASK) != 0
                || (physical_address & ~PAGE_ADDRESS_MASK) != 0) {
                return false;
            }

            table[index] = physical_address | PAGE_PRESENT | PAGE_WRITABLE;
            entry = table[index];
        } else {
            if ((level != 0 && (entry & PAGE_LARGE) != 0)
                || (entry & PAGE_WRITABLE) == 0
                || (entry & PAGE_USER) != 0) {
                return false;
            }
        }

        table = page_table_from_physical(entry & PAGE_ADDRESS_MASK);
        if (table == 0) {
            return false;
        }
    }

    *entry_out = &table[(virtual_address >> 12) & 0x1ff];
    return true;
}

static bool map_page(uint64_t virtual_address, uint64_t physical_address) {
    uint64_t *entry;

    if (!locate_page_entry(virtual_address, true, &entry)
        || (*entry & PAGE_PRESENT) != 0) {
        return false;
    }

    *entry = (physical_address & PAGE_ADDRESS_MASK) | mmio_leaf_flags;
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)(uintptr_t)virtual_address)
                      : "memory");
    return true;
}

static bool page_matches(uint64_t virtual_address,
                         uint64_t physical_address) {
    uint64_t *entry;

    return locate_page_entry(virtual_address, false, &entry)
        && (*entry & PAGE_PRESENT) != 0
        && (*entry & PAGE_ADDRESS_MASK) == physical_address;
}

static bool unmap_page(uint64_t virtual_address,
                       uint64_t physical_address) {
    uint64_t *entry;

    if (!locate_page_entry(virtual_address, false, &entry)
        || (*entry & PAGE_PRESENT) == 0
        || (*entry & PAGE_ADDRESS_MASK) != physical_address) {
        return false;
    }

    *entry = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)(uintptr_t)virtual_address)
                      : "memory");
    return true;
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
        || (x86_read_cr4() & CR4_LA57) != 0
        || (x86_read_msr(EFER_MSR) & EFER_NO_EXECUTE_ENABLE) == 0) {
        return false;
    }

    active_memory_map = memory_map;
    active_hhdm_offset = hhdm_offset;
    page_table_root = page_table_from_physical(x86_read_cr3()
                                               & PAGE_ADDRESS_MASK);
    if (page_table_root == 0) {
        active_memory_map = 0;
        return false;
    }
    if (!find_mmio_window_base(&mmio_window_base)) {
        active_memory_map = 0;
        page_table_root = 0;
        return false;
    }

    mmio_leaf_flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE
        | PAGE_NO_EXECUTE;
    if (x86_has_pat()) {
        mmio_leaf_flags |= PAGE_WRITE_THROUGH;
    }

    mmio_initialized = true;
    return true;
}

bool x86_mmio_map_range(uint64_t physical_address, uint64_t length,
                        struct x86_mmio_mapping *mapping) {
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
    mapping->generation = slot->generation;
    mapping->slot = free_slot;

    x86_irq_restore(irq_flags);
    return true;
}

bool x86_mmio_unmap(struct x86_mmio_mapping *mapping) {
    uint8_t slot_index;
    struct mmio_slot *slot;
    uint64_t virtual_base;
    uint64_t irq_flags;

    if (!mmio_initialized || mapping == 0
        || mapping->slot >= MMIO_MAX_MAPPINGS) {
        return false;
    }

    irq_flags = x86_irq_save();
    slot_index = mapping->slot;
    slot = &mmio_slots[slot_index];
    if (!slot->active || slot->generation != mapping->generation
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
    *mapping = (struct x86_mmio_mapping){0};

    x86_irq_restore(irq_flags);
    return true;
}
