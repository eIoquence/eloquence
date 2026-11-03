#include <stdbool.h>
#include <stdint.h>

#include "io.h"
#include "memory/page_allocator.h"
#include "paging.h"

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
#define USER_PML4_START 256

static uint64_t hhdm_offset;
static bool paging_initialized;
static bool pat_supported;

static bool add_u64(uint64_t left, uint64_t right, uint64_t *sum) {
    if (left > UINT64_MAX - right) {
        return false;
    }

    *sum = left + right;
    return true;
}

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static bool cpu_has_pat(void) {
    uint32_t leaf = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    __asm__ volatile ("cpuid"
                      : "+a"(leaf), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1U << 16)) != 0;
}

static uint64_t *physical_to_hhdm(uint64_t physical_address) {
    uint64_t virtual_address;

    if ((physical_address & PAGE_MASK) != 0
        || (physical_address & ~PAGE_ADDRESS_MASK) != 0
        || !add_u64(hhdm_offset, physical_address, &virtual_address)) {
        return 0;
    }

    return (uint64_t *)(uintptr_t)virtual_address;
}

static bool allocate_table(uint64_t **table_out, uint64_t *physical_out) {
    struct page_frame frame;
    uint64_t physical_address;
    uint64_t *table;

    if (!page_allocator_allocate(&frame)) {
        return false;
    }
    table = frame.virtual_address;
    physical_address = frame.physical_address;
    if ((physical_address & PAGE_MASK) != 0
        || (physical_address & ~PAGE_ADDRESS_MASK) != 0) {
        return false;
    }

    for (uint64_t i = 0; i < PAGE_ENTRY_COUNT; i++) {
        table[i] = 0;
    }
    *table_out = table;
    *physical_out = physical_address;
    return true;
}

static bool address_is_canonical(uint64_t address) {
    return (address >> 48) == ((address & (1ULL << 47)) != 0 ? 0xffff : 0);
}

static bool locate_leaf(struct x86_address_space *space,
                        uint64_t virtual_address, bool create,
                        int user_requirement, uint64_t **entry_out) {
    static const uint8_t shifts[] = {39, 30, 21};
    uint64_t *table;

    if (space == 0 || space->root_virtual_address == 0
        || !address_is_canonical(virtual_address)) {
        return false;
    }
    table = space->root_virtual_address;

    for (uint8_t level = 0; level < sizeof(shifts); level++) {
        uint64_t index = (virtual_address >> shifts[level]) & 0x1ff;
        uint64_t entry = table[index];

        if ((entry & PAGE_PRESENT) == 0) {
            uint64_t *new_table;
            uint64_t physical_address;
            if (!create || !allocate_table(&new_table, &physical_address)) {
                return false;
            }

            table[index] = physical_address | PAGE_PRESENT | PAGE_WRITABLE
                | (user_requirement == 1 ? PAGE_USER : 0);
            entry = table[index];
        } else {
            if ((level != 0 && (entry & PAGE_LARGE) != 0)
                || (entry & PAGE_WRITABLE) == 0
                || (user_requirement == 1 && (entry & PAGE_USER) == 0)
                || (user_requirement == 0 && (entry & PAGE_USER) != 0)) {
                return false;
            }
        }

        table = physical_to_hhdm(entry & PAGE_ADDRESS_MASK);
        if (table == 0) {
            return false;
        }
    }

    *entry_out = &table[(virtual_address >> 12) & 0x1ff];
    return true;
}

bool x86_paging_initialize(uint64_t requested_hhdm_offset) {
    struct x86_address_space current;

    if (paging_initialized || (read_cr4() & CR4_LA57) != 0
        || (read_msr(EFER_MSR) & EFER_NO_EXECUTE_ENABLE) == 0) {
        return false;
    }

    hhdm_offset = requested_hhdm_offset;
    pat_supported = cpu_has_pat();
    current.root_physical_address = read_cr3() & PAGE_ADDRESS_MASK;
    current.root_virtual_address =
        physical_to_hhdm(current.root_physical_address);
    if (current.root_virtual_address == 0) {
        hhdm_offset = 0;
        return false;
    }

    paging_initialized = true;
    return true;
}

uint64_t x86_paging_hhdm_offset(void) {
    return paging_initialized ? hhdm_offset : 0;
}

bool x86_address_space_current(struct x86_address_space *space) {
    if (!paging_initialized || space == 0) {
        return false;
    }

    space->root_physical_address = read_cr3() & PAGE_ADDRESS_MASK;
    space->root_virtual_address =
        physical_to_hhdm(space->root_physical_address);
    return space->root_virtual_address != 0;
}

bool x86_address_space_create_user(struct x86_address_space *space) {
    struct x86_address_space current;
    uint64_t new_root_physical;
    uint64_t *new_root;

    if (!paging_initialized || space == 0
        || !x86_address_space_current(&current)
        || !allocate_table(&new_root, &new_root_physical)) {
        return false;
    }

    for (uint16_t index = USER_PML4_START; index < PAGE_ENTRY_COUNT; index++) {
        /* Keep shared kernel mappings supervisor-only in this address space. */
        new_root[index] = current.root_virtual_address[index] & ~PAGE_USER;
    }

    space->root_physical_address = new_root_physical;
    space->root_virtual_address = new_root;
    return true;
}

void x86_address_space_activate(const struct x86_address_space *space) {
    if (space != 0 && space->root_virtual_address != 0) {
        __asm__ volatile ("mov %0, %%cr3" : : "r"(space->root_physical_address)
                          : "memory");
    }
}

bool x86_address_space_map_page(struct x86_address_space *space,
                                uint64_t virtual_address,
                                uint64_t physical_address, uint32_t flags) {
    uint64_t *entry;
    int user_requirement = (flags & X86_PAGE_USER) != 0 ? 1 : 0;

    if (!paging_initialized || space == 0
        || (virtual_address & PAGE_MASK) != 0
        || (physical_address & PAGE_MASK) != 0
        || (physical_address & ~PAGE_ADDRESS_MASK) != 0
        || (flags & ~(X86_PAGE_WRITABLE | X86_PAGE_USER
                      | X86_PAGE_EXECUTABLE | X86_PAGE_UNCACHED)) != 0
        || ((flags & X86_PAGE_USER) != 0 && (virtual_address >> 47) != 0)
        || !locate_leaf(space, virtual_address, true, user_requirement, &entry)
        || (*entry & PAGE_PRESENT) != 0) {
        return false;
    }

    uint64_t entry_flags = PAGE_PRESENT;
    if ((flags & X86_PAGE_WRITABLE) != 0) {
        entry_flags |= PAGE_WRITABLE;
    }
    if ((flags & X86_PAGE_USER) != 0) {
        entry_flags |= PAGE_USER;
    }
    if ((flags & X86_PAGE_EXECUTABLE) == 0) {
        entry_flags |= PAGE_NO_EXECUTE;
    }
    if ((flags & X86_PAGE_UNCACHED) != 0) {
        entry_flags |= PAGE_CACHE_DISABLE;
        if (pat_supported) {
            entry_flags |= PAGE_WRITE_THROUGH;
        }
    }

    *entry = physical_address | entry_flags;
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)(uintptr_t)virtual_address)
                      : "memory");
    return true;
}

static bool find_leaf(struct x86_address_space *space,
                      uint64_t virtual_address, uint64_t **entry_out) {
    return locate_leaf(space, virtual_address, false, -1, entry_out);
}

bool x86_address_space_unmap_page(struct x86_address_space *space,
                                  uint64_t virtual_address,
                                  uint64_t expected_physical_address) {
    uint64_t *entry;

    if ((virtual_address & PAGE_MASK) != 0
        || (expected_physical_address & PAGE_MASK) != 0
        || !find_leaf(space, virtual_address, &entry)
        || (*entry & PAGE_PRESENT) == 0
        || (*entry & PAGE_ADDRESS_MASK) != expected_physical_address) {
        return false;
    }

    *entry = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"((void *)(uintptr_t)virtual_address)
                      : "memory");
    return true;
}

bool x86_address_space_page_matches(struct x86_address_space *space,
                                    uint64_t virtual_address,
                                    uint64_t physical_address) {
    uint64_t *entry;

    return (virtual_address & PAGE_MASK) == 0
        && (physical_address & PAGE_MASK) == 0
        && find_leaf(space, virtual_address, &entry)
        && (*entry & PAGE_PRESENT) != 0
        && (*entry & PAGE_ADDRESS_MASK) == physical_address;
}

bool x86_virtual_to_physical(uint64_t virtual_address,
                             uint64_t *physical_address) {
    static const uint8_t shifts[] = {39, 30, 21, 12};
    struct x86_address_space current;
    uint64_t *table;

    if (!paging_initialized || physical_address == 0
        || !address_is_canonical(virtual_address)
        || !x86_address_space_current(&current)) {
        return false;
    }

    table = current.root_virtual_address;
    for (uint8_t level = 0; level < sizeof(shifts); level++) {
        uint64_t index = (virtual_address >> shifts[level]) & 0x1ff;
        uint64_t entry = table[index];

        if ((entry & PAGE_PRESENT) == 0) {
            return false;
        }
        if ((level == 1 || level == 2) && (entry & PAGE_LARGE) != 0) {
            uint64_t page_size = 1ULL << shifts[level];
            uint64_t page_base = entry & PAGE_ADDRESS_MASK
                & ~(page_size - 1);
            *physical_address = page_base
                | (virtual_address & (page_size - 1));
            return true;
        }
        if (level == 3) {
            *physical_address = (entry & PAGE_ADDRESS_MASK)
                | (virtual_address & PAGE_MASK);
            return true;
        }

        table = physical_to_hhdm(entry & PAGE_ADDRESS_MASK);
        if (table == 0) {
            return false;
        }
    }

    return false;
}
