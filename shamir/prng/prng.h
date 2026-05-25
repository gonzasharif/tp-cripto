#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

/**
 * Seed the linear-congruential pseudo-random generator used to build
 * the permutation table for the Shamir distribution scheme.
 *
 * Uses the same constants as java.util.Random: a 48-bit state advanced
 * by  state = (state * 0x5DEECE66D + 0xB) mod 2^48.  Given the same
 * `s`, the byte sequence produced by subsequent calls to nextChar() is
 * fully deterministic, which lets distribute() and recovery() agree on
 * the permutation as long as they share the seed.
 *
 * Note that the generator state is module-global: a second call to
 * setSeed() restarts the sequence and discards any previous progress.
 *
 * @param s  Initial seed value. Any 64-bit integer is accepted; only
 *           the low 48 bits (after an internal XOR-scrambling step)
 *           affect the sequence.
 */
void setSeed(int64_t s);

/**
 * Produce the next pseudo-random byte from the sequence started by
 * setSeed().
 *
 * Each call advances the 48-bit state and returns the 8 most
 * significant bits of the new state, since in a power-of-two-modulus
 * LCG the low bits have very short periods and are unsuitable as
 * output.
 *
 * @return One pseudo-random byte (0–255). Calling nextChar() without a
 *         previous setSeed() yields a sequence derived from the
 *         default zero-initialised state.
 */
uint8_t nextChar(void);

#endif /* PRNG_H */
