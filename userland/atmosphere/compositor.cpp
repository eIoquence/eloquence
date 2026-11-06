#include "gfx.h"
#include "window.h"
#include "eloquence/syscall_abi.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Minimal C++ runtime support for freestanding compilation */
extern "C" void *__dso_handle;
extern "C" int __cxa_atexit(void (*func)(void *), void *arg, void *dso) {
    (void)func;
    (void)arg;
    (void)dso;
    return 0;
}
extern "C" void __stack_chk_fail(void) {
    for (;;) { __asm__ volatile ("hlt"); }
}

static inline uint64_t user_syscall2(uint64_t number, uint64_t a0,
                                     uint64_t a1) {
    uint64_t result;
    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(a0), "S"(a1)
                      : "rcx", "r11", "memory");
    return result;
}

#define WINDOW_COUNT 3

struct window windows[WINDOW_COUNT];
int32_t mouse_x = 320, mouse_y = 240;

static void draw_desktop_background(const struct gfx_info *info) {
    for (uint64_t y = 0; y < info->height; y += 32) {
        uint8_t b = (uint8_t)(20 + (y * 40 / (info->height ? info->height : 1)));
        gfx_fill_rect(0, y, info->width, 32, gfx_rgb(0x0a, 0x0a, b));
    }
}

static void draw_top_bar(const struct gfx_info *info) {
    gfx_fill_rect(0, 0, info->width, 32, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, 32, info->width, 2, gfx_rgb(0x33, 0x33, 0x33));
    gfx_fill_rect(4, 8, 8U * 8U, 8U, gfx_rgb(0xdd, 0xdd, 0xdd));
}

static void draw_taskbar(const struct gfx_info *info) {
    uint64_t tb_y = info->height - 48;
    gfx_fill_rect(0, tb_y, info->width, 48, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, tb_y, info->width, 2, gfx_rgb(0x33, 0x33, 0x33));
    gfx_fill_rect(4, tb_y + 6, 80, 36, gfx_rgb(0x4c, 0xc9, 0xf0));
    gfx_fill_rect(12, tb_y + 14, 8U * 4U, 8U, gfx_rgb(0x00, 0x00, 0x00));
}

extern "C" void _start(void) {
    struct gfx_info info;
    if (!gfx_get_info(&info)) {
        for (;;) { __asm__ volatile ("hlt"); }
    }

    window_init(&windows[0], 1, 100, 80, 320, 240, "Terminal");
    window_init(&windows[1], 2, 160, 120, 280, 200, "Browser");
    window_init(&windows[2], 3, 220, 160, 240, 180, "Editor");

    for (;;) {
        /* Poll input */
        uint64_t result = user_syscall2(ELOQUENCE_SYSCALL_INPUT_POLL, 0, 0);
        if ((uint32_t)(result >> 32) == ELOQUENCE_SYSCALL_RESULT_OK) {
            uint32_t count = (uint32_t)result;
            for (uint32_t i = 0; i < count; i++) {
                result = user_syscall2(ELOQUENCE_SYSCALL_INPUT_GET, 0, 0);
                if ((uint32_t)(result >> 32)
                    != ELOQUENCE_SYSCALL_RESULT_OK) {
                    break;
                }
                uint64_t packed = result;
                uint8_t type = (uint8_t)(packed >> 56);
                int32_t mx = (int32_t)((packed >> 32) & 0xFFFFFFFFULL);
                int32_t my = (int32_t)(packed & 0xFFFFFFFFULL);
                if (type == 2U) {
                    mouse_x = mx;
                    mouse_y = my;
                }
            }
        }

        /* Draw frame */
        gfx_clear(gfx_rgb(0x0a, 0x0a, 0x0a));
        draw_desktop_background(&info);
        draw_top_bar(&info);
        for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
            window_draw(&windows[i]);
        }
        draw_taskbar(&info);
        gfx_fill_rect((uint64_t)mouse_x, (uint64_t)mouse_y, 8U, 8U,
                      gfx_rgb(0xdd, 0xdd, 0xdd));
        gfx_flip();
    }

    for (;;) { __asm__ volatile ("hlt"); }
}