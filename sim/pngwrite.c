#include "pngwrite.h"

#include <string.h>

/* Everything goes out through put(), which also keeps the running CRC-32
 * of the chunk being written and remembers the first failure. */
typedef struct {
    png_write_fn write;
    void *ctx;
    bool ok;
    uint32_t crc;
} out_t;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    /* Half a byte at a time: a 16-entry table instead of 1KB. */
    static const uint32_t table[16] = {
            0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
            0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
    };
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        crc = table[crc & 15] ^ (crc >> 4);
        crc = table[crc & 15] ^ (crc >> 4);
    }
    return crc;
}

static void put(out_t *o, const uint8_t *p, size_t n) {
    if (o->ok)
        o->ok = o->write(o->ctx, p, n);
    o->crc = crc32_update(o->crc, p, n);
}

static void put_u32(out_t *o, uint32_t v) {
    uint8_t b[4] = {(uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v};
    put(o, b, 4);
}

/* A chunk: its length, its type, then the data (the caller's put()s), then
 * chunk_end()'s CRC of the type and data. */
static void chunk_begin(out_t *o, const char *type, uint32_t length) {
    put_u32(o, length);
    o->crc = 0xFFFFFFFFu;
    put(o, (const uint8_t *) type, 4);
}

static void chunk_end(out_t *o) {
    put_u32(o, ~o->crc);
}

bool png_write_1bit(const uint8_t *bits, int width, int height, int stride, png_write_fn write, void *ctx) {
    uint8_t row[256]; /* the filter byte, then the pixels */
    if (width < 1 || height < 1 || width > (int) (sizeof(row) - 1) * 8 || stride < (width + 7) / 8)
        return false;
    uint32_t row_len = 1 + (uint32_t) (width + 7) / 8;
    /* A stored deflate block holds at most 65535 bytes: whole rows each. */
    uint32_t rows_per_block = 65535u / row_len;
    uint32_t blocks = ((uint32_t) height + rows_per_block - 1) / rows_per_block;

    out_t o = {write, ctx, true, 0};
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put(&o, signature, sizeof(signature));

    chunk_begin(&o, "IHDR", 13);
    put_u32(&o, (uint32_t) width);
    put_u32(&o, (uint32_t) height);
    static const uint8_t format[5] = {1, 0, 0, 0, 0}; /* 1 bit, grayscale; deflate, filters, not interlaced */
    put(&o, format, sizeof(format));
    chunk_end(&o);

    /* The image: a zlib stream (2-byte header, the blocks, the Adler-32 of
     * the data) of the rows, each led by its filter type (0 = none). */
    chunk_begin(&o, "IDAT", 2 + blocks * 5 + (uint32_t) height * row_len + 4);
    static const uint8_t zlib_header[2] = {0x78, 0x01};
    put(&o, zlib_header, sizeof(zlib_header));
    uint32_t a = 1, b = 0; /* Adler-32 */
    for (uint32_t y = 0; y < (uint32_t) height; y++) {
        if (y % rows_per_block == 0) {
            uint32_t rows = (uint32_t) height - y < rows_per_block ? (uint32_t) height - y : rows_per_block;
            uint32_t len = rows * row_len;
            uint8_t header[5] = {y + rows == (uint32_t) height, /* 1 = the last block */
                                 (uint8_t) len, (uint8_t) (len >> 8), (uint8_t) ~len, (uint8_t) (~len >> 8)};
            put(&o, header, sizeof(header));
        }
        /* In a grayscale PNG 0 is black: the bitmap's bits, inverted, and
         * the unused bits at the end of the row left 0. */
        row[0] = 0;
        for (uint32_t i = 1; i < row_len; i++)
            row[i] = (uint8_t) ~bits[y * (uint32_t) stride + i - 1];
        if (width % 8)
            row[row_len - 1] &= (uint8_t) (0xFF << (8 - width % 8));
        for (uint32_t i = 0; i < row_len; i++) {
            a = (a + row[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        put(&o, row, row_len);
    }
    put_u32(&o, b << 16 | a);
    chunk_end(&o);

    chunk_begin(&o, "IEND", 0);
    chunk_end(&o);
    return o.ok;
}
