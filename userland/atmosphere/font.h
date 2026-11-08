#ifndef ATMOSPHERE_FONT_H
#define ATMOSPHERE_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

#define FONT_CW 5U   /* glyph cell width  */
#define FONT_CH 7U   /* glyph cell height */
#define FONT_SP 1U   /* inter-cell spacing */

/* Obtine bitmap-ul unui caracter (5 octeti = 5 coloane, bitul r al octetului
 * c este pixelul pe randul r). Intoarce NULL pentru caractere nesuportate. */
const uint8_t *font_glyph(char c);

/* Deseneaza un sir la (x, y) cu culoarea data. Intoarce latimea desenata. */
uint64_t font_draw_text(uint64_t x, uint64_t y, const char *s, uint32_t color);

/* Latimea pe care o arunca un sir (in pixeli). */
uint64_t font_text_width(const char *s);

#endif
