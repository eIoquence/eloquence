#include "gfx.h"
#include "window.h"
#include "font.h"
#include "ui.h"
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
    for (;;) {}
}

static inline uint64_t sys2(uint64_t number, uint64_t a0, uint64_t a1) {
    uint64_t result;
    __asm__ volatile ("syscall"
                      : "=a"(result)
                      : "0"(number), "D"(a0), "S"(a1)
                      : "rcx", "r11", "memory");
    return result;
}

/* RESULT_OK: true when the high 32 bits (syscall status) are 0, i.e. OK.
 * Implemented with two volatile 16-bit shifts so clang cannot re-combine them
 * into a single 32-bit shift that QEMU mis-emulates as #UD. */
static inline bool RESULT_OK(uint64_t packed) {
    volatile uint64_t v = packed;
    v >>= 16;
    v >>= 16;
    return (uint32_t)v == 0;
}

static void serial_write_char(char c) {
    sys2(ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR, (uint64_t)(uint8_t)c, 0);
}
static void serial_write(const char *s) {
    while (*s) { serial_write_char(*s++); }
}

/* ------------------------------------------------------------------ UI state */

enum UIState {
    UI_DESKTOP,
    UI_SEARCH,
    UI_WIFI,
};

static enum UIState state = UI_DESKTOP;

struct Rect { uint64_t x, y, w, h; };
static inline bool rect_contains(const Rect *r, int32_t x, int32_t y) {
    return x >= (int32_t)r->x && x < (int32_t)(r->x + r->w)
        && y >= (int32_t)r->y && y < (int32_t)(r->y + r->h);
}

/* Input */
static int32_t mouse_x = 320, mouse_y = 240;
static bool clicked = false;
static int32_t click_x = 0, click_y = 0;

/* Search input */
static char search_buf[61];
static uint32_t search_len = 0;
static uint32_t blink = 0;
#define SEARCH_MAX 60

/* Screen size */
static uint64_t g_screen_w = 640, g_screen_h = 480;

/* ------------------------------------------------------------------ Apps */

struct App { const char *name; uint32_t color; };
static const App dock_apps[] = {
    {"MUSIC", 0x1db954},
    {"CHROME", 0x4285f4},
    {"FIGMA", 0xf24e1e},
    {"TERM", 0xffffff},
    {"FILE", 0x4c9af0},
    {"SET", 0x9aa0a6},
};
#define DOCK_COUNT (sizeof(dock_apps) / sizeof(dock_apps[0]))

static const App search_apps[] = {
    {"INSTALL", 0xffffff},
    {"CHROME", 0x4285f4},
    {"FIGMA", 0xf24e1e},
};
#define SEARCH_COUNT (sizeof(search_apps) / sizeof(search_apps[0]))

struct WifiNet { const char *name; bool locked; };
static const WifiNet wifi_nets[] = {
    {"HOMENET-5G", false},
    {"OFFICE WIFI", true},
    {"CAFE NET", true},
    {"LIBRARY WIFI", true},
    {"GUESTNET-5G", true},
};
#define WIFI_COUNT (sizeof(wifi_nets) / sizeof(wifi_nets[0]))

/* ------------------------------------------------------------------ Scancodes */

static bool shift_down = false;

static bool scancode_key(uint8_t sc, char *out) {
    if (sc >= 0x02 && sc <= 0x0A) {
        *out = (sc == 0x0A) ? '0' : (char)('1' + (sc - 0x02));
        return true;
    }
    if (sc >= 0x1E && sc <= 0x2C) {
        char c = (char)('a' + (sc - 0x1E));
        *out = shift_down ? (char)(c - 32) : c;
        return true;
    }
    switch (sc) {
        case 0x39: *out = ' '; return true;
        case 0x2A: shift_down = true; return false;
        case 0xAA: shift_down = false; return false;
        default: return false;
    }
}

/* ------------------------------------------------------------------ Drawing */

static void draw_desktop_background(const struct gfx_info *info) {
    for (uint64_t y = 0; y < info->height; y += 32) {
        uint8_t b = (uint8_t)(10 + (y * 50 / (info->height ? info->height : 1)));
        gfx_fill_rect(0, y, info->width, 32, gfx_rgb(0x07, 0x07, b));
    }
}

static void draw_top_bar(const struct gfx_info *info) {
    gfx_fill_rect(0, 0, info->width, 40, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, 40, info->width, 2, gfx_rgb(0x2a, 0x2a, 0x2a));
    font_draw_text(20, 10, "11:20 PM", gfx_rgb(0xee, 0xee, 0xee));
    font_draw_text(20, 24, "FRI 14", gfx_rgb(0x88, 0x88, 0x88));
}

static void draw_app_icon(uint64_t cx, uint64_t cy, uint64_t r,
                          uint32_t color, bool hover) {
    uint32_t c = hover ? gfx_rgb(
        (uint8_t)((color >> 16) & 0xff) * 9 / 10,
        (uint8_t)((color >> 8) & 0xff) * 9 / 10,
        (uint8_t)(color & 0xff) * 9 / 10) : color;
    ui_draw_app_icon(cx, cy, r, c);
}

static void draw_dock(void) {
    uint64_t dock_w = 100;
    uint64_t dock_x = 16;
    uint64_t dock_y = 76;
    uint64_t cell = 46;
    gfx_fill_rect(dock_x, dock_y, dock_w, 240, gfx_rgb(0x14, 0x14, 0x1a));
    gfx_fill_rect(dock_x, dock_y, dock_w, 2, gfx_rgb(0x2a, 0x2a, 0x2a));

    for (uint32_t i = 0; i < DOCK_COUNT; i++) {
        uint32_t col = i % 2;
        uint32_t row = i / 2;
        uint64_t cx = dock_x + 24 + col * cell;
        uint64_t cy = dock_y + 30 + row * cell;
        Rect r = { cx - 18, cy - 18, 36, 36 };
        bool hover = rect_contains(&r, mouse_x, mouse_y);
        draw_app_icon(cx, cy, 16, dock_apps[i].color, hover);
        font_draw_text(cx - 10, cy + 22, dock_apps[i].name,
                       gfx_rgb(0xbb, 0xbb, 0xbb));
    }

    /* Music widget */
    uint64_t my = dock_y + 200;
    gfx_fill_rect(dock_x, my, dock_w, 64, gfx_rgb(0x1a, 0x1a, 0x22));
    ui_draw_app_icon(dock_x + 28, my + 32, 14, gfx_rgb(0x1d, 0xb9, 0x54));
    font_draw_text(dock_x + 52, my + 14, "MOMENT", gfx_rgb(0xee, 0xee, 0xee));
    font_draw_text(dock_x + 52, my + 30, "NOT FOR RADIO",
                   gfx_rgb(0x77, 0x77, 0x77));

    /* Notifications */
    uint64_t ny = my + 76;
    gfx_fill_rect(dock_x, ny, dock_w, 40, gfx_rgb(0x14, 0x14, 0x1a));
    font_draw_text(dock_x + 16, ny + 12, "NOTIFICATIONS",
                   gfx_rgb(0xbb, 0xbb, 0xbb));
    ui_draw_circle(dock_x + 78, ny + 12, 6, gfx_rgb(0x55, 0x55, 0x55));
}

static void draw_taskbar(const struct gfx_info *info) {
    uint64_t tb_y = info->height - 44;
    gfx_fill_rect(0, tb_y, info->width, 44, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, tb_y, info->width, 2, gfx_rgb(0x2a, 0x2a, 0x2a));

    /* Left: logo */
    gfx_fill_rect(14, tb_y + 10, 24, 24, gfx_rgb(0x2a, 0x2a, 0x2a));
    ui_draw_circle(26, tb_y + 22, 10, gfx_rgb(0xdd, 0xdd, 0xdd));

    /* Right: status icons */
    Rect wifi_btn = { info->width >= 160 ? info->width - 160 : 0,
                      tb_y, 140, 44 };
    if (rect_contains(&wifi_btn, mouse_x, mouse_y)) {
        gfx_fill_rect(wifi_btn.x, wifi_btn.y, wifi_btn.w, wifi_btn.h,
                      gfx_rgb(0x22, 0x22, 0x2a));
    }
    ui_draw_wifi(info->width - 130, tb_y + 22, gfx_rgb(0x88, 0x88, 0x88));
    font_draw_text(info->width - 110, tb_y + 10, "WiFi",
                   gfx_rgb(0xbb, 0xbb, 0xbb));

    uint64_t bx = info->width - 60;
    gfx_fill_rect(bx, tb_y + 8, 32, 28, gfx_rgb(0x2a, 0x2a, 0x2a));
    gfx_fill_rect(bx + 6, tb_y + 14, 16, 16, gfx_rgb(0x4c, 0xc9, 0xf0));
}

/* ------------------------------------------------------------------ Panels */

static void draw_search_panel(const struct gfx_info *info) {
    uint64_t pw = 520, ph = 340;
    uint64_t px = (info->width - pw) / 2;
    uint64_t py = (info->height - ph) / 2;

    gfx_fill_rect(0, 0, info->width, info->height, gfx_rgb(0x00, 0x00, 0x00));
    for (uint64_t y = 0; y < info->height; y += 4) {
        gfx_fill_rect(0, y, info->width, 4, gfx_rgb(0x05, 0x05, 0x07));
    }

    ui_fill_round_rect(px, py, pw, ph, 20, gfx_rgb(0x22, 0x22, 0x2a));
    gfx_fill_rect(px, py, pw, 2, gfx_rgb(0x3a, 0x3a, 0x4a));

    /* Search field */
    uint64_t sf_x = px + 40, sf_y = py + 52, sf_w = pw - 80, sf_h = 44;
    ui_fill_round_rect(sf_x, sf_y, sf_w, sf_h, 10, gfx_rgb(0x2a, 0x2a, 0x34));
    ui_draw_search_glass(sf_x + 18, sf_y + sf_h / 2, 10,
                         gfx_rgb(0x66, 0x66, 0x66));
    font_draw_text(sf_x + 52, sf_y + 14, "START TYPING TO SEARCH",
                   gfx_rgb(0x88, 0x88, 0x88));

    if (search_len > 0) {
        char tmp[SEARCH_MAX + 2];
        for (uint32_t k = 0; k < search_len; k++) {
            tmp[k] = search_buf[k];
        }
        tmp[search_len] = '\0';
        font_draw_text(sf_x + 52, sf_y + 14, tmp, gfx_rgb(0xee, 0xee, 0xee));
        if ((blink / 30) % 2 == 0) {
            uint64_t tw = font_text_width(tmp);
            ui_draw_text_caret(sf_x + 52 + tw, sf_y + 10, 24,
                               gfx_rgb(0xee, 0xee, 0xee));
        }
    }

    /* Results */
    uint64_t ry = py + 130;
    for (uint32_t i = 0; i < SEARCH_COUNT; i++) {
        uint64_t ry_i = ry + i * 62;
        Rect r = { px + 40, ry_i, pw - 80, 52 };
        bool hover = rect_contains(&r, mouse_x, mouse_y);
        if (hover) {
            gfx_fill_rect(px + 40, ry_i, pw - 80, 52,
                          gfx_rgb(0x2a, 0x2a, 0x34));
        }
        ui_draw_app_icon(px + 62, ry_i + 26, 18, search_apps[i].color);
        font_draw_text(px + 96, ry_i + 14, search_apps[i].name,
                       gfx_rgb(0xee, 0xee, 0xee));
    }
}

static void draw_wifi_panel(const struct gfx_info *info) {
    uint64_t pw = 360, ph = 420;
    uint64_t px = info->width - pw - 24;
    uint64_t py = 60;

    gfx_fill_rect(0, 0, info->width, info->height, gfx_rgb(0x00, 0x00, 0x00));
    for (uint64_t y = 0; y < info->height; y += 4) {
        gfx_fill_rect(0, y, info->width, 4, gfx_rgb(0x05, 0x05, 0x07));
    }

    ui_fill_round_rect(px, py, pw, ph, 20, gfx_rgb(0x22, 0x22, 0x2a));
    gfx_fill_rect(px, py, pw, 2, gfx_rgb(0x3a, 0x3a, 0x4a));

    font_draw_text(px + 32, py + 24, "WiFi", gfx_rgb(0xee, 0xee, 0xee));

    /* Toggle */
    uint64_t tx = px + pw - 90, ty = py + 20;
    gfx_fill_rect(tx, ty, 56, 28, gfx_rgb(0x2a, 0x2a, 0x34));
    ui_fill_circle(tx + 14, ty + 14, 12, gfx_rgb(0x3a, 0xc9, 0x6c));

    /* Networks list */
    uint64_t ny = py + 74;
    for (uint32_t i = 0; i < WIFI_COUNT; i++) {
        uint64_t ny_i = ny + i * 52;
        Rect r = { px + 32, ny_i, pw - 64, 44 };
        bool hover = rect_contains(&r, mouse_x, mouse_y);
        if (hover) {
            gfx_fill_rect(px + 32, ny_i, pw - 64, 44,
                          gfx_rgb(0x2a, 0x2a, 0x34));
        }
        ui_draw_wifi(px + 52, ny_i + 22, gfx_rgb(0x4c, 0xc9, 0xf0));
        font_draw_text(px + 86, ny_i + 8, wifi_nets[i].name,
                       gfx_rgb(0xdd, 0xdd, 0xdd));
        if (wifi_nets[i].locked) {
            ui_draw_lock(px + pw - 60, ny_i + 22, 8,
                         gfx_rgb(0x77, 0x77, 0x77));
        }
    }

    font_draw_text(px + 32, py + ph - 30, "CLICK OUTSIDE TO CLOSE",
                   gfx_rgb(0x66, 0x66, 0x66));
}

/* ------------------------------------------------------------------ Input handling */

static void handle_input(void) {
    uint64_t result = sys2(ELOQUENCE_SYSCALL_INPUT_POLL, 0, 0);
    if (!RESULT_OK(result)) {
        return;
    }
    uint32_t count = (uint32_t)result;
    clicked = false;
    for (uint32_t i = 0; i < count; i++) {
        result = sys2(ELOQUENCE_SYSCALL_INPUT_GET, 0, 0);
        if (!RESULT_OK(result)) {
            break;
        }
        uint64_t packed = result;
        /* Extract fields from the packed 64-bit syscall result using two
         * volatile 16-bit shifts per 32 bits (a single 32-bit shift is
         * mis-emulated by QEMU as #UD). */
        volatile uint64_t pv = packed;
        pv >>= 48;
        pv >>= 8;
        uint8_t type = (uint8_t)pv;
        pv = packed;
        pv >>= 16;
        pv >>= 16;
        pv >>= 16;
        uint8_t key_code = (uint8_t)((uint32_t)pv & 0xffU);
        pv = packed;
        pv >>= 16;
        pv >>= 16;
        int32_t mx = (int32_t)(uint32_t)pv;
        int32_t my = (int32_t)(packed & 0xffffffffU);

        if (type == 2U) {
            mouse_x = mx;
            mouse_y = my;
        } else if (type == 3U && key_code == 0U) {
            clicked = true;
            click_x = mx;
            click_y = my;
        } else if (type == 0U && state == UI_SEARCH) {
            char ch = 0;
            if (scancode_key(key_code, &ch)) {
                if (ch == '\b') {
                    if (search_len > 0) {
                        search_len--;
                        search_buf[search_len] = '\0';
                    }
                } else if (ch == '\n') {
                    /* submit: no-op in prototype */
                } else if (search_len < 60) {
                    search_buf[search_len++] = ch;
                    search_buf[search_len] = '\0';
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ Click routing */

static void route_click(uint64_t sw, uint64_t sh) {
    if (!clicked) return;

    if (state == UI_SEARCH || state == UI_WIFI) {
        state = UI_DESKTOP;
        return;
    }

    /* Desktop: Wi-Fi area in taskbar (bottom-right) opens the Wi-Fi panel */
    uint64_t tb_y = sh >= 44 ? sh - 44 : 0;
    Rect wifi_btn = { sw >= 160 ? sw - 160 : 0, tb_y, 140, 44 };
    if (rect_contains(&wifi_btn, click_x, click_y)) {
        state = UI_WIFI;
        return;
    }

    /* Desktop: search button (top area) */
    Rect search_btn = { 640, 90, 240, 64 };
    if (rect_contains(&search_btn, click_x, click_y)) {
        state = UI_SEARCH;
        search_len = 0;
        search_buf[0] = '\0';
        return;
    }

    /* Dock app clicks */
    uint64_t dock_x = 16, dock_y = 76, cell = 46;
    for (uint32_t i = 0; i < DOCK_COUNT; i++) {
        uint32_t col = i % 2, row = i / 2;
        int32_t cx = (int32_t)(dock_x + 24 + col * cell);
        int32_t cy = (int32_t)(dock_y + 30 + row * cell);
        if (click_x >= cx - 18 && click_x < cx + 18
            && click_y >= cy - 18 && click_y < cy + 18) {
            serial_write("atmosphere: dock app clicked\n");
            (void)i;
        }
    }
}

/* ------------------------------------------------------------------ Main */

extern "C" void _start(void) {
    struct gfx_info info;
    serial_write("atmosphere: entry\n");
    if (!gfx_get_info(&info)) {
        serial_write("atmosphere: gfx_get_info failed\n");
        for (;;) {}
    }
    g_screen_w = info.width;
    g_screen_h = info.height;
    serial_write("atmosphere: gui started\n");

    for (;;) {
        blink++;
        handle_input();
        route_click(g_screen_w, g_screen_h);

        gfx_clear(gfx_rgb(0x0a, 0x0a, 0x0a));
        draw_desktop_background(&info);
        draw_top_bar(&info);
        draw_dock();
        draw_taskbar(&info);

        if (state == UI_DESKTOP) {
            gfx_fill_rect(640, 90, 220, 52, gfx_rgb(0x22, 0x22, 0x2a));
            ui_draw_search_glass(660, 116, 10, gfx_rgb(0x88, 0x88, 0x88));
            font_draw_text(684, 100, "SEARCH", gfx_rgb(0xdd, 0xdd, 0xdd));
        }

        if (state == UI_SEARCH) {
            draw_search_panel(&info);
        } else if (state == UI_WIFI) {
            draw_wifi_panel(&info);
        }

        /* Mouse cursor (clamped to screen) */
        int32_t cx = mouse_x < 0 ? 0 : (mouse_x > (int32_t)g_screen_w - 8
                                         ? (int32_t)g_screen_w - 8 : mouse_x);
        int32_t cy = mouse_y < 0 ? 0 : (mouse_y > (int32_t)g_screen_h - 8
                                         ? (int32_t)g_screen_h - 8 : mouse_y);
        gfx_fill_rect((uint64_t)cx, (uint64_t)cy, 8U, 8U,
                      gfx_rgb(0xee, 0xee, 0xee));

        gfx_flip();
    }

    for (;;) {}
}
