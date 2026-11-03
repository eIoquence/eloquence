#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/paging.h"
#include "arch/x86_64/syscall_abi.h"
#include "arch/x86_64/user_mode.h"
#include "capability/mmio_capability.h"
#include "domain/domain.h"
#include "memory/page_allocator.h"
#include "mmio.h"
#include "serial.h"
#include "user/mmio_demo.h"

#define PAGE_SIZE 4096ULL
#define DEMO_DOMAIN_ID 1ULL
#define FOREIGN_DOMAIN_ID 2ULL
#define EDU_DEVICE_ID 0x010000edu
#define PROBE_VALUE 0x5a5aa5a5U
#define DEMO_MODE_FOREIGN 0ULL
#define DEMO_MODE_OWNER_READ_WRITE 1ULL
#define DEMO_MODE_FAULT 2ULL
#define DEMO_MODE_OWNER_READ_ONLY 3ULL

extern const uint8_t __user_mmio_demo_start[];
extern const uint8_t __user_mmio_demo_end[];

static uint64_t pack_handle(struct mmio_capability_handle handle) {
    return ((uint64_t)handle.generation << 32) | handle.slot;
}

static void copy_page(uint8_t *destination, const uint8_t *source,
                      size_t length) {
    for (size_t i = 0; i < length; i++) {
        destination[i] = source[i];
    }
}

static void clear_page(uint8_t *page) {
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        page[i] = 0;
    }
}

static bool create_user_mapping(struct x86_address_space *space,
                                struct page_frame *code_frame_out,
                                struct page_frame *stack_frame_out) {
    struct page_frame code_frame = {0};
    struct page_frame stack_frame = {0};
    uint64_t code_size = (uint64_t)(__user_mmio_demo_end
                                    - __user_mmio_demo_start);

    if (space == 0 || code_frame_out == 0 || stack_frame_out == 0) {
        return false;
    }
    *code_frame_out = (struct page_frame){0};
    *stack_frame_out = (struct page_frame){0};
    if (code_size == 0 || code_size > PAGE_SIZE
        || !page_allocator_allocate(&code_frame)) {
        serial_write("  EDU user-domain setup failed: code-page allocation\n");
        return false;
    }
    *code_frame_out = code_frame;
    if (!page_allocator_allocate(&stack_frame)) {
        serial_write("  EDU user-domain setup failed: stack-page allocation\n");
        return false;
    }
    *stack_frame_out = stack_frame;

    if (!x86_address_space_create_user(space)) {
        serial_write("  EDU user-domain setup failed: address-space creation\n");
        return false;
    }

    clear_page(code_frame.virtual_address);
    copy_page(code_frame.virtual_address, __user_mmio_demo_start,
              (size_t)code_size);
    clear_page(stack_frame.virtual_address);
    if (!x86_address_space_map_page(space, USER_MMIO_CODE_ADDRESS,
                                    code_frame.physical_address,
                                    X86_PAGE_USER | X86_PAGE_EXECUTABLE)
        || !x86_address_space_map_page(space, USER_MMIO_STACK_ADDRESS,
                                      stack_frame.physical_address,
                                      X86_PAGE_USER | X86_PAGE_WRITABLE)) {
        serial_write("  EDU user-domain setup failed: code/stack mapping\n");
        return false;
    }

    return true;
}

static void cleanup_capability(struct mmio_capability_handle handle,
                               bool granted) {
    if (granted) {
        (void)mmio_capability_revoke_kernel(handle);
    }
}

static void cleanup_mapping(struct mmio_mapping *mapping) {
    if (mapping->address != 0) {
        (void)x86_mmio_unmap(mapping);
    }
}

static void release_user_frame(struct x86_address_space *space,
                               uint64_t virtual_address,
                               struct page_frame *frame) {
    if (frame->physical_address == 0) {
        return;
    }
    if (space != 0 && space->root_virtual_address != 0
        && x86_address_space_page_matches(space, virtual_address,
                                          frame->physical_address)) {
        (void)x86_address_space_unmap_page(space, virtual_address,
                                          frame->physical_address);
        if (x86_address_space_page_matches(space, virtual_address,
                                           frame->physical_address)) {
            return;
        }
    }
    (void)page_allocator_free(frame);
}

bool user_mmio_demo_execute(const struct pci_bar_info *bar, uint64_t size,
                            uint64_t *failure_mask) {
    struct mmio_mapping mapping = {0};
    struct mmio_capability_handle handle = {0};
    struct x86_address_space owner_space = {0};
    struct x86_address_space foreign_space = {0};
    struct page_frame owner_code_frame = {0};
    struct page_frame owner_stack_frame = {0};
    struct page_frame foreign_code_frame = {0};
    struct page_frame foreign_stack_frame = {0};
    bool capability_granted = false;
    bool ran = false;

    if (bar == 0 || failure_mask == 0 || size < sizeof(uint32_t)
        || kernel_domain_current_id() != KERNEL_DOMAIN_ID
        || !x86_mmio_map_range(bar->base_address, size, &mapping)) {
        goto cleanup;
    }

    if (mmio_capability_grant(
            DEMO_DOMAIN_ID, &mapping,
            MMIO_CAPABILITY_READ32 | MMIO_CAPABILITY_WRITE32, &handle)
        != MMIO_CAPABILITY_OK) {
        goto cleanup;
    }
    capability_granted = true;
    if (!create_user_mapping(&owner_space, &owner_code_frame,
                             &owner_stack_frame)
        || !create_user_mapping(&foreign_space, &foreign_code_frame,
                                &foreign_stack_frame)) {
        serial_write("  EDU user-domain setup failed: isolated mappings\n");
        goto cleanup;
    }

    uint64_t read_write_failures = UINT64_MAX;
    bool read_write_faulted = false;
    bool read_write_ran = x86_user_mode_run(
        &owner_space, USER_MMIO_CODE_ADDRESS, USER_MMIO_STACK_POINTER,
        DEMO_DOMAIN_ID, pack_handle(handle), DEMO_MODE_OWNER_READ_WRITE, 0,
        &read_write_failures, &read_write_faulted);
    if (!read_write_ran) {
        serial_write("  EDU user-domain run failed: owner context did not exit\n");
        goto cleanup;
    }
    if (read_write_faulted) {
        read_write_failures |= ELOQUENCE_USER_TEST_FAULT_TERMINATION;
    }
    capability_granted = (read_write_failures
                          & ELOQUENCE_USER_TEST_REVOKE) != 0;

    if (!x86_mmio_map_range(bar->base_address, size, &mapping)
        || mmio_capability_grant(DEMO_DOMAIN_ID, &mapping,
                                 MMIO_CAPABILITY_READ32, &handle)
            != MMIO_CAPABILITY_OK) {
        goto cleanup;
    }
    capability_granted = true;

    uint64_t foreign_failures = UINT64_MAX;
    bool foreign_faulted = false;
    bool foreign_ran = x86_user_mode_run(
        &foreign_space, USER_MMIO_CODE_ADDRESS, USER_MMIO_STACK_POINTER,
        FOREIGN_DOMAIN_ID, pack_handle(handle), DEMO_MODE_FOREIGN, 0,
        &foreign_failures, &foreign_faulted);
    if (!foreign_ran) {
        goto cleanup;
    }
    if (foreign_faulted) {
        foreign_failures |= ELOQUENCE_USER_TEST_FAULT_TERMINATION;
    }

    uint64_t fault_status = 0;
    bool fault_observed = false;
    bool fault_ran = x86_user_mode_run(
        &foreign_space, USER_MMIO_CODE_ADDRESS, USER_MMIO_STACK_POINTER,
        FOREIGN_DOMAIN_ID, pack_handle(handle), DEMO_MODE_FAULT, 0,
        &fault_status, &fault_observed);
    if (!fault_ran) {
        goto cleanup;
    }
    if (!fault_observed || fault_status != 6) {
        foreign_failures |= ELOQUENCE_USER_TEST_FAULT_TERMINATION;
    }

    uint64_t read_only_failures = UINT64_MAX;
    bool read_only_faulted = false;
    bool read_only_ran = x86_user_mode_run(
        &owner_space, USER_MMIO_CODE_ADDRESS, USER_MMIO_STACK_POINTER,
        DEMO_DOMAIN_ID, pack_handle(handle), DEMO_MODE_OWNER_READ_ONLY, 0,
        &read_only_failures, &read_only_faulted);
    if (!read_only_ran) {
        goto cleanup;
    }
    if (read_only_faulted) {
        read_only_failures |= ELOQUENCE_USER_TEST_FAULT_TERMINATION;
    }

    ran = true;
    *failure_mask = read_write_failures | foreign_failures
        | read_only_failures;
    capability_granted = (read_only_failures
                          & ELOQUENCE_USER_TEST_REVOKE) != 0;

cleanup:
    release_user_frame(&owner_space, USER_MMIO_CODE_ADDRESS,
                       &owner_code_frame);
    release_user_frame(&owner_space, USER_MMIO_STACK_ADDRESS,
                       &owner_stack_frame);
    release_user_frame(&foreign_space, USER_MMIO_CODE_ADDRESS,
                       &foreign_code_frame);
    release_user_frame(&foreign_space, USER_MMIO_STACK_ADDRESS,
                       &foreign_stack_frame);
    cleanup_capability(handle, capability_granted);
    cleanup_mapping(&mapping);
    return ran;
}

static inline __attribute__((always_inline)) uint64_t user_syscall(
    uint64_t number, uint64_t argument0, uint64_t argument1,
    uint64_t argument2) {
    uint64_t result;

    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(argument0), "S"(argument1),
                        "d"(argument2)
                      : "rcx", "r11", "memory");
    return result;
}

__attribute__((used, noinline, section(".user_text")))
void user_mmio_demo_entry(uint64_t handle, uint64_t mode,
                          uint64_t unused_argument) {
    (void)unused_argument;
    uint32_t failures = 0;
    if (mode == DEMO_MODE_FAULT) {
        __asm__ volatile ("ud2");
        for (;;) {
            __asm__ volatile ("pause");
        }
    } else if (mode == DEMO_MODE_FOREIGN) {
        uint64_t result = user_syscall(ELOQUENCE_SYSCALL_MMIO_READ32,
                                       handle, 0, 0);
        if ((uint32_t)(result >> 32) != MMIO_CAPABILITY_ACCESS_DENIED) {
            failures |= ELOQUENCE_USER_TEST_FOREIGN_DENIED;
        }
    } else if (mode == DEMO_MODE_OWNER_READ_WRITE) {
        uint64_t read = user_syscall(ELOQUENCE_SYSCALL_MMIO_READ32,
                                     handle, 0, 0);
        if ((uint32_t)(read >> 32) != MMIO_CAPABILITY_OK
            || (uint32_t)read != EDU_DEVICE_ID) {
            failures |= ELOQUENCE_USER_TEST_READ;
        }

        uint64_t previous_result = user_syscall(
            ELOQUENCE_SYSCALL_MMIO_READ32, handle, 4, 0);
        uint32_t previous = (uint32_t)previous_result;
        uint64_t probe_write = user_syscall(
            ELOQUENCE_SYSCALL_MMIO_WRITE32, handle, 4, PROBE_VALUE);
        uint64_t observed_result = user_syscall(
            ELOQUENCE_SYSCALL_MMIO_READ32, handle, 4, 0);
        uint64_t restore_write = user_syscall(
            ELOQUENCE_SYSCALL_MMIO_WRITE32, handle, 4, ~previous);
        uint64_t restored_result = user_syscall(
            ELOQUENCE_SYSCALL_MMIO_READ32, handle, 4, 0);
        if ((uint32_t)(previous_result >> 32) != MMIO_CAPABILITY_OK
            || (uint32_t)probe_write != MMIO_CAPABILITY_OK
            || (uint32_t)(observed_result >> 32) != MMIO_CAPABILITY_OK
            || (uint32_t)observed_result != ~PROBE_VALUE
            || (uint32_t)restore_write != MMIO_CAPABILITY_OK
            || (uint32_t)(restored_result >> 32) != MMIO_CAPABILITY_OK
            || (uint32_t)restored_result != previous) {
            failures |= ELOQUENCE_USER_TEST_WRITE_LIVENESS;
        }

        uint64_t invalid_syscall = user_syscall(0xffff, 0, 0, 0);
        if ((uint32_t)(invalid_syscall >> 32)
            != MMIO_CAPABILITY_INVALID_ARGUMENT) {
            failures |= ELOQUENCE_USER_TEST_INVALID_SYSCALL;
        }
        uint64_t malformed_handle = user_syscall(
            ELOQUENCE_SYSCALL_MMIO_READ32, 0x100, 0, 0);
        if ((uint32_t)(malformed_handle >> 32)
            != MMIO_CAPABILITY_INVALID_HANDLE) {
            failures |= ELOQUENCE_USER_TEST_MALFORMED_HANDLE;
        }

        uint64_t revoke = user_syscall(ELOQUENCE_SYSCALL_MMIO_REVOKE,
                                       handle, 0, 0);
        if ((uint32_t)revoke != MMIO_CAPABILITY_OK) {
            failures |= ELOQUENCE_USER_TEST_REVOKE;
        }

        uint64_t stale = user_syscall(ELOQUENCE_SYSCALL_MMIO_READ32,
                                      handle, 0, 0);
        if ((uint32_t)(stale >> 32) != MMIO_CAPABILITY_STALE_HANDLE) {
            failures |= ELOQUENCE_USER_TEST_STALE_DENIED;
        }
    } else {
        uint64_t read = user_syscall(ELOQUENCE_SYSCALL_MMIO_READ32,
                                     handle, 0, 0);
        if ((uint32_t)(read >> 32) != MMIO_CAPABILITY_OK
            || (uint32_t)read != EDU_DEVICE_ID) {
            failures |= ELOQUENCE_USER_TEST_READ;
        }

        uint64_t write = user_syscall(ELOQUENCE_SYSCALL_MMIO_WRITE32,
                                      handle, 4, PROBE_VALUE);
        if ((uint32_t)write != MMIO_CAPABILITY_ACCESS_DENIED) {
            failures |= ELOQUENCE_USER_TEST_READ_ONLY_DENIED;
        }

        uint64_t revoke = user_syscall(ELOQUENCE_SYSCALL_MMIO_REVOKE,
                                       handle, 0, 0);
        if ((uint32_t)revoke != MMIO_CAPABILITY_OK) {
            failures |= ELOQUENCE_USER_TEST_REVOKE;
        }

        uint64_t stale = user_syscall(ELOQUENCE_SYSCALL_MMIO_READ32,
                                      handle, 0, 0);
        if ((uint32_t)(stale >> 32) != MMIO_CAPABILITY_STALE_HANDLE) {
            failures |= ELOQUENCE_USER_TEST_STALE_DENIED;
        }
    }

    (void)user_syscall(ELOQUENCE_SYSCALL_EXIT, failures, 0, 0);
    for (;;) {
        __asm__ volatile ("pause");
    }
}
