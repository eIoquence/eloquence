#include "ui.h"

#include "font.h"

static inline void putp(uint64_t x, uint64_t y, uint32_t color) {
    (void)gfx_put_pixel(x, y, color);
}

static inline void hline(uint64_t x0, uint64_t x1, uint64_t y,
                         uint32_t color) {
    if (x1 < x0) {
        return;
    }
    for (uint64_t x = x0; x <= x1; x++) {
        putp(x, y, color);
    }
}

static inline void vline(uint64_t x, uint64_t y0, uint64_t y1,
                         uint32_t color) {
    if (y1 < y0) {
        return;
    }
    for (uint64_t y = y0; y <= y1; y++) {
        putp(x, y, color);
    }
}

void ui_fill_circle(uint64_t cx, uint64_t cy, uint64_t r,
                    uint32_t color) {
    if (r == 0) {
        return;
    }
    uint64_t r2 = r * r;
    for (uint64_t dy = 0; dy <= r; dy++) {
        uint64_t dy2 = dy * dy;
        uint64_t hw = r;
        while (hw > 0 && hw * hw + dy2 > r2) {
            hw--;
        }
        hline(cx - hw, cx + hw, cy + dy, color);
        if (dy > 0 && cy > dy) {
            hline(cx - hw, cx + hw, cy - dy, color);
        }
    }
}

void ui_draw_circle(uint64_t cx, uint64_t cy, uint64_t r,
                    uint32_t color) {
    if (r == 0) {
        return;
    }
    int64_t x = (int64_t)r;
    int64_t y = 0;
    int64_t err = 0;
    while (x >= y) {
        putp(cx + x, cy + y, color);
        putp(cx + y, cy + x, color);
        putp(cx - y, cy + x, color);
        putp(cx - x, cy + y, color);
        putp(cx - x, cy - y, color);
        putp(cx - y, cy - x, color);
        putp(cx + y, cy - x, color);
        putp(cx + x, cy - y, color);
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

void ui_fill_round_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                        uint64_t radius, uint32_t color) {
    if (w == 0 || h == 0) {
        return;
    }
    if (radius > w / 2) {
        radius = w / 2;
    }
    if (radius > h / 2) {
        radius = h / 2;
    }
    /* Central rectangle + side strips, then corner circles. */
    gfx_fill_rect(x + radius, y, w - 2 * radius, h, color);
    gfx_fill_rect(x, y + radius, radius, h - 2 * radius, color);
    gfx_fill_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);
    ui_fill_circle(x + radius, y + radius, radius, color);
    ui_fill_circle(x + w - radius - 1, y + radius, radius, color);
    ui_fill_circle(x + radius, y + h - radius - 1, radius, color);
    ui_fill_circle(x + w - radius - 1, y + h - radius - 1, radius, color);
}

void ui_draw_app_icon(uint64_t cx, uint64_t cy, uint64_t r,
                      uint32_t color) {
    ui_fill_circle(cx, cy, r, color);
    ui_draw_circle(cx, cy, r + 1, gfx_rgb(0x00, 0x00, 0x00));
}

void ui_draw_lock(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color) {
    ui_draw_circle(cx, cy, r, color);
    /* Shackle: arc on top. */
    uint64_t sr = r / 2;
    ui_draw_circle(cx, cy - sr / 2, sr, color);
    /* Body: small rect below center. */
    gfx_fill_rect(cx - r / 2, cy, r, r / 2, color);
}

void ui_draw_wifi(uint64_t cx, uint64_t cy, uint32_t color) {
    for (uint64_t i = 0; i < 3; i++) {
        ui_draw_circle(cx, cy + i * 4, 6 + i * 4, color);
    }
    putp(cx, cy + 12, color);
}

void ui_draw_search_glass(uint64_t cx, uint64_t cy, uint64_t r,
                          uint32_t color) {
    ui_draw_circle(cx, cy, r, color);
    /* Handle. */
    uint64_t hx = cx + (r * 3) / 2;
    uint64_t hy = cy + (r * 3) / 2;
    hline(cx + r, hx, cy + r, color);
    putp(hx, hy, color);
}

void ui_draw_text_caret(uint64_t x, uint64_t y, uint64_t h,
                        uint32_t color) {
    vline(x, y, y + h, color);
}
