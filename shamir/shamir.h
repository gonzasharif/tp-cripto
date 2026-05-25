#include <stdio.h>
#include <stdlib.h>

#include "prng/prng.h"
#include "bmp/bmp.h"

#define UNIMPLEMENTED() \
    do { \
        fprintf(stderr, "%s:%d UNIMPLEMENTED\n", __FILE__, __LINE__); \
        abort(); \
    } while(0)

/**
 * Distribute a secret BMP image into `n` shadow images using a
 * (k, n)-threshold secret sharing scheme.
 *
 * The secret is split so that any `k` of the produced shadows are
 * sufficient to reconstruct it, while any subset of fewer than `k`
 * reveals no information about the original. The shadows are embedded
 * into the BMP carrier images found in `dir` (excluding the secret
 * itself), which are modified in place.
 *
 * @param secret_path  Path to the secret BMP image to distribute.
 *                     Must exist and end in ".bmp". Must not be NULL.
 * @param k            Minimum number of shadows required to recover
 *                     the secret (threshold). Must satisfy 2 <= k <= n.
 * @param n            Total number of shadows to generate. There must
 *                     be at least `n` BMP carrier images in `dir`.
 * @param dir          Directory containing the carrier BMP images that
 *                     will hold the shadows. Must not be NULL.
 *
 * @return 0 on success, non-zero on failure (e.g. the secret cannot be
 *         opened, there are not enough carriers in `dir`, or a carrier
 *         cannot be written).
 */
int distribute(const char * secret_path, int k, int n, const char * dir);

/**
 * Recover a secret BMP image from shadows embedded in carrier images.
 *
 * Scans `dir` for BMP carrier images containing shadows produced by a
 * previous call to distribute(), extracts the shadows, and reconstructs
 * the original secret using any `k` of them. The reconstructed image is
 * written to `secret_path`.
 *
 * @param secret_path  Output path where the recovered BMP image will be
 *                     written. Must end in ".bmp". Must not be NULL.
 * @param k            Number of shadows used during distribution
 *                     (threshold). At least `k` valid shadows must be
 *                     available in `dir`. Must satisfy 2 <= k <= 10.
 * @param dir          Directory containing the carrier BMP images with
 *                     embedded shadows. Must not be NULL.
 *
 * @return 0 on success, non-zero on failure (e.g. fewer than `k`
 *         shadows could be read, or the output file cannot be written).
 */
int recovery(const char * secret_path, int k, const char * dir);