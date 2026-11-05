#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/paging.h"
#include "arch/x86_64/user_mode.h"
#include "domain/domain.h"
#include "process/process.h"
#include "memory/page_allocator.h"
#include "arch/x86_64/timer.h"
#include "elf_loader.h"

#define ELF64_HEADER_SIZE 64U
#define ELF64_PROGRAM_HEADER_SIZE 56U
#define ELF64_MAX_PROGRAM_HEADERS 8U
#define ELF64_PT_LOAD 1U
#define ELF64_PT_DYNAMIC 2U
#define ELF64_PT_INTERP 3U
#define ELF64_PF_EXECUTE 1U
#define ELF64_PF_WRITE 2U
#define ELF64_PF_READ 4U

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)
#define USER_MIN_ADDRESS 0x0000000000010000ULL
#define USER_STACK_BASE 0x00007fffffffd000ULL
#define USER_STACK_POINTER 0x00007fffffffeff8ULL
#define USER_STACK_PAGE_COUNT 2U
#define USER_PROCESS_MAX_PAGES 16U
#define USER_PROCESS_DOMAIN_FIRST 3ULL
#define USER_PROCESS_KERNEL_STACK_SIZE 16384U

struct user_process_page {
    struct page_frame frame;
    uint64_t virtual_address;
};

struct user_process_state {
    struct x86_address_space address_space;
    struct user_process_page pages[USER_PROCESS_MAX_PAGES];
    uint8_t kernel_stack[USER_PROCESS_KERNEL_STACK_SIZE] __attribute__((aligned(16)));
    uint32_t page_count;
    bool address_space_ready;
    bool poisoned;
    bool in_use;
};

static struct user_process_state process_states[KERNEL_PROCESS_MAX];
static struct user_process_state *active_process_state;
static uint64_t next_process_id = USER_PROCESS_DOMAIN_FIRST;

#define process_state (*active_process_state)

static uint16_t read_u16_le(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16)
        | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *bytes) {
    return (uint64_t)read_u32_le(bytes)
        | ((uint64_t)read_u32_le(bytes + 4) << 32);
}

static bool power_of_two(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

static bool align_up_to_page(uint64_t address, uint64_t *aligned) {
    if (aligned == 0 || address > UINT64_MAX - PAGE_MASK) {
        return false;
    }
    *aligned = (address + PAGE_MASK) & ~PAGE_MASK;
    return true;
}

static void clear_page(uint8_t *page) {
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        page[i] = 0;
    }
}

static bool allocate_process_page(uint64_t virtual_address,
                                  uint32_t mapping_flags) {
    struct page_frame frame = {0};

    if (process_state.page_count == USER_PROCESS_MAX_PAGES
        || !page_allocator_allocate(&frame)) {
        return false;
    }
    clear_page(frame.virtual_address);
    if (!x86_address_space_map_page(&process_state.address_space,
                                    virtual_address,
                                    frame.physical_address,
                                    mapping_flags)) {
        (void)page_allocator_free(&frame);
        return false;
    }

    struct user_process_page *page =
        &process_state.pages[process_state.page_count++];
    page->frame = frame;
    page->virtual_address = virtual_address;
    return true;
}

static bool load_segment(const uint8_t *image, size_t image_size,
                         uint64_t file_offset, uint64_t virtual_address,
                         uint64_t file_size, uint64_t memory_size,
                         uint32_t elf_flags) {
    uint64_t segment_end;
    uint64_t page_start;
    uint64_t page_end;
    uint64_t page_flags = X86_PAGE_USER;

    if ((elf_flags & ELF64_PF_READ) == 0
        || (elf_flags & ~(ELF64_PF_READ | ELF64_PF_WRITE
                          | ELF64_PF_EXECUTE)) != 0
        || ((elf_flags & ELF64_PF_WRITE) != 0
            && (elf_flags & ELF64_PF_EXECUTE) != 0)
        || file_size > memory_size || memory_size == 0
        || file_offset > image_size || file_size > image_size - file_offset
        || virtual_address < USER_MIN_ADDRESS
        || virtual_address > UINT64_MAX - memory_size) {
        return false;
    }

    segment_end = virtual_address + memory_size;
    if (segment_end > USER_STACK_BASE
        || !align_up_to_page(segment_end, &page_end)) {
        return false;
    }
    page_start = virtual_address & ~PAGE_MASK;
    if ((elf_flags & ELF64_PF_WRITE) != 0) {
        page_flags |= X86_PAGE_WRITABLE;
    }
    if ((elf_flags & ELF64_PF_EXECUTE) != 0) {
        page_flags |= X86_PAGE_EXECUTABLE;
    }

    for (uint64_t page_address = page_start; page_address < page_end;
         page_address += PAGE_SIZE) {
        if (!allocate_process_page(page_address, (uint32_t)page_flags)) {
            return false;
        }

        struct user_process_page *page =
            &process_state.pages[process_state.page_count - 1];
        uint64_t copy_start = virtual_address > page_address
            ? virtual_address : page_address;
        uint64_t file_end = virtual_address + file_size;
        uint64_t page_limit = page_address + PAGE_SIZE;
        uint64_t copy_end = file_end < page_limit ? file_end : page_limit;
        if (copy_start < copy_end) {
            uint64_t source_offset = file_offset
                + copy_start - virtual_address;
            uint64_t destination_offset = copy_start - page_address;
            uint64_t copy_size = copy_end - copy_start;
            for (uint64_t i = 0; i < copy_size; i++) {
                ((uint8_t *)page->frame.virtual_address)
                    [destination_offset + i] = image[source_offset + i];
            }
        }
    }
    return true;
}

static bool parse_and_load_image(const uint8_t *image, size_t image_size,
                                 uint64_t *entry_out) {
    uint64_t program_offset;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint64_t entry;
    bool loaded_segment = false;
    bool entry_is_executable = false;

    if (image_size < ELF64_HEADER_SIZE || image[0] != 0x7f
        || image[1] != 'E' || image[2] != 'L' || image[3] != 'F'
        || image[4] != 2 || image[5] != 1 || image[6] != 1
        || read_u16_le(image + 16) != 2
        || read_u16_le(image + 18) != 62
        || read_u32_le(image + 20) != 1
        || read_u16_le(image + 52) != ELF64_HEADER_SIZE) {
        return false;
    }

    entry = read_u64_le(image + 24);
    program_offset = read_u64_le(image + 32);
    program_entry_size = read_u16_le(image + 54);
    program_count = read_u16_le(image + 56);
    if (entry < USER_MIN_ADDRESS || entry >= USER_STACK_BASE
        || program_entry_size != ELF64_PROGRAM_HEADER_SIZE
        || program_count == 0
        || program_count > ELF64_MAX_PROGRAM_HEADERS
        || program_offset > image_size
        || program_count > (image_size - program_offset)
            / ELF64_PROGRAM_HEADER_SIZE) {
        return false;
    }

    for (uint16_t i = 0; i < program_count; i++) {
        const uint8_t *program = image + program_offset
            + (uint64_t)i * ELF64_PROGRAM_HEADER_SIZE;
        uint32_t type = read_u32_le(program);
        uint32_t flags = read_u32_le(program + 4);
        uint64_t file_offset = read_u64_le(program + 8);
        uint64_t virtual_address = read_u64_le(program + 16);
        uint64_t file_size = read_u64_le(program + 32);
        uint64_t memory_size = read_u64_le(program + 40);
        uint64_t alignment = read_u64_le(program + 48);

        if (type == ELF64_PT_DYNAMIC || type == ELF64_PT_INTERP) {
            return false;
        }
        if (type != ELF64_PT_LOAD) {
            continue;
        }
        if ((alignment > 1
             && (!power_of_two(alignment)
                 || file_offset % alignment != virtual_address % alignment))
            || !load_segment(image, image_size, file_offset,
                             virtual_address, file_size, memory_size, flags)) {
            return false;
        }
        loaded_segment = true;
        if ((flags & ELF64_PF_EXECUTE) != 0
            && entry >= virtual_address
            && entry - virtual_address < memory_size) {
            entry_is_executable = true;
        }
    }

    if (!loaded_segment || !entry_is_executable) {
        return false;
    }
    *entry_out = entry;
    return true;
}

static bool initialize_address_space(void) {
    if (process_state.address_space_ready) {
        return true;
    }
    if (!x86_address_space_create_user(&process_state.address_space)) {
        return false;
    }
    process_state.address_space_ready = true;
    return true;
}

static bool map_user_stack(void) {
    for (uint32_t page = 0; page < USER_STACK_PAGE_COUNT; page++) {
        uint64_t virtual_address = USER_STACK_BASE
            + (uint64_t)page * PAGE_SIZE;
        if (!allocate_process_page(virtual_address,
                                   X86_PAGE_USER | X86_PAGE_WRITABLE)) {
            return false;
        }
    }
    return true;
}

static bool cleanup_process_pages(void) {
    bool cleanup_ok = true;

    while (process_state.page_count != 0) {
        struct user_process_page *page =
            &process_state.pages[process_state.page_count - 1];
        if (!x86_address_space_unmap_page(&process_state.address_space,
                                          page->virtual_address,
                                          page->frame.physical_address)) {
            cleanup_ok = false;
            break;
        }
        if (!page_allocator_free(&page->frame)) {
            cleanup_ok = false;
            break;
        }
        *page = (struct user_process_page){0};
        process_state.page_count--;
    }
    if (!cleanup_ok) {
        process_state.poisoned = true;
    }
    return cleanup_ok;
}

bool x86_user_elf_run(const void *image, size_t image_size,
                      uint64_t *exit_status, bool *faulted) {
    uint64_t entry;
    uint64_t process_id;
    uint64_t parent_id;
    bool ran;
    bool cleanup_ok;
    bool recorded;
    struct user_process_state *previous_state = active_process_state;
    struct user_process_state *selected_state = 0;

    if (image == 0 || image_size == 0 || exit_status == 0 || faulted == 0
        || next_process_id == UINT64_MAX) {
        return false;
    }

    for (uint32_t i = 0; i < KERNEL_PROCESS_MAX; i++) {
        if (!process_states[i].in_use && !process_states[i].poisoned) {
            selected_state = &process_states[i];
            break;
        }
    }
    if (selected_state == 0) {
        return false;
    }
    selected_state->in_use = true;
    active_process_state = selected_state;
    if (process_state.page_count != 0 || !initialize_address_space()) {
        process_state.in_use = false;
        active_process_state = previous_state;
        return false;
    }

    if (!parse_and_load_image(image, image_size, &entry)
        || !map_user_stack()) {
        (void)cleanup_process_pages();
        process_state.in_use = false;
        active_process_state = previous_state;
        return false;
    }

    process_id = next_process_id++;
    parent_id = kernel_domain_current_id();
    if (kernel_process_start(process_id, parent_id, x86_timer_ticks()) == 0) {
        (void)cleanup_process_pages();
        process_state.in_use = false;
        active_process_state = previous_state;
        return false;
    }
    ran = x86_user_mode_run_with_kernel_stack(
        &process_state.address_space, entry, USER_STACK_POINTER, process_id,
        0, 0, 0,
        (uint64_t)(uintptr_t)(process_state.kernel_stack
                              + sizeof(process_state.kernel_stack)),
        exit_status, faulted);
    if (!ran) {
        *exit_status = UINT64_MAX;
        *faulted = true;
    }
    recorded = kernel_process_finish(process_id, x86_timer_ticks(),
                                     *exit_status, *faulted);
    cleanup_ok = cleanup_process_pages();
    process_state.in_use = false;
    active_process_state = previous_state;
    return ran && recorded && cleanup_ok;
}
