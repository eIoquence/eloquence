#ifndef ATMOSPHERE_UI_H
#define ATMOSPHERE_UI_H

#include <stdint.h>

#include "gfx.h"

/* Deseneaza un dreptunghi cu colturile rotunde (aproximat printr-un
 * dreptunghi umplut plus cercuri in colturii interioare pentru aspect). */
void ui_fill_round_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                        uint64_t radius, uint32_t color);

/* Deseneaza un cerc umplut la (cx, cy) cu raza r. */
void ui_fill_circle(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);

/* Deseneaza conturul unui cerc. */
void ui_draw_circle(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);

/* Pictograma de aplicatie: cerc colorat cu un contur deschis. */
void ui_draw_app_icon(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);

/* Simbol de castig (cerc exterior + interior). */
void ui_draw_lock(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);

/* Semnal Wi-Fi: trei arce concentrice. */
void ui_draw_wifi(uint64_t cx, uint64_t cy, uint32_t color);

/* Lupă de căutare: cerc + linie diagonala. */
void ui_draw_search_glass(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);

/* Punct de(cursor) text. */
void ui_draw_text_caret(uint64_t x, uint64_t y, uint64_t h, uint32_t color);

#endif
