#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "eloquence/syscall_abi.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

static void serial_write_char(char c) {
    sys2(ELOQUENCE_SYSCALL_CONSOLE_WRITE_CHAR, (uint64_t)(uint8_t)c, 0);
}
static void serial_write(const char *s) {
    while (*s) { serial_write_char(*s++); }
}

/* UPTIME_TICKS = 100 ticks/sec */
static uint64_t uptime_ticks(void) {
    return sys2(ELOQUENCE_SYSCALL_UPTIME_TICKS, 0, 0);
}

static void write_uint(uint64_t v) {
    char buf[16];
    int n = 0;
    if (v == 0) { serial_write_char('0'); return; }
    while (v > 0) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) serial_write_char(buf[--n]);
}

static void write_time(void) {
    uint64_t ticks = uptime_ticks();
    uint64_t seconds = ticks / 100;
    uint64_t hours = seconds / 3600;
    uint64_t minutes = (seconds % 3600) / 60;
    write_uint(hours);
    serial_write_char(':');
    if (minutes < 10) serial_write_char('0');
    write_uint(minutes);
}

static void write_date(void) {
    uint64_t ticks = uptime_ticks();
    uint64_t days = ticks / (100ULL * 86400ULL);
    serial_write_char('D');
    write_uint(days + 1);
}

extern "C" void _start(void) {
    struct gfx_info info;
    serial_write("atmosphere: entry\n");
    if (!gfx_get_info(&info)) {
        serial_write("atmosphere: gfx_get_info failed\n");
        for (;;) {}
    }
    serial_write("atmosphere: gui started\n");

    /* --- Background gradient --- */
    for (uint64_t y = 0; y < info.height; y += 8) {
        uint8_t b = (uint8_t)(15 + (y * 40 / (info.height ? info.height : 1)));
        gfx_fill_rect(0, y, info.width, 8, gfx_rgb(0x05, 0x07, b));
    }
    serial_write("a\n");

    /* --- Top bar --- */
    gfx_fill_rect(0, 0, info.width, 40, gfx_rgb(0x1a, 0x1a, 0x2a));
    gfx_fill_rect(0, 40, info.width, 2, gfx_rgb(0x2a, 0x2a, 0x3a));
    font_draw_text(20, 10, "TIME: ", gfx_rgb(0xee, 0xee, 0xee));
    write_time();
    serial_write("t1\n");
    font_draw_text(20, 24, "DAY: ", gfx_rgb(0x99, 0x99, 0xaa));
    write_date();
    serial_write("t2\n");

    /* --- Dock (empty, rounded rects as placeholders) --- */
    uint64_t dock_w = 120, dock_x = 20, dock_y = 80, cell = 48;
    ui_fill_round_rect(dock_x, dock_y, dock_w, 200, 12, gfx_rgb(0x1a, 0x1a, 0x22));
    gfx_fill_rect(dock_x, dock_y, dock_w, 2, gfx_rgb(0x3a, 0x3a, 0x3a));
    serial_write("d1\n");

    /* Dock icon slots (empty rounded squares) */
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t col = i % 2, row = i / 2;
        uint64_t cx = dock_x + 24 + col * cell;
        uint64_t cy = dock_y + 30 + row * cell;
        ui_fill_round_rect(cx - 12, cy - 12, 24, 24, 6, gfx_rgb(0x2a, 0x2a, 0x32));
        gfx_fill_rect(cx - 8, cy - 8, 16, 16, gfx_rgb(0x1a, 0x1a, 0x1a));
    }
    serial_write("d2\n");

    /* Music widget */
    uint64_t my = dock_y + 200;
    ui_fill_round_rect(dock_x, my, dock_w, 64, 10, gfx_rgb(0x1a, 0x1a, 0x22));
    ui_draw_app_icon(dock_x + 28, my + 32, 16, gfx_rgb(0x1d, 0xb9, 0x54));
    font_draw_text(dock_x + 52, my + 12, "MOMENT", gfx_rgb(0xee, 0xee, 0xee));
    font_draw_text(dock_x + 52, my + 34, "NOT FOR RADIO", gfx_rgb(0x77, 0x77, 0x77));
    serial_write("d3\n");

    /* Notifications */
    uint64_t ny = my + 76;
    ui_fill_round_rect(dock_x, ny, dock_w, 40, 8, gfx_rgb(0x1a, 0x1a, 0x22));
    font_draw_text(dock_x + 16, ny + 10, "NOTIFICATIONS", gfx_rgb(0xbb, 0xbb, 0xbb));
    ui_draw_circle(dock_x + 78, ny + 20, 6, gfx_rgb(0x55, 0x55, 0x55));
    serial_write("d4\n");

    /* --- Taskbar --- */
    uint64_t tb_y = info.height - 44;
    gfx_fill_rect(0, tb_y, info.width, 44, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, tb_y, info.width, 2, gfx_rgb(0x2a, 0x2a, 0x2a));
    gfx_fill_rect(14, tb_y + 10, 24, 24, gfx_rgb(0x2a, 0x2a, 0x3a));
    ui_draw_circle(26, tb_y + 22, 10, gfx_rgb(0xee, 0xee, 0xee));
    ui_draw_wifi(info.width - 130, tb_y + 22, gfx_rgb(0x88, 0x88, 0x88));
    font_draw_text(info.width - 110, tb_y + 10, "WiFi", gfx_rgb(0xbb, 0xbb, 0xbb));
    serial_write("tb\n");

    /* --- Mouse cursor --- */
    gfx_fill_rect(info.width / 2 - 4, info.height / 2 - 4, 8, 8, gfx_rgb(0xee, 0xee, 0xee));
    serial_write("mc\n");

    gfx_flip();
    serial_write("flip\n");

    serial_write("atmosphere: frame drawn\n");
    sys2(ELOQUENCE_SYSCALL_EXIT, 0, 0);
    for (;;) {}
}