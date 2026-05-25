#include <string.h>
#include <time.h>

#include "shamir.h"
#include "../utils/utils.h"

#define MOD 257   /* Prime modulus for polynomial evaluation in GF(257). */

/* ─── Helpers ─────────────────────────────────────────────────── */

/* Evaluate p(x) = c[0] + c[1]*x + c[2]*x^2 + ... + c[k-1]*x^{k-1}  (mod MOD). */
static int poly_eval(const uint8_t *coefs, int k, int x) {
    int result = 0;
    int power  = 1;
    for (int i = 0; i < k; i++) {
        result = (result + (int)coefs[i] * power) % MOD;
        power  = (power * x) % MOD;
    }
    return result;
}

/* Embed the 8 bits of `byte` into the LSBs of 8 consecutive carrier
 * pixels starting at pixels[offset]. Most-significant bit first. */
static void embed_byte_lsb(uint8_t *pixels, size_t offset, uint8_t byte) {
    for (int b = 0; b < 8; b++) {
        uint8_t bit = (byte >> (7 - b)) & 1;
        pixels[offset + b] = (pixels[offset + b] & 0xFE) | bit;
    }
}

/* Inverse of embed_byte_lsb: read 8 LSBs as a single byte, MSB-first. */
static uint8_t extract_byte_lsb(const uint8_t *pixels, size_t offset) {
    uint8_t byte = 0;
    for (int b = 0; b < 8; b++) {
        byte = (uint8_t)((byte << 1) | (pixels[offset + b] & 1));
    }
    return byte;
}

/* Modular exponentiation: base^exp mod m. */
static int mod_pow(int base, int exp, int m) {
    long result = 1;
    long b = base % m;
    if (b < 0) b += m;
    while (exp > 0) {
        if (exp & 1) result = result * b % m;
        b = b * b % m;
        exp >>= 1;
    }
    return (int)result;
}

/* Modular inverse via Fermat's little theorem (valid since MOD is prime). */
static int mod_inv(int a, int m) {
    return mod_pow(a, m - 2, m);
}

/* Solve the k x k linear system in GF(MOD) given in augmented form
 * [A | y] (k rows, k+1 columns). Modifies `mat` in place; on success
 * writes the k-element solution vector into `out`. Returns 0 on success
 * or 1 if the matrix is singular. */
static int gauss_solve(int mat[10][11], int k, int *out) {
    for (int col = 0; col < k; col++) {
        /* Find a non-zero pivot in column `col`. */
        int pivot = -1;
        for (int row = col; row < k; row++) {
            if (mat[row][col] != 0) { pivot = row; break; }
        }
        if (pivot < 0) return 1;

        if (pivot != col) {
            for (int j = 0; j <= k; j++) {
                int tmp = mat[col][j];
                mat[col][j]   = mat[pivot][j];
                mat[pivot][j] = tmp;
            }
        }

        /* Scale pivot row so the diagonal entry becomes 1. */
        int inv = mod_inv(mat[col][col], MOD);
        for (int j = col; j <= k; j++) {
            mat[col][j] = (int)((long)mat[col][j] * inv % MOD);
        }

        /* Eliminate every other row's entry in column `col`. */
        for (int row = 0; row < k; row++) {
            if (row == col || mat[row][col] == 0) continue;
            int factor = mat[row][col];
            for (int j = col; j <= k; j++) {
                long v = mat[row][j] - (long)factor * mat[col][j];
                v %= MOD;
                if (v < 0) v += MOD;
                mat[row][j] = (int)v;
            }
        }
    }

    for (int i = 0; i < k; i++) out[i] = mat[i][k];
    return 0;
}

/* ═══════════════════════════════════════════════════════════════ */

int distribute(const char *secret_path, int k, int n, const char *dir) {
    int       ret           = 1;     /* failure by default */
    BMPImage  secret        = {0};
    BMPImage *carriers      = NULL;
    char    **carrier_paths = NULL;
    int       carrier_count = 0;
    int       carriers_read = 0;
    uint8_t  *perm          = NULL;

    /* ── 1. Read secret ───────────────────────────────────────── */
    if (bmp_read(secret_path, &secret) != 0) {
        fprintf(stderr, "Error: could not read secret '%s'.\n", secret_path);
        return 1;
    }

    size_t secret_size = (size_t)secret.width * secret.height;
    if (secret_size % (size_t)k != 0) {
        fprintf(stderr,
                "Error: secret size (%zu px) must be divisible by k=%d.\n",
                secret_size, k);
        goto cleanup;
    }
    size_t shadow_bytes  = secret_size / (size_t)k;
    size_t pixels_needed = 8 * shadow_bytes;   /* 1 LSB per carrier pixel */

    /* ── 2. List carriers from `dir` (excluding the secret) ──── */
    const char *secret_basename = strrchr(secret_path, '/');
    secret_basename = secret_basename ? secret_basename + 1 : secret_path;

    carrier_count = list_bmp_files(dir, secret_basename, &carrier_paths);
    if (carrier_count < 0) {
        fprintf(stderr, "Error: could not list BMPs in '%s'.\n", dir);
        goto cleanup;
    }
    if (carrier_count < n) {
        fprintf(stderr,
                "Error: need %d carrier images but found %d in '%s'.\n",
                n, carrier_count, dir);
        goto cleanup;
    }

    /* ── 3. Read first n carriers and validate they are big enough ── */
    carriers = calloc((size_t)n, sizeof(BMPImage));
    if (!carriers) goto cleanup;

    for (int i = 0; i < n; i++) {
        if (bmp_read(carrier_paths[i], &carriers[i]) != 0) {
            fprintf(stderr, "Error: could not read carrier '%s'.\n",
                    carrier_paths[i]);
            goto cleanup;
        }
        carriers_read++;

        /* For k == 8 the spec requires exact width and height match
         * with the secret. For other k the carrier just needs enough
         * pixels to host the shadow (1 LSB per pixel). */
        if (k == 8) {
            if (carriers[i].width  != secret.width ||
                carriers[i].height != secret.height) {
                fprintf(stderr,
                        "Error: with k=8, carrier '%s' must be %ux%u "
                        "(matches secret) but is %ux%u.\n",
                        carrier_paths[i],
                        secret.width, secret.height,
                        carriers[i].width, carriers[i].height);
                goto cleanup;
            }
        } else {
            size_t carrier_size = (size_t)carriers[i].width * carriers[i].height;
            if (carrier_size < pixels_needed) {
                fprintf(stderr,
                        "Error: carrier '%s' has %zu pixels but %zu are needed "
                        "to embed the shadow.\n",
                        carrier_paths[i], carrier_size, pixels_needed);
                goto cleanup;
            }
        }
    }

    /* ── 4. Generate seed and permute the secret with PRNG XOR mask.
     *
     * Following the Thien-Lin scheme, we XOR each secret byte with a
     * pseudo-random byte from the LCG. The mask is reversible by
     * XORing again with the same stream, so recovery only needs the
     * seed (stored in the carrier header below). */
    uint16_t seed = (uint16_t)(time(NULL) & 0xFFFF);
    setSeed((int64_t)seed);

    perm = malloc(secret_size);
    if (!perm) goto cleanup;
    for (size_t i = 0; i < secret_size; i++) {
        perm[i] = secret.pixels[i] ^ nextChar();
    }

    /* ── 5. Evaluate polynomials and embed shadow bytes ─────────
     *
     * The permuted secret is consumed in blocks of k bytes. Each block
     * forms a polynomial of degree k-1 (the k bytes are the
     * coefficients). For shadow j ∈ {1..n} we evaluate p(j) mod 257
     * and embed the resulting byte in 8 consecutive LSBs of carrier
     * j-1.
     *
     * Because MOD = 257 the evaluation can produce 256, which does
     * not fit in a byte. Standard Thien-Lin workaround: when any
     * shadow lands on 256, decrement coefs[0] by 1 and re-evaluate
     * the whole block. This makes the scheme slightly lossy (one
     * pixel per affected block is recovered with a value −1 from the
     * original), but keeps everything inside a single byte. */
    for (size_t block = 0; block < shadow_bytes; block++) {
        uint8_t *coefs = perm + block * (size_t)k;
        uint8_t  shadow_byte[10];   /* k_max = n_max = 10 from the CLI spec */
        int      original_a0 = coefs[0];
        int      found = 0;

        /* Try a_0 ∈ {original, original±1, original±2, ...} in order of
         * increasing distance. Each j has exactly one "bad" a_0 that
         * makes p(j) == 256, so across all n shadows at most n values
         * of a_0 fail and at least 256 - n always work. */
        for (int dist = 0; dist <= 255 && !found; dist++) {
            for (int sign = (dist == 0 ? 1 : -1); sign <= 1 && !found; sign += 2) {
                int candidate = original_a0 + sign * dist;
                if (candidate < 0 || candidate > 255) continue;
                coefs[0] = (uint8_t)candidate;

                int collision = 0;
                for (int j = 0; j < n; j++) {
                    int result = poly_eval(coefs, k, j + 1);
                    if (result == 256) { collision = 1; break; }
                    shadow_byte[j] = (uint8_t)result;
                }
                if (!collision) found = 1;
            }
        }
        if (!found) {
            fprintf(stderr,
                    "Error: no usable a_0 found for block %zu (should be "
                    "impossible for n <= 10).\n", block);
            goto cleanup;
        }

        for (int j = 0; j < n; j++) {
            embed_byte_lsb(carriers[j].pixels, block * 8, shadow_byte[j]);
        }
    }

    /* ── 6. Stamp header (seed + shadow index) and write carriers ── */
    for (int j = 0; j < n; j++) {
        carriers[j].file_header.bfReserved1 = seed;
        carriers[j].file_header.bfReserved2 = (uint16_t)(j + 1);

        if (bmp_write(carrier_paths[j], &carriers[j]) != 0) {
            fprintf(stderr, "Error: could not write carrier '%s'.\n",
                    carrier_paths[j]);
            goto cleanup;
        }
    }

    printf("Info: distributed secret into %d shadow(s) with seed %u.\n",
           n, (unsigned)seed);
    ret = 0;

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
    /* The spec only fixes the carrier sizing for k=8. For other k the
     * group must define a convention; here we only support k=8 to keep
     * the recovery side aligned with the explicit part of the spec. */
    if (k != 8) {
        fprintf(stderr,
                "Error: recovery is currently only supported for k=8 "
                "(the case fixed by the spec).\n");
        return 1;
    }

    int       ret           = 1;
    BMPImage *carriers      = NULL;
    char    **carrier_paths = NULL;
    int       carrier_count = 0;
    int       carriers_read = 0;
    uint8_t  *secret_bytes  = NULL;
    BMPImage  output        = {0};

    /* ── 1. List BMPs in `dir` (excluding the target secret path) ── */
    const char *secret_basename = strrchr(secret_path, '/');
    secret_basename = secret_basename ? secret_basename + 1 : secret_path;

    carrier_count = list_bmp_files(dir, secret_basename, &carrier_paths);
    if (carrier_count < 0) {
        fprintf(stderr, "Error: could not list BMPs in '%s'.\n", dir);
        goto cleanup;
    }
    if (carrier_count < k) {
        fprintf(stderr,
                "Error: need at least %d carrier images but found %d in '%s'.\n",
                k, carrier_count, dir);
        goto cleanup;
    }

    /* ── 2. Read first k carriers ─────────────────────────────── */
    carriers = calloc((size_t)k, sizeof(BMPImage));
    if (!carriers) goto cleanup;

    for (int i = 0; i < k; i++) {
        if (bmp_read(carrier_paths[i], &carriers[i]) != 0) {
            fprintf(stderr, "Error: could not read carrier '%s'.\n",
                    carrier_paths[i]);
            goto cleanup;
        }
        carriers_read++;
    }

    /* ── 3. Validate same dimensions (spec requirement for k=8) ── */
    for (int i = 1; i < k; i++) {
        if (carriers[i].width  != carriers[0].width ||
            carriers[i].height != carriers[0].height) {
            fprintf(stderr,
                    "Error: with k=8, all carriers must share dimensions. "
                    "'%s' is %ux%u but '%s' is %ux%u.\n",
                    carrier_paths[0], carriers[0].width, carriers[0].height,
                    carrier_paths[i], carriers[i].width, carriers[i].height);
            goto cleanup;
        }
    }

    size_t secret_size  = (size_t)carriers[0].width * carriers[0].height;
    size_t shadow_bytes = secret_size / (size_t)k;

    /* ── 4. Read seed (header bytes 6-7) and shadow indices (8-9) ── */
    uint16_t seed = carriers[0].file_header.bfReserved1;

    int x_values[10];
    for (int i = 0; i < k; i++) {
        x_values[i] = (int)carriers[i].file_header.bfReserved2;
        if (x_values[i] < 1) {
            fprintf(stderr,
                    "Error: invalid shadow index %d in carrier '%s'.\n",
                    x_values[i], carrier_paths[i]);
            goto cleanup;
        }
    }
    /* Duplicate x-values would make the Vandermonde matrix singular. */
    for (int i = 0; i < k; i++) {
        for (int j = i + 1; j < k; j++) {
            if (x_values[i] == x_values[j]) {
                fprintf(stderr,
                        "Error: duplicate shadow index %d in '%s' and '%s'.\n",
                        x_values[i], carrier_paths[i], carrier_paths[j]);
                goto cleanup;
            }
        }
    }

    /* ── 5. For each block, interpolate the polynomial mod 257.
     *
     * For block b we have k pairs (x_i, p(x_i)) where p has degree
     * k-1. Solving V * c = y (with V the Vandermonde matrix of the
     * x-values) yields the k coefficients c[0..k-1], which are
     * exactly the k permuted secret bytes of that block. */
    secret_bytes = malloc(secret_size);
    if (!secret_bytes) goto cleanup;

    for (size_t block = 0; block < shadow_bytes; block++) {
        uint8_t y[10];
        for (int i = 0; i < k; i++) {
            y[i] = extract_byte_lsb(carriers[i].pixels, block * 8);
        }

        int mat[10][11];
        for (int i = 0; i < k; i++) {
            int power = 1;
            for (int j = 0; j < k; j++) {
                mat[i][j] = power;
                power = (int)((long)power * x_values[i] % MOD);
            }
            mat[i][k] = (int)y[i];
        }

        int coefs[10];
        if (gauss_solve(mat, k, coefs) != 0) {
            fprintf(stderr,
                    "Error: singular Vandermonde matrix at block %zu.\n", block);
            goto cleanup;
        }
        for (int i = 0; i < k; i++) {
            secret_bytes[block * (size_t)k + (size_t)i] = (uint8_t)coefs[i];
        }
    }

    /* ── 6. Undo the PRNG XOR mask using the recovered seed ──── */
    setSeed((int64_t)seed);
    for (size_t i = 0; i < secret_size; i++) {
        secret_bytes[i] ^= nextChar();
    }

    /* ── 7. Build output BMP using carrier[0]'s header + palette ── */
    output.file_header = carriers[0].file_header;
    output.info_header = carriers[0].info_header;
    output.file_header.bfReserved1 = 0;   /* the secret is not a shadow */
    output.file_header.bfReserved2 = 0;
    output.width  = carriers[0].width;
    output.height = carriers[0].height;

    if (carriers[0].palette && carriers[0].palette_size > 0) {
        output.palette_size = carriers[0].palette_size;
        output.palette = malloc(output.palette_size);
        if (!output.palette) goto cleanup;
        memcpy(output.palette, carriers[0].palette, output.palette_size);
    }

    output.pixels = malloc(secret_size);
    if (!output.pixels) goto cleanup;
    memcpy(output.pixels, secret_bytes, secret_size);

    if (bmp_write(secret_path, &output) != 0) {
        fprintf(stderr, "Error: could not write recovered secret '%s'.\n",
                secret_path);
        goto cleanup;
    }

    printf("Info: recovered secret '%s' from %d shadow(s) with seed %u.\n",
           secret_path, k, (unsigned)seed);
    ret = 0;

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
