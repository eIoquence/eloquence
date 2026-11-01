#include <stdbool.h>
#include <stdint.h>

#include "capability/mmio_capability.h"
#include "domain/domain.h"
#include "syscall.h"
#include "syscall_abi.h"
#include "user_mode.h"

static bool decode_handle(uint64_t packed,
                          struct mmio_capability_handle *handle) {
    if ((packed & 0x00000000ffffff00ULL) != 0 || handle == 0) {
        return false;
    }

    *handle = (struct mmio_capability_handle){
        .generation = (uint32_t)(packed >> 32),
        .slot = (uint8_t)packed
    };
    return true;
}

static uint64_t pack_result(enum mmio_capability_result result,
                            uint32_t value) {
    return ((uint64_t)result << 32) | value;
}

uint64_t x86_syscall_dispatch(const struct x86_syscall_frame *frame) {
    if (frame == 0 || kernel_domain_current_id() == KERNEL_DOMAIN_ID) {
        return pack_result(MMIO_CAPABILITY_ACCESS_DENIED, 0);
    }

    struct mmio_capability_handle handle;
    if (frame->rax != ELOQUENCE_SYSCALL_EXIT
        && !decode_handle(frame->rdi, &handle)) {
        handle = (struct mmio_capability_handle){ .slot = UINT8_MAX };
    }
    switch (frame->rax) {
        case ELOQUENCE_SYSCALL_EXIT:
            x86_user_mode_set_exit_status(frame->rdi);
            kernel_domain_leave();
            return ELOQUENCE_SYSCALL_EXIT_SENTINEL;
        case ELOQUENCE_SYSCALL_MMIO_READ32: {
            uint32_t value = 0;
            enum mmio_capability_result result = mmio_capability_read32(
                handle, frame->rsi, &value);
            return pack_result(result, value);
        }
        case ELOQUENCE_SYSCALL_MMIO_WRITE32:
            return mmio_capability_write32(handle, frame->rsi,
                                           (uint32_t)frame->rdx);
        case ELOQUENCE_SYSCALL_MMIO_REVOKE:
            return mmio_capability_revoke(handle);
        default:
            return pack_result(MMIO_CAPABILITY_INVALID_ARGUMENT, 0);
    }
}
