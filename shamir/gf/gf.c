#include "gf.h"

int gf_poly_eval(const uint8_t *coefs, int k, int x) {
    int result = 0;
    int power  = 1;
    for (int i = 0; i < k; i++) {
        result = (result + (int)coefs[i] * power) % GF_MOD;
        power  = (power * x) % GF_MOD;
    }
    return result;
}

int gf_mod_pow(int base, int exp, int m) {
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

int gf_mod_inv(int a, int m) {
    return gf_mod_pow(a, m - 2, m);
}

int gf_gauss_solve(int mat[GF_K_MAX][GF_K_MAX + 1], int k, int *out) {
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
        int inv = gf_mod_inv(mat[col][col], GF_MOD);
        for (int j = col; j <= k; j++) {
            mat[col][j] = (int)((long)mat[col][j] * inv % GF_MOD);
        }

        /* Eliminate every other row's entry in column `col`. */
        for (int row = 0; row < k; row++) {
            if (row == col || mat[row][col] == 0) continue;
            int factor = mat[row][col];
            for (int j = col; j <= k; j++) {
                long v = mat[row][j] - (long)factor * mat[col][j];
                v %= GF_MOD;
                if (v < 0) v += GF_MOD;
                mat[row][j] = (int)v;
            }
        }
    }

    for (int i = 0; i < k; i++) out[i] = mat[i][k];
    return 0;
}
