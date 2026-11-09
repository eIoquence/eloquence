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

/* Handle a single input event from the kernel input queue. */
static void handle_input_event(uint64_t packed) {
    uint8_t type = (uint8_t)(packed >> 56);
    uint8_t key_code = (uint8_t)((packed >> 48) & 0xff);
    uint32_t x = (uint32_t)(packed >> 32);
    uint32_t y = (uint32_t)packed;

    if (type == 2) { /* mouse_move */
        serial_write("mouse: move ");
    } else if (type == 3) { /* mouse_btn_down */
        serial_write("mouse: btn_down ");
    } else if (type == 4) { /* mouse_btn_up */
        serial_write("mouse: btn_up ");
    } else if (type == 0) { /* key_down */
        serial_write("key: down ");
    } else if (type == 1) { /* key_up */
        serial_write("key: up ");
    } else {
        return;
    }
    serial_write("code=");
    /* Print key_code as hex */
    char hex[3];
    hex[0] = "0123456789abcdef"[(key_code >> 4) & 0xf];
    hex[1] = "0123456789abcdef"[key_code & 0xf];
    hex[2] = 0;
    serial_write(hex);
    serial_write(" x=");
    /* Print x as decimal */
    char buf[12];
    int len = 0;
    uint32_t v = x;
    if (v == 0) { serial_write("0"); }
    else {
        char tmp[12];
        int tl = 0;
        while (v > 0) { tmp[tl++] = '0' + v % 10; v /= 10; }
        while (tl > 0) { buf[len++] = tmp[--tl]; }
    }
    buf[len] = 0;
    serial_write(buf);
    serial_write(" y=");
    len = 0;
    v = y;
    if (v == 0) { serial_write("0"); }
    else {
        char tmp[12];
        int tl = 0;
        while (v > 0) { tmp[tl++] = '0' + v % 10; v /= 10; }
        while (tl > 0) { buf[len++] = tmp[--tl]; }
    }
    buf[len] = 0;
    serial_write(buf);
    serial_write("\n");
}

extern "C" void _start(void) {
    serial_write("atmosphere: entry\n");
    serial_write("atmosphere: gui started\n");

    for (;;) {
        /* Draw one frame */
        sys2(ELOQUENCE_SYSCALL_GFX_FRAME, 0, 0);

        /* Poll and handle all pending input events */
        uint64_t count = sys2(ELOQUENCE_SYSCALL_INPUT_POLL, 0, 0);
        while (count-- > 0) {
            uint64_t packed = sys2(ELOQUENCE_SYSCALL_INPUT_GET, 0, 0);
            handle_input_event(packed);
        }
    }
}
