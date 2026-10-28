#ifndef PNGWRITE_H
#define PNGWRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes a 1-bit bitmap as a PNG file -- for screenshots (screenshot.c).
 * Black and white, one bit per pixel, not compressed (the image data goes
 * out as "stored" deflate blocks): a 250 x 122 screen is about 4KB, and
 * nothing here needs working memory. Every viewer reads it, and so does
 * png1bit.c -- a screenshot can be used as a splash image as it is. */

/* Takes the next len bytes of the file; false if they couldn't be written. */
typedef bool (*png_write_fn)(void *ctx, const uint8_t *data, size_t len);

/* bits: width x height pixels, rows of stride bytes, MSB-first, 1 = black
 * (png1bit.h's and the display's own layout). At most 2040 pixels wide.
 * False if the size is out of range or write failed. */
bool png_write_1bit(const uint8_t *bits, int width, int height, int stride, png_write_fn write, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
