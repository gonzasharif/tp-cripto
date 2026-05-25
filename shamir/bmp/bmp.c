#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmp.h"

#define BMP_MAGIC 0x4D42

/* Bytes per row in the BMP file, including the 4-byte alignment
 * padding that the format requires after each scanline. */
static size_t row_stride(uint32_t width, uint16_t bpp) {
    return ((size_t)width * bpp + 31) / 32 * 4;
}

int bmp_read(const char *path, BMPImage *img) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;

    memset(img, 0, sizeof(*img));

    if (fread(&img->file_header, sizeof(img->file_header), 1, f) != 1) goto fail;
    if (fread(&img->info_header, sizeof(img->info_header), 1, f) != 1) goto fail;

    if (img->file_header.bfType    != BMP_MAGIC) goto fail;
    if (img->info_header.biBitCount != 8)        goto fail;
    if (img->info_header.biCompression != 0)     goto fail;

    img->width  = (uint32_t)img->info_header.biWidth;
    img->height = (uint32_t)(img->info_header.biHeight < 0
                                 ? -img->info_header.biHeight
                                 :  img->info_header.biHeight);

    /* The palette (if any) sits between the info header and the pixel
     * data. Its size is whatever the file reserves before bfOffBits. */
    size_t headers = sizeof(img->file_header) + sizeof(img->info_header);
    if (img->file_header.bfOffBits > headers) {
        img->palette_size = img->file_header.bfOffBits - headers;
        img->palette = malloc(img->palette_size);
        if (!img->palette) goto fail;
        if (fread(img->palette, 1, img->palette_size, f) != img->palette_size) goto fail;
    }

    /* Defensive seek in case there is slack between the palette and
     * bfOffBits — some BMP encoders leave a gap. */
    if (fseek(f, (long)img->file_header.bfOffBits, SEEK_SET) != 0) goto fail;

    size_t pixel_count = (size_t)img->width * img->height;
    img->pixels = malloc(pixel_count);
    if (!img->pixels) goto fail;

    size_t stride  = row_stride(img->width, img->info_header.biBitCount);
    size_t padding = stride - img->width;
    for (uint32_t y = 0; y < img->height; y++) {
        if (fread(img->pixels + (size_t)y * img->width, 1, img->width, f) != img->width)
            goto fail;
        if (padding && fseek(f, (long)padding, SEEK_CUR) != 0) goto fail;
    }

    fclose(f);
    return 0;

fail:
    fclose(f);
    bmp_free(img);
    return 1;
}

int bmp_write(const char *path, const BMPImage *img) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    if (fwrite(&img->file_header, sizeof(img->file_header), 1, f) != 1) goto fail;
    if (fwrite(&img->info_header, sizeof(img->info_header), 1, f) != 1) goto fail;

    if (img->palette && img->palette_size > 0) {
        if (fwrite(img->palette, 1, img->palette_size, f) != img->palette_size) goto fail;
    }

    size_t stride  = row_stride(img->width, img->info_header.biBitCount);
    size_t padding = stride - img->width;
    static const uint8_t zero_pad[4] = {0, 0, 0, 0};
    for (uint32_t y = 0; y < img->height; y++) {
        if (fwrite(img->pixels + (size_t)y * img->width, 1, img->width, f) != img->width)
            goto fail;
        if (padding && fwrite(zero_pad, 1, padding, f) != padding) goto fail;
    }

    fclose(f);
    return 0;

fail:
    fclose(f);
    return 1;
}

void bmp_free(BMPImage *img) {
    free(img->palette);
    free(img->pixels);
    img->palette = NULL;
    img->pixels = NULL;
    img->palette_size = 0;
}
