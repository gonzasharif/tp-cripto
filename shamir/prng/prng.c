#include "prng.h"

/* Module-private state. Kept `static` so each translation unit that
 * includes prng.h does not get its own copy and the generator behaves
 * as a single shared sequence. */
static int64_t seed;

void setSeed(int64_t s) {
    seed = (s ^ 0x5DEECE66DL) & ((1LL << 48) - 1);
}

uint8_t nextChar(void) {
    seed = (seed * 0x5DEECE66DL + 0xBL) & ((1LL << 48) - 1);
    return (uint8_t)(seed >> 40);
}
