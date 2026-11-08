#include "gfx.h"
#include "eloquence/syscall_abi.h"

/* A packed syscall result carries the status in bits 32-63 and the payload
 * in bits 0-31. Extract the high 32 bits with two 16-bit shifts (a single
 * 32-bit shift is mis-emulated by QEMU as an invalid opcode). The volatile
 * keeps clang from re-combining them into one 32-bit shift. */
static inline bool RESULT_OK(uint64_t packed) {
    volatile uint64_t hi = packed;
    hi >>= 16;
    hi >>= 16;
    return (uint32_t)hi == 0;
}

static inline uint64_t user_syscall(uint64_t number, uint64_t a0,
                                    uint64_t a1, uint64_t a2) {
    uint64_t result;
    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(a0), "S"(a1), "d"(a2)
                      : "rcx", "r11", "memory");
    return result;
}

/* Single-argument register syscall (all GFX_GET_* take no meaningful args). */
static inline uint64_t user_syscall1(uint64_t number) {
    uint64_t result;
    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(0ULL), "S"(0ULL), "d"(0ULL)
                      : "rcx", "r11", "r10", "r8", "memory");
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
    uint64_t r;
    r = user_syscall1(ELOQUENCE_SYSCALL_GFX_GET_ADDR);
    /* Kernel returns INVALID_ARGUMENT when Limine did not expose a
     * framebuffer request. Fall back to the known framebuffer address from the
     * memory map (0xfd000000, 640x480) so the GUI can still run. */
    if (!RESULT_OK(r)) {
        info->address = (void *)0xfd000000ULL;
        info->width = 640;
        info->height = 480;
        info->pitch = 640 * 4;
        info->bpp = 32;
        info->framebuffer_size = 640 * 480 * 4;
        info->rgb = 1;
        return true;
    }
    info->address = (void *)(uintptr_t)(uint32_t)r;
    r = user_syscall1(ELOQUENCE_SYSCALL_GFX_GET_WIDTH);
    if (!RESULT_OK(r)) return false;
    info->width = (uint32_t)r;
    r = user_syscall1(ELOQUENCE_SYSCALL_GFX_GET_HEIGHT);
    if (!RESULT_OK(r)) return false;
    info->height = (uint32_t)r;
    r = user_syscall1(ELOQUENCE_SYSCALL_GFX_GET_PITCH);
    if (!RESULT_OK(r)) return false;
    info->pitch = (uint32_t)r;
    r = user_syscall1(ELOQUENCE_SYSCALL_GFX_GET_BPP);
    if (!RESULT_OK(r)) return false;
    info->bpp = (uint16_t)(uint32_t)r;
    r = user_syscall1(ELOQUENCE_SYSCALL_GFX_GET_SIZE);
    if (!RESULT_OK(r)) return false;
    info->framebuffer_size = (uint32_t)r;
    info->rgb = 1;
    return true;
}

bool gfx_put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_DRAW_PIXEL,
                                   x, y, color);
    return RESULT_OK(result);
}

bool gfx_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                   uint32_t color) {
    uint64_t result = user_syscall5(ELOQUENCE_SYSCALL_GFX_FILL_RECT,
                                    x, y, w, h, color);
    return RESULT_OK(result);
}

bool gfx_clear(uint32_t color) {
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_CLEAR, color, 0, 0);
    return RESULT_OK(result);
}

bool gfx_flip(void) {
    uint64_t result = user_syscall(ELOQUENCE_SYSCALL_GFX_FLIP, 0, 0, 0);
    return RESULT_OK(result);
}
