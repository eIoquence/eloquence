#ifndef ELOQUENCE_DRIVERS_VIDEO_FB_H
#define ELOQUENCE_DRIVERS_VIDEO_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Informații despre framebuffer-ul returnate de Limine. */
struct video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct framebuffer_info {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint32_t framebuffer_size;  /* în octeți */
    bool rgb;                   /* true = RGB, false = BGR */
};

/* Inițializează framebuffer-ul din requestul Limine. Returnează true la succes. */
bool fb_init(void);

/* Returnează pointer la structura de informații framebuffer. */
const struct framebuffer_info *fb_get_info(void);

/* Desenează un pixel la (x, y) cu culoarea 32-bit RRGGBB. */
bool fb_put_pixel(uint64_t x, uint64_t y, uint32_t color);

/* Umplere dreptunghi. */
bool fb_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);

/* Șterge tot ecranul cu culoarea dată. */
bool fb_clear(uint32_t color);

/* Sincronizează afișarea (dummy pentru framebuffer simplu, disponibil pentru dublu-buffer viitor). */
bool fb_flip(void);

/* Desenează un cadru complet al desktop-ului în modul kernel (evită round-trip-uri
 * în userspace între operațiuni de desenare, care ar cauza #UD după sysretq). */
bool kernel_gfx_draw_frame(void);

#endif
