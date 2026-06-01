#include <string.h>
#include <time.h>

#include "shamir.h"
#include "gf/gf.h"
#include "lsb/lsb.h"
#include "../utils/utils.h"

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
     * Because GF_MOD = 257 an evaluation can produce 256, which does not
     * fit in a byte. We follow Wu-Lo Step 5 verbatim: whenever any
     * shadow lands on 256, decrement the first non-zero coefficient of
     * the block by one and re-evaluate every shadow. The paper proves an
     * all-zero block can never yield 256 (it evaluates to 0), so a
     * non-zero coefficient always exists; each step lowers the
     * coefficient sum by one, so the loop terminates. */
    for (size_t block = 0; block < shadow_bytes; block++) {
        uint8_t *coefs = perm + block * (size_t)k;
        uint8_t  shadow_byte[GF_K_MAX];

        for (;;) {
            int collision = 0;
            for (int j = 0; j < n; j++) {
                int result = gf_poly_eval(coefs, k, j + 1);
                if (result == 256) { collision = 1; break; }
                shadow_byte[j] = (uint8_t)result;
            }
            if (!collision) break;

            int idx = 0;
            while (idx < k && coefs[idx] == 0) idx++;
            coefs[idx]--;   /* idx < k guaranteed: all-zero can't hit 256 */
        }

        for (int j = 0; j < n; j++) {
            lsb_embed_byte(carriers[j].pixels, block * 8, shadow_byte[j]);
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

    int x_values[GF_K_MAX];
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
        uint8_t y[GF_K_MAX];
        for (int i = 0; i < k; i++) {
            y[i] = lsb_extract_byte(carriers[i].pixels, block * 8);
        }

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
        if (gf_gauss_solve(mat, k, coefs) != 0) {
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
