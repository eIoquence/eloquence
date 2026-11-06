#include "window.h"
#include "gfx.h"

#define TITLE_BAR_H 24
#define BORDER_W 2

void window_init(struct window *win, uint32_t id, int32_t x, int32_t y,
                 uint64_t w, uint64_t h, const char *title) {
    win->id = id;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->state = WINDOW_NORMAL;
    for (uint32_t i = 0; i < 31 && title[i] != '\0'; i++) {
        win->title[i] = title[i];
    }
    win->title[31] = '\0';
}

void window_draw(const struct window *win) {
    uint32_t border = win->state == WINDOW_FOCUSED
        ? gfx_rgb(0x4c, 0xc9, 0xf0) : gfx_rgb(0x44, 0x44, 0x44);
    uint32_t title_bar = win->state == WINDOW_FOCUSED
        ? gfx_rgb(0x3a, 0x3a, 0x4a) : gfx_rgb(0x2a, 0x2a, 0x2a);
    uint32_t body = gfx_rgb(0x1a, 0x1a, 0x1a);

    gfx_fill_rect((uint64_t)win->x, (uint64_t)win->y, win->w, win->h,
                  border);
    gfx_fill_rect((uint64_t)(win->x + BORDER_W),
                  (uint64_t)(win->y + BORDER_W),
                  win->w - 2 * BORDER_W, TITLE_BAR_H, title_bar);
    gfx_fill_rect((uint64_t)(win->x + BORDER_W),
                  (uint64_t)(win->y + BORDER_W + TITLE_BAR_H),
                  win->w - 2 * BORDER_W,
                  win->h - BORDER_W - TITLE_BAR_H, body);

    /* Title placeholder */
    gfx_fill_rect((uint64_t)(win->x + BORDER_W + 4),
                  (uint64_t)(win->y + BORDER_W + 4), 72U, 8U,
                  gfx_rgb(0xdd, 0xdd, 0xdd));

    /* Close button */
    gfx_fill_rect((uint64_t)(win->x + win->w - BORDER_W - 20),
                  (uint64_t)(win->y + BORDER_W + 4), 16U, 16U,
                  gfx_rgb(0xcc, 0x44, 0x44));
}

bool window_contains(const struct window *win, int32_t x, int32_t y) {
    return x >= win->x && x < (int32_t)(win->x + win->w)
        && y >= win->y && y < (int32_t)(win->y + win->h);
}
