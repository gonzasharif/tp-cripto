#include "lsb.h"

void lsb_embed_byte(uint8_t *pixels, size_t offset, uint8_t byte) {
    for (int b = 0; b < 8; b++) {
        uint8_t bit = (byte >> (7 - b)) & 1;
        pixels[offset + b] = (pixels[offset + b] & 0xFE) | bit;
    }
}

uint8_t lsb_extract_byte(const uint8_t *pixels, size_t offset) {
    uint8_t byte = 0;
    for (int b = 0; b < 8; b++) {
        byte = (uint8_t)((byte << 1) | (pixels[offset + b] & 1));
    }
    return byte;
}

void lsb4_embed_byte(uint8_t *pixels, size_t offset, uint8_t byte) {
    /* High nibble in the first pixel, low nibble in the second. The top
     * 4 bits of each carrier pixel are preserved. */
    pixels[offset]     = (uint8_t)((pixels[offset]     & 0xF0) | (byte >> 4));
    pixels[offset + 1] = (uint8_t)((pixels[offset + 1] & 0xF0) | (byte & 0x0F));
}

uint8_t lsb4_extract_byte(const uint8_t *pixels, size_t offset) {
    return (uint8_t)(((pixels[offset] & 0x0F) << 4) | (pixels[offset + 1] & 0x0F));
}
