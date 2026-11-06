#include "gfx.h"
#include "eloquence/syscall_abi.h"

static inline uint64_t user_syscall(uint64_t number, uint64_t a0,
                                    uint64_t a1, uint64_t a2) {
    uint64_t result;
    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(a0), "S"(a1), "d"(a2)
                      : "rcx", "r11", "memory");
    return result;
}

static inline uint64_t user_syscall5(uint64_t number, uint64_t a0,
                                     uint64_t a1, uint64_t a2,
                                     uint64_t a3, uint64_t a4) {
    uint64_t result;
    __asm__ volatile (
        "mov %4, %%r10\n\t"
        "mov %5, %%r8\n\t"
        "syscall"
        : "=a"(result)
        : "0"(number), "D"(a0), "S"(a1), "d"(a2), "r"(a3), "r"(a4)
        : "rcx", "r11", "r10", "r8", "memory"
    );
    return result;
}

bool gfx_init(void) {
    return true;
}

bool gfx_get_info(struct gfx_info *info) {
    if (!info) return false;
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_GET_INFO,
                                   (uint64_t)(uintptr_t)info, 0, 0);
    return (uint32_t)(result >> 32) == ELOQUENCE_SYSCALL_RESULT_OK;
}

bool gfx_put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_DRAW_PIXEL,
                                   x, y, color);
    return (uint32_t)(result >> 32) == ELOQUENCE_SYSCALL_RESULT_OK;
}

bool gfx_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                   uint32_t color) {
    uint64_t result = user_syscall5(ELOQUENCE_SYSCALL_GFX_FILL_RECT,
                                    x, y, w, h, color);
    return (uint32_t)(result >> 32) == ELOQUENCE_SYSCALL_RESULT_OK;
}

bool gfx_clear(uint32_t color) {
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_CLEAR, color, 0, 0);
    return (uint32_t)(result >> 32) == ELOQUENCE_SYSCALL_RESULT_OK;
}

bool gfx_flip(void) {
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_FLIP, 0, 0, 0);
    return (uint32_t)(result >> 32) == ELOQUENCE_SYSCALL_RESULT_OK;
}
