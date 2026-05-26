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
