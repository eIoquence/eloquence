#ifndef ATMOSPHERE_GFX_H
#define ATMOSPHERE_GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct gfx_info {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint32_t framebuffer_size;
    uint8_t rgb;
};

bool gfx_init(void);
bool gfx_get_info(struct gfx_info *info);
bool gfx_put_pixel(uint64_t x, uint64_t y, uint32_t color);
bool gfx_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                   uint32_t color);
bool gfx_clear(uint32_t color);
bool gfx_flip(void);

static inline uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

#endif
