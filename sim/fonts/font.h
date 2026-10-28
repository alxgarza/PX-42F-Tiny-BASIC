#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* A font's glyph bitmap, decoupled from any display controller's memory
 * layout: one bit per pixel, MSB-first, packed continuously row-major
 * within each glyph (no padding between rows or between glyphs) --
 * ceil(width*height/8) bytes per character. `data` holds `last_char -
 * first_char + 1` glyphs back to back in code-point order; a character
 * outside [first_char, last_char] has no glyph in this font. */
typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t advance; /* pixel-columns to move right after drawing a glyph
                       * -- may differ from width: some glyph sets (e.g.
                       * font_10x16/font_24x10) are drawn edge to edge and
                       * need an extra 1px gap added on top of width; others
                       * (e.g. font_8x12) already reserve blank columns
                       * within each glyph, so advance == width, and adding
                       * another gap on top would double-space them. */
    uint8_t first_char;
    uint8_t last_char;
    uint16_t bytes_per_char;
    const uint8_t *data;
} display_font_t;

#endif
