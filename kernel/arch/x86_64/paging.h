#ifndef ELOQUENCE_X86_64_PAGING_H
#define ELOQUENCE_X86_64_PAGING_H

#include <stdbool.h>
#include <stdint.h>

enum x86_page_flags {
    X86_PAGE_WRITABLE = 1U << 0,
    X86_PAGE_USER = 1U << 1,
    X86_PAGE_EXECUTABLE = 1U << 2,
    X86_PAGE_UNCACHED = 1U << 3
};

struct x86_address_space {
    uint64_t root_physical_address;
    uint64_t *root_virtual_address;
};

bool x86_paging_initialize(uint64_t hhdm_offset);
uint64_t x86_paging_hhdm_offset(void);
bool x86_address_space_current(struct x86_address_space *space);
bool x86_address_space_create_user(struct x86_address_space *space);
void x86_address_space_activate(const struct x86_address_space *space);
bool x86_address_space_map_page(struct x86_address_space *space,
                                uint64_t virtual_address,
                                uint64_t physical_address, uint32_t flags);
bool x86_address_space_unmap_page(struct x86_address_space *space,
                                  uint64_t virtual_address,
                                  uint64_t expected_physical_address);
bool x86_address_space_page_matches(struct x86_address_space *space,
                                    uint64_t virtual_address,
                                    uint64_t physical_address);

#endif
