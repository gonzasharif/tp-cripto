#include <stdio.h>
#include <stdlib.h>

#include "prng/prng.h"
#include "bmp/bmp.h"

#define UNIMPLEMENTED() \
    do { \
        fprintf(stderr, "%s:%d UNIMPLEMENTED\n", __FILE__, __LINE__); \
        abort(); \
    } while(0)

/* Process exit / return codes by error category, shared by the CLI
 * (main) and the distribute()/recovery() routines, so the exit status
 * tells a script what kind of error occurred:
 *   SSS_OK    success
 *   SSS_USAGE invalid command-line arguments (missing, wrong type,
 *             unknown flag, k out of range, -n misuse)
 *   SSS_IO    a file or directory could not be opened / read / written
 *   SSS_DATA  well-formed request that cannot be carried out: too few
 *             carriers, n < k, secret size not divisible by k, carriers
 *             with wrong/mismatched dimensions, invalid/duplicate shadow
 *             index, a singular system, or an allocation failure
 */
enum sss_status {
    SSS_OK    = 0,
    SSS_USAGE = 1,
    SSS_IO    = 2,
    SSS_DATA  = 3
};

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
 * @return SSS_OK on success, or an `sss_status` error code: SSS_IO if
 *         the secret/carriers cannot be read or written, SSS_DATA if
 *         there are too few carriers, the secret size is not divisible
 *         by `k`, or a carrier does not match the secret's dimensions.
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
 * @return SSS_OK on success, or an `sss_status` error code: SSS_IO if
 *         the carriers or the output file cannot be read/written,
 *         SSS_DATA if fewer than `k` carriers are available, they
 *         disagree on dimensions, or carry invalid shadow indices.
 */
int recovery(const char * secret_path, int k, const char * dir);