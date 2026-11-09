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

/* UPTIME_TICKS returnează numărul de tics de la boot (100 ticks/sec). */
static uint64_t uptime_ticks(void) {
    return sys2(ELOQUENCE_SYSCALL_UPTIME_TICKS, 0, 0);
}

/* Scrie un număr zecimal fără semn, maxim 6 cifre. */
static void write_uint(uint64_t v) {
    char buf[16];
    int n = 0;
    if (v == 0) {
        serial_write_char('0');
        return;
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        serial_write_char(buf[--n]);
    }
}

static void write_time(void) {
    /* 100 tics/sec */
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
    /* Fără calendar real: afișăm ziua aprox. din numărul de zile de la boot. */
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

    /* Draw one complete frame. Each drawing syscall is followed by
     * a serial_write syscall to keep the syscall->sysretq cycle going,
     * which avoids the #UD that occurs when executing user-mode
     * instructions directly after sysretq. */
    gfx_clear(gfx_rgb(0x0a, 0x0a, 0x0a));
    serial_write("a\n");

    /* Desktop background */
    for (uint64_t y = 0; y < info.height; y += 32) {
        uint8_t b = (uint8_t)(10 + (y * 50 / (info.height ? info.height : 1)));
        gfx_fill_rect(0, y, info.width, 32, gfx_rgb(0x07, 0x07, b));
    }
    serial_write("b\n");

    /* Top bar */
    gfx_fill_rect(0, 0, info.width, 40, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, 40, info.width, 2, gfx_rgb(0x2a, 0x2a, 0x2a));
    font_draw_text(20, 10, "TIME: ", gfx_rgb(0xee, 0xee, 0xee));
    write_time();
    serial_write("\n");
    font_draw_text(20, 24, "DAY: ", gfx_rgb(0x88, 0x88, 0x88));
    write_date();
    serial_write("c\n");

    /* Empty dock (no icons) */
    uint64_t dock_w = 100, dock_x = 16, dock_y = 76;
    gfx_fill_rect(dock_x, dock_y, dock_w, 240, gfx_rgb(0x14, 0x14, 0x1a));
    gfx_fill_rect(dock_x, dock_y, dock_w, 2, gfx_rgb(0x2a, 0x2a, 0x2a));
    serial_write("d\n");

    /* Taskbar */
    uint64_t tb_y = info.height - 44;
    gfx_fill_rect(0, tb_y, info.width, 44, gfx_rgb(0x1a, 0x1a, 0x1a));
    gfx_fill_rect(0, tb_y, info.width, 2, gfx_rgb(0x2a, 0x2a, 0x2a));
    ui_draw_circle(26, tb_y + 22, 10, gfx_rgb(0xdd, 0xdd, 0xdd));
    ui_draw_wifi(info.width - 130, tb_y + 22, gfx_rgb(0x88, 0x88, 0x88));
    font_draw_text(info.width - 110, tb_y + 10, "WiFi", gfx_rgb(0xbb, 0xbb, 0xbb));
    serial_write("f\n");

    /* Mouse cursor */
    gfx_fill_rect(info.width / 2 - 4, info.height / 2 - 4, 8, 8,
                  gfx_rgb(0xee, 0xee, 0xee));
    serial_write("g\n");

    gfx_flip();
    serial_write("h\n");

    serial_write("atmosphere: frame drawn, exiting\n");
    sys2(ELOQUENCE_SYSCALL_EXIT, 0, 0);
    for (;;) {}
}