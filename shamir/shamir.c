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

        size_t carrier_size = (size_t)carriers[i].width * carriers[i].height;
        if (carrier_size < pixels_needed) {
            fprintf(stderr,
                    "Error: carrier '%s' has %zu pixels but %zu are needed "
                    "to embed the shadow.\n",
                    carrier_paths[i], carrier_size, pixels_needed);
            goto cleanup;
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

        while (1) {
            int collision = 0;
            for (int j = 0; j < n; j++) {
                int result = poly_eval(coefs, k, j + 1);
                if (result == 256) { collision = 1; break; }
                shadow_byte[j] = (uint8_t)result;
            }
            if (!collision) break;

            if (coefs[0] == 0) {
                fprintf(stderr,
                        "Error: irreducible collision at block %zu.\n", block);
                goto cleanup;
            }
            coefs[0]--;
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
    (void)secret_path; (void)k; (void)dir;
    UNIMPLEMENTED();
}
