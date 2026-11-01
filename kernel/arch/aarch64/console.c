#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "console.h"

#define FONT_WIDTH 3U
#define FONT_HEIGHT 5U
#define FONT_SCALE 3U
#define CHARACTER_ADVANCE ((FONT_WIDTH + 1U) * FONT_SCALE)
#define LINE_ADVANCE ((FONT_HEIGHT + 2U) * FONT_SCALE)

struct framebuffer_console {
    volatile uint32_t *pixels;
    uint64_t width;
    uint64_t height;
    uint64_t pitch_pixels;
    uint64_t margin;
    uint64_t cursor_x;
    uint64_t cursor_y;
    uint32_t background;
    uint32_t foreground;
};

static struct framebuffer_console console;

/* Five rows of three pixels for A-Z followed by 0-9. */
static const uint8_t font[][FONT_HEIGHT] = {
    {0x2, 0x5, 0x7, 0x5, 0x5}, /* A */
    {0x6, 0x5, 0x6, 0x5, 0x6}, /* B */
    {0x3, 0x4, 0x4, 0x4, 0x3}, /* C */
    {0x6, 0x5, 0x5, 0x5, 0x6}, /* D */
    {0x7, 0x4, 0x6, 0x4, 0x7}, /* E */
    {0x7, 0x4, 0x6, 0x4, 0x4}, /* F */
    {0x3, 0x4, 0x5, 0x5, 0x3}, /* G */
    {0x5, 0x5, 0x7, 0x5, 0x5}, /* H */
    {0x7, 0x2, 0x2, 0x2, 0x7}, /* I */
    {0x1, 0x1, 0x1, 0x5, 0x2}, /* J */
    {0x5, 0x5, 0x6, 0x5, 0x5}, /* K */
    {0x4, 0x4, 0x4, 0x4, 0x7}, /* L */
    {0x5, 0x7, 0x7, 0x5, 0x5}, /* M */
    {0x5, 0x7, 0x7, 0x7, 0x5}, /* N */
    {0x2, 0x5, 0x5, 0x5, 0x2}, /* O */
    {0x6, 0x5, 0x6, 0x4, 0x4}, /* P */
    {0x2, 0x5, 0x5, 0x7, 0x3}, /* Q */
    {0x6, 0x5, 0x6, 0x5, 0x5}, /* R */
    {0x3, 0x4, 0x2, 0x1, 0x6}, /* S */
    {0x7, 0x2, 0x2, 0x2, 0x2}, /* T */
    {0x5, 0x5, 0x5, 0x5, 0x7}, /* U */
    {0x5, 0x5, 0x5, 0x5, 0x2}, /* V */
    {0x5, 0x5, 0x7, 0x7, 0x5}, /* W */
    {0x5, 0x5, 0x2, 0x5, 0x5}, /* X */
    {0x5, 0x5, 0x2, 0x2, 0x2}, /* Y */
    {0x7, 0x1, 0x2, 0x4, 0x7}, /* Z */
    {0x7, 0x5, 0x5, 0x5, 0x7}, /* 0 */
    {0x2, 0x6, 0x2, 0x2, 0x7}, /* 1 */
    {0x6, 0x1, 0x2, 0x4, 0x7}, /* 2 */
    {0x6, 0x1, 0x2, 0x1, 0x6}, /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1}, /* 4 */
    {0x7, 0x4, 0x6, 0x1, 0x6}, /* 5 */
    {0x3, 0x4, 0x6, 0x5, 0x2}, /* 6 */
    {0x7, 0x1, 0x2, 0x2, 0x2}, /* 7 */
    {0x2, 0x5, 0x2, 0x5, 0x2}, /* 8 */
    {0x2, 0x5, 0x3, 0x1, 0x6}, /* 9 */
};

static uint32_t color(uint8_t red, uint8_t green, uint8_t blue,
                      const struct limine_framebuffer *framebuffer) {
    return ((uint32_t)red << framebuffer->red_mask_shift)
         | ((uint32_t)green << framebuffer->green_mask_shift)
         | ((uint32_t)blue << framebuffer->blue_mask_shift);
}

static void fill_background(void) {
    for (uint64_t y = 0; y < console.height; y++) {
        volatile uint32_t *row = console.pixels + y * console.pitch_pixels;

        for (uint64_t x = 0; x < console.width; x++) {
            row[x] = console.background;
        }
    }
}

static void draw_pixel(uint64_t x, uint64_t y) {
    if (x < console.width && y < console.height) {
        console.pixels[y * console.pitch_pixels + x] = console.foreground;
    }
}

static const uint8_t *glyph_for(char character) {
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    if (character >= 'A' && character <= 'Z') {
        return font[(unsigned int)(character - 'A')];
    }
    if (character >= '0' && character <= '9') {
        return font[26U + (unsigned int)(character - '0')];
    }
    return 0;
}

static void advance_line(void) {
    console.cursor_x = console.margin;
    if (console.cursor_y <= UINT64_MAX - LINE_ADVANCE) {
        console.cursor_y += LINE_ADVANCE;
    } else {
        console.cursor_y = console.height;
    }
}

static void draw_character(char character) {
    const uint8_t *glyph;

    if (character == '\n') {
        advance_line();
        return;
    }

    if (console.cursor_x > console.width
        || console.width - console.cursor_x < CHARACTER_ADVANCE) {
        advance_line();
    }
    if (console.cursor_y > console.height
        || console.height - console.cursor_y < FONT_HEIGHT * FONT_SCALE) {
        return;
    }

    glyph = glyph_for(character);
    if (glyph != 0) {
        for (uint64_t row = 0; row < FONT_HEIGHT; row++) {
            for (uint64_t column = 0; column < FONT_WIDTH; column++) {
                if ((glyph[row] & (1U << (FONT_WIDTH - 1U - column))) == 0) {
                    continue;
                }
                for (uint64_t dy = 0; dy < FONT_SCALE; dy++) {
                    for (uint64_t dx = 0; dx < FONT_SCALE; dx++) {
                        draw_pixel(console.cursor_x + column * FONT_SCALE + dx,
                                   console.cursor_y + row * FONT_SCALE + dy);
                    }
                }
            }
        }
    }
    console.cursor_x += CHARACTER_ADVANCE;
}

bool aarch64_console_initialize(const struct limine_framebuffer *framebuffer) {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;

    if (framebuffer == 0 || framebuffer->address == 0
        || framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB
        || framebuffer->bpp != 32 || framebuffer->red_mask_size != 8
        || framebuffer->green_mask_size != 8 || framebuffer->blue_mask_size != 8
        || framebuffer->red_mask_shift > 24
        || framebuffer->green_mask_shift > 24
        || framebuffer->blue_mask_shift > 24 || framebuffer->width == 0
        || framebuffer->height == 0 || framebuffer->width > UINT64_MAX / 4
        || framebuffer->pitch < framebuffer->width * 4
        || framebuffer->pitch % sizeof(uint32_t) != 0
        || framebuffer->height > UINT64_MAX / framebuffer->pitch
        || (uintptr_t)framebuffer->address % _Alignof(uint32_t) != 0) {
        return false;
    }

    red_mask = 0xffU << framebuffer->red_mask_shift;
    green_mask = 0xffU << framebuffer->green_mask_shift;
    blue_mask = 0xffU << framebuffer->blue_mask_shift;
    if ((red_mask & green_mask) != 0 || (red_mask & blue_mask) != 0
        || (green_mask & blue_mask) != 0) {
        return false;
    }

    console.pixels = framebuffer->address;
    console.width = framebuffer->width;
    console.height = framebuffer->height;
    console.pitch_pixels = framebuffer->pitch / sizeof(uint32_t);
    console.margin = framebuffer->width >= CHARACTER_ADVANCE + 32U ? 16 : 0;
    console.cursor_x = console.margin;
    console.cursor_y = console.height >= FONT_HEIGHT * FONT_SCALE + 32U ? 16 : 0;
    console.background = color(12, 20, 34, framebuffer);
    console.foreground = color(220, 236, 248, framebuffer);
    if (console.width < CHARACTER_ADVANCE
        || console.height < FONT_HEIGHT * FONT_SCALE) {
        return false;
    }

    fill_background();
    return true;
}

void aarch64_console_write(const char *text) {
    if (text == 0) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        draw_character(text[i]);
    }
}

void aarch64_console_write_u64(uint64_t value) {
    char digits[20];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0);

    while (count > 0) {
        draw_character(digits[--count]);
    }
}
