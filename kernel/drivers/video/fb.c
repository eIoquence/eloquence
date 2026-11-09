#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <limine.h>

#include "arch/x86_64/paging.h"
#include "drivers/video/fb.h"

#define PIXEL_BPP 32
#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

static struct framebuffer_info g_fb_info;
static bool g_fb_initialized = false;

bool fb_init(void) {
    if (g_fb_initialized) {
        return true;
    }

    if (framebuffer_request.response == 0
        || framebuffer_request.response->framebuffer_count == 0
        || framebuffer_request.response->framebuffers[0] == 0) {
        /* Fallback: Limine didn't expose a framebuffer request (e.g. booting
         * without -vga std). Use the known framebuffer address from the memory
         * map with a standard 640x480 32-bpp mode so user-space GFX syscalls
         * can still draw. */
        g_fb_info.address = (void *)(uintptr_t)(x86_paging_hhdm_offset() + 0xfd000000ULL);
        g_fb_info.width = 640;
        g_fb_info.height = 480;
        g_fb_info.pitch = 640U * 4U;
        g_fb_info.bpp = 32U;
        g_fb_info.rgb = true;
        g_fb_info.framebuffer_size = 640U * 480U * 4U;
        g_fb_initialized = true;
        return true;
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    g_fb_info.address = (void *)(uintptr_t)(x86_paging_hhdm_offset() + (uint64_t)fb->address);
    g_fb_info.width = fb->width;
    g_fb_info.height = fb->height;
    g_fb_info.pitch = fb->pitch;
    g_fb_info.bpp = fb->bpp;
    g_fb_info.rgb = (fb->memory_model == LIMINE_FRAMEBUFFER_RGB);
    g_fb_info.framebuffer_size = (uint32_t)(fb->pitch * fb->height);

    g_fb_initialized = true;
    return true;
}

const struct framebuffer_info *fb_get_info(void) {
    return g_fb_initialized ? &g_fb_info : 0;
}

bool fb_put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (!g_fb_initialized || x >= g_fb_info.width || y >= g_fb_info.height) {
        return false;
    }

    volatile uint8_t *base = g_fb_info.address;
    uint64_t offset = y * g_fb_info.pitch + x * (g_fb_info.bpp / 8U);

    /* Scrie 4 octeți (32 bpp). */
    base[offset + 0] = (uint8_t)color;        /* R */
    base[offset + 1] = (uint8_t)(color >> 8); /* G */
    base[offset + 2] = (uint8_t)(color >> 16); /* B */
    base[offset + 3] = 0x00;                  /* padding */

    return true;
}

bool fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (!g_fb_initialized || x >= g_fb_info.width || y >= g_fb_info.height) {
        return false;
    }

    uint64_t end_x = x + w;
    uint64_t end_y = y + h;
    if (end_x > g_fb_info.width) {
        end_x = g_fb_info.width;
    }
    if (end_y > g_fb_info.height) {
        end_y = g_fb_info.height;
    }

    volatile uint8_t *base = g_fb_info.address;
    uint32_t bpp_bytes = g_fb_info.bpp / 8U;

    for (uint64_t row = y; row < end_y; row++) {
        uint64_t offset = row * g_fb_info.pitch + x * bpp_bytes;
        for (uint64_t col = x; col < end_x; col++) {
            base[offset + 0] = (uint8_t)color;
            base[offset + 1] = (uint8_t)(color >> 8);
            base[offset + 2] = (uint8_t)(color >> 16);
            base[offset + 3] = 0x00;
            offset += bpp_bytes;
        }
    }

    return true;
}

bool fb_clear(uint32_t color) {
    if (!g_fb_initialized) {
        return false;
    }
    return fb_fill_rect(0, 0, g_fb_info.width, g_fb_info.height, color);
}

bool fb_flip(void) {
    /* Framebuffer simplu: nu este nevoie de flip. */
    return g_fb_initialized;
}

/* Desenează un cadru complet al desktop-ului în modul kernel (evită round-trip-uri
 * în userspace între operațiuni de desenare, care ar cauza #UD după sysretq). */
bool kernel_gfx_draw_frame(void) {
    if (!g_fb_initialized) {
        return false;
    }
    if (!g_fb_initialized) {
        return false;
    }

    /* Background gradient */
    for (uint64_t y = 0; y < g_fb_info.height; y += 8) {
        uint8_t b = (uint8_t)(10 + (y * 50 / (g_fb_info.height ? g_fb_info.height : 1)));
        fb_fill_rect(0, y, g_fb_info.width, 8, RGB(0x05, 0x07, b));
    }

    /* Top bar */
    fb_fill_rect(0, 0, g_fb_info.width, 40, RGB(0x1a, 0x1a, 0x2a));
    fb_fill_rect(0, 40, g_fb_info.width, 2, RGB(0x2a, 0x2a, 0x3a));
    /* Time and date placeholder */
    fb_fill_rect(20, 10, 60, 8, RGB(0x2a, 0x2a, 0x3a));
    fb_fill_rect(20, 24, 60, 8, RGB(0x2a, 0x2a, 0x3a));

    /* Dock (empty placeholders) */
    uint64_t dock_w = 120, dock_x = 20, dock_y = 76, cell = 48;
    fb_fill_rect(dock_x, dock_y, dock_w, 200, RGB(0x1a, 0x1a, 0x22));
    fb_fill_rect(dock_x, dock_y, dock_w, 2, RGB(0x3a, 0x3a, 0x3a));

    for (uint32_t i = 0; i < 4; i++) {
        uint32_t col = i % 2, row = i / 2;
        uint64_t cx = dock_x + 24 + col * cell;
        uint64_t cy = dock_y + 30 + row * cell;
        fb_fill_rect(cx - 12, cy - 12, 24, 24, RGB(0x2a, 0x2a, 0x32));
        fb_fill_rect(cx - 8, cy - 8, 16, 16, RGB(0x1a, 0x1a, 0x1a));
    }

    /* Music widget */
    uint64_t my = dock_y + 200;
    fb_fill_rect(dock_x, my, dock_w, 64, RGB(0x1a, 0x1a, 0x22));
    fb_fill_rect(dock_x + 28, my + 32, 16, 16, RGB(0x1d, 0xb9, 0x54));
    fb_fill_rect(dock_x + 52, my + 14, 64, 8, RGB(0x2a, 0x2a, 0x3a));
    fb_fill_rect(dock_x + 52, my + 34, 64, 8, RGB(0x2a, 0x2a, 0x3a));

    /* Notifications */
    uint64_t ny = my + 76;
    fb_fill_rect(dock_x, ny, dock_w, 40, RGB(0x1a, 0x1a, 0x22));
    fb_fill_rect(dock_x + 16, ny + 10, 140, 8, RGB(0x2a, 0x2a, 0x3a));
    fb_fill_rect(dock_x + 78, ny + 20, 10, 10, RGB(0x55, 0x55, 0x55));

    /* Taskbar */
    uint64_t tb_y = g_fb_info.height - 44;
    fb_fill_rect(0, tb_y, g_fb_info.width, 44, RGB(0x1a, 0x1a, 0x1a));
    fb_fill_rect(0, tb_y, g_fb_info.width, 2, RGB(0x2a, 0x2a, 0x3a));
    fb_fill_rect(14, tb_y + 10, 24, 24, RGB(0x2a, 0x2a, 0x3a));
    fb_fill_rect(26, tb_y + 22, 10, 10, RGB(0xee, 0xee, 0xee));
    fb_fill_rect(g_fb_info.width - 130, tb_y + 22, 10, 10, RGB(0x88, 0x88, 0x88));
    fb_fill_rect(g_fb_info.width - 110, tb_y + 10, 40, 8, RGB(0x2a, 0x2a, 0x3a));

    /* Mouse cursor (centered) */
    fb_fill_rect(g_fb_info.width / 2 - 4, g_fb_info.height / 2 - 4, 8, 8,
                  RGB(0xee, 0xee, 0xee));

    return fb_flip();
}
