#ifndef LSB_H
#define LSB_H

#include <stdint.h>
#include <stddef.h>

/**
 * Embed the 8 bits of `byte` into the least significant bit of 8
 * consecutive pixels starting at `pixels[offset]`, most-significant
 * bit first.
 *
 * Each target pixel is replaced as:
 *   pixels[offset + b] = (pixels[offset + b] & 0xFE) | bit_b
 * where bit_b is the (7-b)-th bit of `byte` (so the MSB of `byte`
 * lands at offset + 0).
 *
 * @param pixels  Pointer to the carrier pixel buffer. Must have at
 *                least offset + 8 bytes addressable. Modified in place.
 * @param offset  Index of the first pixel to overwrite.
 * @param byte    Value whose 8 bits will be embedded.
 */
void lsb_embed_byte(uint8_t *pixels, size_t offset, uint8_t byte);

/**
 * Inverse of lsb_embed_byte: read the LSBs of 8 consecutive pixels
 * starting at `pixels[offset]` and reassemble them into a single
 * byte, most-significant bit first.
 *
 * @param pixels  Pointer to the carrier pixel buffer (read-only).
 *                Must have at least offset + 8 bytes addressable.
 * @param offset  Index of the first pixel to read.
 *
 * @return The 8 LSBs combined as a byte (MSB-first).
 */
uint8_t lsb_extract_byte(const uint8_t *pixels, size_t offset);

#endif /* LSB_H */
