#ifndef ATMOSPHERE_WINDOW_H
#define ATMOSPHERE_WINDOW_H

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

enum window_state {
    WINDOW_NORMAL,
    WINDOW_FOCUSED,
    WINDOW_CLOSED
};

struct window {
    uint32_t id;
    int32_t x, y;
    uint64_t w, h;
    enum window_state state;
    char title[32];
};

void window_init(struct window *win, uint32_t id, int32_t x, int32_t y,
                 uint64_t w, uint64_t h, const char *title);
void window_draw(const struct window *win);
bool window_contains(const struct window *win, int32_t x, int32_t y);

#endif
