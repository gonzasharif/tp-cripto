#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stddef.h>

/* The BMP on-disk layout has no implicit padding between fields, so
 * the two header structs must be packed. Otherwise the compiler would
 * insert alignment bytes and direct fread/fwrite would read garbage. */
#pragma pack(push, 1)

/* BMP file header (14 bytes on disk). */
typedef struct {
    uint16_t bfType;        /* magic: 0x4D42 ("BM") */
    uint32_t bfSize;        /* total file size in bytes */
    uint16_t bfReserved1;   /* free for use; the TP stores the PRNG seed here */
    uint16_t bfReserved2;   /* free */
    uint32_t bfOffBits;     /* byte offset where the pixel data starts */
} BMPFileHeader;

/* BMP info / DIB header (40 bytes on disk). */
typedef struct {
    uint32_t biSize;            /* size of this header, must be 40 */
    int32_t  biWidth;           /* image width in pixels */
    int32_t  biHeight;          /* image height in pixels (negative = top-down) */
    uint16_t biPlanes;          /* must be 1 */
    uint16_t biBitCount;        /* bits per pixel (this module assumes 8) */
    uint32_t biCompression;     /* compression mode (this module assumes 0 = BI_RGB) */
    uint32_t biSizeImage;       /* size of raw pixel data with padding */
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;         /* number of palette entries (0 means 2^bpp) */
    uint32_t biClrImportant;
} BMPInfoHeader;

#pragma pack(pop)

/**
 * In-memory representation of an 8-bit BMP image.
 *
 * The pixel buffer is stored *unpadded*: each row contains exactly
 * `width` bytes and `pixels[y*width + x]` is the byte at row `y`,
 * column `x`. Rows follow the same order they had in the file, which
 * for a standard BMP (biHeight > 0) means the first row in `pixels`
 * is the bottom row of the image.
 */
typedef struct {
    BMPFileHeader file_header;
    BMPInfoHeader info_header;
    uint8_t *palette;        /* raw palette bytes, or NULL if absent */
    size_t   palette_size;   /* size of `palette` in bytes (typically 1024 for 8-bit) */
    uint8_t *pixels;         /* width * height bytes, unpadded */
    uint32_t width;          /* convenience: |biWidth| */
    uint32_t height;         /* convenience: |biHeight| */
} BMPImage;

/**
 * Load an 8-bit uncompressed BMP image from disk.
 *
 * Validates the magic number, bit depth (must be 8) and compression
 * (must be 0). Reads the palette if present, then reads the pixel
 * data row by row, stripping the per-row padding so `img->pixels`
 * holds exactly width*height bytes.
 *
 * @param path  Path to the BMP file. Must not be NULL.
 * @param img   Output struct. On success its `palette` and `pixels`
 *              buffers are allocated with malloc and must be released
 *              with bmp_free(). On failure the struct is cleaned up
 *              internally and is safe to discard.
 *
 * @return 0 on success, non-zero on failure (file not openable,
 *         invalid magic, unsupported bit depth or compression, or any
 *         I/O error).
 */
int bmp_read(const char *path, BMPImage *img);

/**
 * Write a BMPImage back to disk as a well-formed BMP file.
 *
 * Re-applies per-row padding as required by the format. The headers
 * and palette are written verbatim from the struct, so any in-place
 * modifications (e.g. setting bfReserved1 to the PRNG seed) are
 * preserved.
 *
 * @param path  Output path. Existing files are overwritten.
 * @param img   Image to write. Must have a valid `pixels` buffer of
 *              size width*height. `palette` may be NULL only when the
 *              source image had no palette.
 *
 * @return 0 on success, non-zero on failure.
 */
int bmp_write(const char *path, const BMPImage *img);

/**
 * Release the dynamic buffers (palette, pixels) owned by a BMPImage
 * and zero out the pointers.
 *
 * Safe to call on an all-zero struct or after a previous bmp_free():
 * the freed pointers are set to NULL.
 *
 * @param img  Image to clean up. Must not be NULL.
 */
void bmp_free(BMPImage *img);

#endif /* BMP_H */
