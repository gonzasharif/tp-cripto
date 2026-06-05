#include <string.h>
#include <time.h>

#include "shamir.h"
#include "gf/gf.h"
#include "lsb/lsb.h"
#include "../utils/utils.h"

/* ─────────────────────────── Helpers ─────────────────────────── */

/* Embedding depth per scheme. The carriers always have the secret's
 * dimensions; only the number of LSBs used changes, so the shadow body
 * occupies px_per_byte(k) * (m/k) of the carrier's m pixels:
 *   k == 8 → 1 LSB/pixel (8 px/byte) → fills the whole carrier (spec).
 *   k != 8 → 4 LSB/pixel (2 px/byte) → uses 2/k of the carrier (<= m).
 * Because the carrier and the secret are the same size, recovery reads
 * the secret dimensions straight from the carrier header — no extra
 * metadata is needed. */
static size_t px_per_byte(int k) { return k == 8 ? 8 : 2; }

/* Embed the shadow byte of block `block` into a carrier, picking the LSB
 * depth from the scheme. */
static void embed_shadow_byte(uint8_t *pixels, size_t block, int k, uint8_t byte) {
    size_t off = block * px_per_byte(k);
    if (k == 8) lsb_embed_byte(pixels, off, byte);
    else        lsb4_embed_byte(pixels, off, byte);
}

/* Inverse of embed_shadow_byte: read the shadow byte of block `block`. */
static uint8_t extract_shadow_byte(const uint8_t *pixels, size_t block, int k) {
    size_t off = block * px_per_byte(k);
    return k == 8 ? lsb_extract_byte(pixels, off)
                  : lsb4_extract_byte(pixels, off);
}

/* Apply the reversible PRNG XOR mask (Wu-Lo Step 1): dst[i] = src[i] ^
 * nextChar(), seeding the LCG with `seed`. `dst` and `src` may alias, so
 * the same routine both randomizes the secret and undoes the mask on
 * recovery. */
static void apply_xor_mask(uint8_t *dst, const uint8_t *src,
                           size_t size, uint16_t seed) {
    setSeed((int64_t)seed);
    for (size_t i = 0; i < size; i++)
        dst[i] = (uint8_t)(src[i] ^ nextChar());
}

/* Compute the n shadow bytes of one block. The k bytes in `coefs` are the
 * coefficients of a degree-(k-1) polynomial mod 257; out[j] = p(j+1).
 *
 * Handles the GF_MOD == 257 overflow (Wu-Lo Step 5): if any evaluation
 * lands on 256 (does not fit in a byte) the first non-zero coefficient is
 * decremented and every shadow re-evaluated. The paper proves an all-zero
 * block can never yield 256, so a non-zero coefficient always exists, and
 * each step lowers the coefficient sum by one, so the loop terminates.
 * `coefs` may be modified in place. */
static void shares_for_block(uint8_t *coefs, int k, int n, uint8_t *out) {
    for (;;) {
        int collision = 0;
        for (int j = 0; j < n; j++) {
            int result = gf_poly_eval(coefs, k, j + 1);
            if (result == 256) { collision = 1; break; }
            out[j] = (uint8_t)result;
        }
        if (!collision) return;

        int idx = 0;
        while (idx < k && coefs[idx] == 0) idx++;
        coefs[idx]--;   /* idx < k guaranteed: an all-zero block can't hit 256 */
    }
}

/* Recover the k coefficients of one block (its k permuted secret bytes)
 * from the k shares y[i] at abscissas x_values[i], by solving the
 * Vandermonde system V·c = y mod 257 with Gauss-Jordan. Writes the k
 * bytes to `out`; returns 0 on success, non-zero if the matrix is
 * singular (duplicate x-values). */
static int solve_block(const int *x_values, const uint8_t *y, int k, uint8_t *out) {
    int mat[GF_K_MAX][GF_K_MAX + 1];
    for (int i = 0; i < k; i++) {
        int power = 1;
        for (int j = 0; j < k; j++) {
            mat[i][j] = power;
            power = (int)((long)power * x_values[i] % GF_MOD);
        }
        mat[i][k] = (int)y[i];
    }

    int coefs[GF_K_MAX];
    if (gf_gauss_solve(mat, k, coefs) != 0) return 1;
    for (int i = 0; i < k; i++) out[i] = (uint8_t)coefs[i];
    return 0;
}

/* List the BMP carriers in `dir` (excluding `exclude`), require at least
 * `n_read` of them, and read the first `n_read` into a fresh array.
 *
 * On return `paths` and `n_paths` hold the full listing (caller frees
 * both the strings and the array) and `carriers` holds the array whose
 * first `n_loaded` entries are initialized (caller frees with bmp_free).
 * The out-params are always set so the caller's cleanup is valid even on
 * failure. Prints its own diagnostics; returns 0 on success. */
static int load_carriers(const char *dir, const char *exclude, int n_read,
                         char ***paths, int *n_paths,
                         BMPImage **carriers, int *n_loaded) {
    *paths = NULL; *n_paths = 0; *carriers = NULL; *n_loaded = 0;

    char **list = NULL;
    int count = list_bmp_files(dir, exclude, &list);
    if (count < 0) {
        fprintf(stderr, "Error: could not list BMPs in '%s'.\n", dir);
        return SSS_IO;
    }
    *paths = list;
    *n_paths = count;

    if (count < n_read) {
        fprintf(stderr,
                "Error: need at least %d carrier images but found %d in '%s'.\n",
                n_read, count, dir);
        return SSS_DATA;
    }

    *carriers = calloc((size_t)n_read, sizeof(BMPImage));
    if (!*carriers) return SSS_DATA;

    for (int i = 0; i < n_read; i++) {
        if (bmp_read(list[i], &(*carriers)[i]) != 0) {
            fprintf(stderr, "Error: could not read carrier '%s'.\n", list[i]);
            return SSS_IO;
        }
        (*n_loaded)++;
    }
    return SSS_OK;
}

/* Read the k shadow indices (header bytes 8-9) used as polynomial
 * abscissas, rejecting non-positive or duplicate values — both would make
 * the Vandermonde system ill-defined. Returns 0 on success. */
static int read_x_values(const BMPImage *carriers, char *const *paths,
                         int k, int *x_values) {
    for (int i = 0; i < k; i++) {
        x_values[i] = (int)carriers[i].file_header.bfReserved2;
        if (x_values[i] < 1) {
            fprintf(stderr, "Error: invalid shadow index %d in carrier '%s'.\n",
                    x_values[i], paths[i]);
            return 1;
        }
    }
    for (int i = 0; i < k; i++)
        for (int j = i + 1; j < k; j++)
            if (x_values[i] == x_values[j]) {
                fprintf(stderr,
                        "Error: duplicate shadow index %d in '%s' and '%s'.\n",
                        x_values[i], paths[i], paths[j]);
                return 1;
            }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════ */

int distribute(const char *secret_path, int k, int n, const char *dir) {
    int       ret           = SSS_DATA;   /* overwritten at each exit */
    BMPImage  secret        = {0};
    BMPImage *carriers      = NULL;
    char    **carrier_paths = NULL;
    int       carrier_count = 0;
    int       carriers_read = 0;
    uint8_t  *perm          = NULL;

    /* ── 1. Read the secret ───────────────────────────────────── */
    if (bmp_read(secret_path, &secret) != 0) {
        fprintf(stderr, "Error: could not read secret '%s'.\n", secret_path);
        return SSS_IO;
    }

    /* Work over the raw pixel-data region (per-row padding included), which
     * is what the steganographic stream covers. For widths that are a
     * multiple of 4 this equals width*height. */
    size_t secret_size = secret.pixel_bytes;
    if (secret_size % (size_t)k != 0) {
        fprintf(stderr,
                "Error: secret size (%zu bytes) must be divisible by k=%d.\n",
                secret_size, k);
        ret = SSS_DATA;
        goto cleanup;
    }
    size_t shadow_bytes = secret_size / (size_t)k;

    /* ── 2. Load the first n carriers and check they match the secret ── */
    const char *secret_basename = strrchr(secret_path, '/');
    secret_basename = secret_basename ? secret_basename + 1 : secret_path;

    int load_rc = load_carriers(dir, secret_basename, n,
                                &carrier_paths, &carrier_count,
                                &carriers, &carriers_read);
    if (load_rc != SSS_OK) { ret = load_rc; goto cleanup; }

    for (int i = 0; i < n; i++) {
        if (carriers[i].width  != secret.width ||
            carriers[i].height != secret.height) {
            fprintf(stderr,
                    "Error: carrier '%s' must be %ux%u (same as the secret) "
                    "but is %ux%u.\n",
                    carrier_paths[i], secret.width, secret.height,
                    carriers[i].width, carriers[i].height);
            ret = SSS_DATA;
            goto cleanup;
        }
    }

    /* ── 3. Randomize the secret with the PRNG XOR mask (Step 1) ── */
    uint16_t seed = (uint16_t)(time(NULL) & 0xFFFF);
    perm = malloc(secret_size);
    if (!perm) { ret = SSS_DATA; goto cleanup; }
    apply_xor_mask(perm, secret.pixels, secret_size, seed);

    /* ── 4. Share each block of k bytes and embed the n results ── */
    for (size_t block = 0; block < shadow_bytes; block++) {
        uint8_t *coefs = perm + block * (size_t)k;
        uint8_t  shadow_byte[GF_K_MAX];

        shares_for_block(coefs, k, n, shadow_byte);
        for (int j = 0; j < n; j++)
            embed_shadow_byte(carriers[j].pixels, block, k, shadow_byte[j]);
    }

    /* ── 5. Stamp seed + shadow index into the header and write ── */
    for (int j = 0; j < n; j++) {
        carriers[j].file_header.bfReserved1 = seed;
        carriers[j].file_header.bfReserved2 = (uint16_t)(j + 1);

        if (bmp_write(carrier_paths[j], &carriers[j]) != 0) {
            fprintf(stderr, "Error: could not write carrier '%s'.\n",
                    carrier_paths[j]);
            ret = SSS_IO;
            goto cleanup;
        }
    }

    printf("Info: distributed secret into %d shadow(s) with seed %u.\n",
           n, (unsigned)seed);
    ret = SSS_OK;

cleanup:
    if (carriers) {
        for (int i = 0; i < carriers_read; i++) bmp_free(&carriers[i]);
        free(carriers);
    }
    if (carrier_paths) {
        for (int i = 0; i < carrier_count; i++) free(carrier_paths[i]);
        free(carrier_paths);
    }
    free(perm);
    bmp_free(&secret);
    return ret;
}

int recovery(const char *secret_path, int k, const char *dir) {
    int       ret           = SSS_DATA;   /* overwritten at each exit */
    BMPImage *carriers      = NULL;
    char    **carrier_paths = NULL;
    int       carrier_count = 0;
    int       carriers_read = 0;
    uint8_t  *secret_bytes  = NULL;
    BMPImage  output        = {0};

    /* ── 1. Load the first k carriers ─────────────────────────── */
    const char *secret_basename = strrchr(secret_path, '/');
    secret_basename = secret_basename ? secret_basename + 1 : secret_path;

    int load_rc = load_carriers(dir, secret_basename, k,
                                &carrier_paths, &carrier_count,
                                &carriers, &carriers_read);
    if (load_rc != SSS_OK) { ret = load_rc; goto cleanup; }

    /* ── 2. All carriers must share dimensions; the secret inherits them.
     *
     * For every k the secret and the carriers are the same size, so all
     * participating carriers must agree on width and height. */
    for (int i = 1; i < k; i++) {
        if (carriers[i].width  != carriers[0].width ||
            carriers[i].height != carriers[0].height) {
            fprintf(stderr,
                    "Error: all carriers must share dimensions. "
                    "'%s' is %ux%u but '%s' is %ux%u.\n",
                    carrier_paths[0], carriers[0].width, carriers[0].height,
                    carrier_paths[i], carriers[i].width, carriers[i].height);
            ret = SSS_DATA;
            goto cleanup;
        }
    }

    /* Same convention as distribute: operate over the padded pixel-data
     * region the shadow stream was embedded into. */
    size_t secret_size  = carriers[0].pixel_bytes;
    size_t shadow_bytes = secret_size / (size_t)k;

    /* ── 3. Read seed (bytes 6-7) and shadow indices / x-values (8-9) ── */
    uint16_t seed = carriers[0].file_header.bfReserved1;
    int x_values[GF_K_MAX];
    if (read_x_values(carriers, carrier_paths, k, x_values) != 0) {
        ret = SSS_DATA;
        goto cleanup;
    }

    /* ── 4. Interpolate each block to recover the permuted secret bytes.
     *
     * Block b gives k pairs (x_i, p(x_i)) of a degree-(k-1) polynomial;
     * its coefficients are exactly the k permuted secret bytes. */
    secret_bytes = malloc(secret_size);
    if (!secret_bytes) { ret = SSS_DATA; goto cleanup; }

    for (size_t block = 0; block < shadow_bytes; block++) {
        uint8_t y[GF_K_MAX];
        for (int i = 0; i < k; i++)
            y[i] = extract_shadow_byte(carriers[i].pixels, block, k);

        if (solve_block(x_values, y, k, secret_bytes + block * (size_t)k) != 0) {
            fprintf(stderr,
                    "Error: singular Vandermonde matrix at block %zu.\n", block);
            ret = SSS_DATA;
            goto cleanup;
        }
    }

    /* ── 5. Undo the PRNG XOR mask with the recovered seed ────── */
    apply_xor_mask(secret_bytes, secret_bytes, secret_size, seed);

    /* ── 6. Build the output BMP from carrier[0]'s header + palette ──
     *
     * The carrier already has the secret's dimensions (true for every k),
     * so the header and palette are copied verbatim. */
    output.file_header = carriers[0].file_header;
    output.info_header = carriers[0].info_header;
    output.file_header.bfReserved1 = 0;   /* the secret is not a shadow */
    output.file_header.bfReserved2 = 0;
    output.width       = carriers[0].width;
    output.height      = carriers[0].height;
    output.row_size    = carriers[0].row_size;
    output.pixel_bytes = carriers[0].pixel_bytes;

    if (carriers[0].palette && carriers[0].palette_size > 0) {
        output.palette_size = carriers[0].palette_size;
        output.palette = malloc(output.palette_size);
        if (!output.palette) { ret = SSS_DATA; goto cleanup; }
        memcpy(output.palette, carriers[0].palette, output.palette_size);
    }

    output.pixels = malloc(secret_size);
    if (!output.pixels) { ret = SSS_DATA; goto cleanup; }
    memcpy(output.pixels, secret_bytes, secret_size);

    if (bmp_write(secret_path, &output) != 0) {
        fprintf(stderr, "Error: could not write recovered secret '%s'.\n",
                secret_path);
        ret = SSS_IO;
        goto cleanup;
    }

    printf("Info: recovered secret '%s' from %d shadow(s) with seed %u.\n",
           secret_path, k, (unsigned)seed);
    ret = SSS_OK;

cleanup:
    if (carriers) {
        for (int i = 0; i < carriers_read; i++) bmp_free(&carriers[i]);
        free(carriers);
    }
    if (carrier_paths) {
        for (int i = 0; i < carrier_count; i++) free(carrier_paths[i]);
        free(carrier_paths);
    }
    free(secret_bytes);
    bmp_free(&output);
    return ret;
}
