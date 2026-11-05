#include <stdbool.h>
#include <stdint.h>

#include "capability/mmio_capability.h"
#include "domain/domain.h"
#include "serial.h"
#include "timer.h"
#include "system_info.h"
#include "syscall.h"
#include "eloquence/syscall_abi.h"
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

static uint64_t pack_result(uint32_t result,
                            uint32_t value) {
    return ((uint64_t)result << 32) | value;
}

uint64_t x86_syscall_dispatch(const struct x86_syscall_frame *frame) {
    if (frame == 0 || kernel_domain_current_id() == KERNEL_DOMAIN_ID) {
        return pack_result(MMIO_CAPABILITY_ACCESS_DENIED, 0);
    }

    switch (frame->rax) {
        case ELOQUENCE_SYSCALL_EXIT:
            x86_user_mode_set_exit_status(frame->rdi);
            kernel_domain_leave();
            return ELOQUENCE_SYSCALL_EXIT_SENTINEL;
        case ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR:
            return pack_result(
                serial_write_user_char(frame->rdi)
                    ? ELOQUENCE_SYSCALL_RESULT_OK
                    : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                0);
        case ELOQUENCE_SYSCALL_CONSOLE_READ_CHAR: {
            char value;
            if (serial_try_read_char(&value)) {
                return pack_result(ELOQUENCE_SYSCALL_RESULT_OK,
                                   (uint8_t)value);
            }
            return pack_result(ELOQUENCE_SYSCALL_RESULT_WOULD_BLOCK, 0);
        }
        case ELOQUENCE_SYSCALL_CONSOLE_CLEAR:
            serial_write("\033[2J\033[H");
            return pack_result(ELOQUENCE_SYSCALL_RESULT_OK, 0);
        case ELOQUENCE_SYSCALL_UPTIME_TICKS:
            return x86_timer_ticks();
        case ELOQUENCE_SYSCALL_SYSTEM_REPORT:
            return pack_result(kernel_system_report(frame->rdi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_LIST:
            return pack_result(kernel_system_file_list(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_CAT:
            return pack_result(kernel_system_file_cat(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        case ELOQUENCE_SYSCALL_FS_STAT:
            return pack_result(kernel_system_file_stat(frame->rdi, frame->rsi)
                                   ? ELOQUENCE_SYSCALL_RESULT_OK
                                   : ELOQUENCE_SYSCALL_RESULT_INVALID_ARGUMENT,
                               0);
        default:
            break;
    }

    struct mmio_capability_handle handle;
    if (!decode_handle(frame->rdi, &handle)) {
        handle = (struct mmio_capability_handle){ .slot = UINT8_MAX };
    }
    switch (frame->rax) {
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
