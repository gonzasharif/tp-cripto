#include "shamir.h"

static int SEED = 10;
char * CARRIER_PATH = "..";

int distribute(const char * secret_path, int k, int n, const char * dir) {
    UNIMPLEMENTED();

    // 1. Read secret
    BMPImage secret;
    if (bmp_read(secret_path, &secret) != 0) {
        return 1;
    }

    // 2. Validate secret size
    size_t total = (size_t)secret.width * secret.height;
    if (total % k != 0) {
        bmp_free(&secret);
        return 1;
    }

    // 3. List and read n carriets
    BMPImage * carriers = malloc(n * sizeof(BMPImage));

    // 4. Generate seed
    uint16_t seed = (uint16_t) SEED;
    setSeed(seed);
    uint8_t * perm = malloc(total);

    for (size_t i = 0; i < total; i++) {
        perm[i] = nextChar();
    }

    // 5. build polynomials
    for (int j = 0; j < n; j++) {
        carriers[j].file_header.bfReserved1 = seed;
        bmp_write(CARRIER_PATH, &carriers[j]);
        bmp_free(&carriers[j]);
    }

    free(perm);
    free(carriers);
    bmp_free(&secret);

    return 0;    

}

int recovery(const char * secret_path, int k, const char * dir) {
    UNIMPLEMENTED();
}