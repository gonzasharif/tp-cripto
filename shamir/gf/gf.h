#ifndef GF_H
#define GF_H

#include <stdint.h>

/* Prime modulus used throughout the Shamir scheme (GF(257)). */
#define GF_MOD 257

/* Maximum k supported by the CLI; sizes the matrix passed to gf_gauss_solve. */
#define GF_K_MAX 10

/**
 * Evaluate p(x) = c[0] + c[1]·x + c[2]·x² + ... + c[k-1]·x^{k-1}  (mod GF_MOD).
 *
 * @param coefs  Pointer to the k coefficients (a_0 first). Must not be NULL.
 * @param k      Number of coefficients (degree + 1). Must satisfy 1 ≤ k ≤ GF_K_MAX.
 * @param x      Evaluation point. Typically in [1, n].
 *
 * @return p(x) mod GF_MOD, a value in [0, GF_MOD - 1] (i.e. [0, 256]).
 *         Note that a value of 256 will not fit in a byte; callers that
 *         need to embed the result must handle that case explicitly.
 */
int gf_poly_eval(const uint8_t *coefs, int k, int x);

/**
 * Modular exponentiation: returns base^exp mod m using fast squaring.
 *
 * @param base  Base. May be negative; the result is normalised to [0, m-1].
 * @param exp   Non-negative exponent.
 * @param m     Modulus. Must be positive.
 *
 * @return base^exp mod m.
 */
int gf_mod_pow(int base, int exp, int m);

/**
 * Modular multiplicative inverse, valid only when `m` is prime (uses
 * Fermat's little theorem: a^(m-2) ≡ a⁻¹ mod m).
 *
 * @param a  Element to invert. Must not be 0 mod m.
 * @param m  Prime modulus.
 *
 * @return a⁻¹ mod m.
 */
int gf_mod_inv(int a, int m);

/**
 * Solve the k×k linear system in GF(GF_MOD) given in augmented form
 * [A | y] (k rows, k+1 columns) via Gauss-Jordan elimination.
 *
 * @param mat  Augmented matrix, modified in place. Must have at least k rows
 *             and k+1 columns. Fixed to GF_K_MAX x (GF_K_MAX+1) for simplicity.
 * @param k    Number of equations / unknowns. Must satisfy 1 ≤ k ≤ GF_K_MAX.
 * @param out  Output buffer of length ≥ k. Receives the solution vector
 *             on success. Untouched on failure.
 *
 * @return 0 on success, 1 if the matrix is singular (no unique solution).
 */
int gf_gauss_solve(int mat[GF_K_MAX][GF_K_MAX + 1], int k, int *out);

#endif /* GF_H */
