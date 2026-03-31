/*
 * quadr_xxh3.c  –  Portable XXH3-64bit hash
 *
 * Stripped-down, self-contained implementation sufficient for block
 * integrity checks in Quadr v1.5.  Not the full xxHash library.
 *
 * Based on the XXH3 algorithm by Yann Collet (public domain / BSD 2-clause).
 * Reference: https://github.com/Cyan4973/xxHash
 */

#include "quadr.h"
#include "quadr_platform.h"
#include <string.h>

/* ─── constants ────────────────────────────────────────────────────────── */

#define XXH_PRIME_1  UINT64_C(0x9E3779B185EBCA87)
#define XXH_PRIME_2  UINT64_C(0xC2B2AE3D27D4EB4F)
#define XXH_PRIME_3  UINT64_C(0x165667B19E3779F9)
#define XXH_PRIME_4  UINT64_C(0x85EBCA77C2B2AE63)
#define XXH_PRIME_5  UINT64_C(0x27D4EB2F165667C5)

#define XXH3_SECRET_SIZE 192

/* Default secret derived from the primes */
static const uint8_t xxh3_default_secret[XXH3_SECRET_SIZE] = {
    0xb8,0xfe,0x6c,0x39,0x23,0xa4,0x4b,0xbe,0x7c,0x01,0x81,0x2c,0xf7,0x21,0xad,0x1c,
    0xde,0xd4,0x6d,0xe9,0x83,0x90,0x97,0xdb,0x72,0x40,0xa4,0xa4,0xb7,0xb3,0x67,0x1f,
    0xcb,0x79,0xe6,0x4e,0xcc,0xc0,0xe5,0x78,0x82,0x5a,0xd0,0x7d,0xcc,0xff,0x72,0x21,
    0xb8,0x08,0x46,0x74,0xf7,0x43,0x24,0x8e,0xe0,0x35,0x90,0xe6,0x81,0x3a,0x26,0x4c,
    0x3c,0x28,0x52,0xbb,0x91,0xc3,0x00,0xcb,0x88,0xd0,0x65,0x8b,0x1b,0x53,0x2e,0xa3,
    0x71,0x64,0x48,0x97,0xa2,0x0d,0xf9,0x4e,0x38,0x19,0xef,0x46,0xa9,0xde,0xac,0xd8,
    0xa8,0xfa,0x76,0x3f,0xe3,0x9c,0x34,0x3f,0xf9,0xdc,0xbb,0xc7,0xc7,0x0b,0x4f,0x1d,
    0x8a,0x51,0xe0,0x4b,0xcd,0xb4,0x59,0x31,0xc8,0x9f,0x7e,0xc9,0xd9,0x78,0x73,0x64,
    0xea,0xc5,0xac,0x83,0x34,0xd3,0xeb,0xc3,0xc5,0x81,0xa0,0xff,0xfa,0x13,0x63,0xeb,
    0x17,0x0d,0xdd,0x51,0xb7,0xf0,0xda,0x49,0xd3,0x16,0x55,0x26,0x29,0xd4,0x68,0x9e,
    0x2b,0x16,0xbe,0x58,0x7d,0x47,0xa1,0xfc,0x8f,0xf8,0xb8,0xd1,0x7a,0xd0,0x31,0xce,
    0x45,0xcb,0x3a,0x8f,0x95,0x16,0x04,0x28,0xaf,0xd7,0xfb,0xca,0xbb,0x4b,0x40,0x7e,
};

/* ─── helpers ──────────────────────────────────────────────────────────── */

static inline uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}
static inline uint64_t xxh3_avalanche(uint64_t h) {
    h ^= h >> 37;
    h *= UINT64_C(0x165667919E3779F9);
    h ^= h >> 32;
    return h;
}
static inline uint64_t xxh3_mix16(const uint8_t *p, const uint8_t *secret, uint64_t seed) {
    uint64_t lo, hi;
    memcpy(&lo, p,   8);
    memcpy(&hi, p+8, 8);
    uint64_t sl, sh;
    memcpy(&sl, secret,   8);
    memcpy(&sh, secret+8, 8);
    uint64_t a = (lo ^ (sl + seed));
    uint64_t b = (hi ^ (sh - seed));
    /* 128-bit multiply: XOR of high and low halves */
    return quadr_mul128_high(a, b) ^ quadr_mul128_low(a, b);
}

/* ─── main entry ──────────────────────────────────────────────────────── */

uint64_t quadr_xxh3_64(const void *data, size_t len) {
    const uint8_t *p   = (const uint8_t *)data;
    const uint8_t *sec = xxh3_default_secret;
    uint64_t seed      = 0;
    uint64_t h64;

    if (len <= 16) {
        /* Short input (0–16 bytes) */
        if (len == 0) {
            uint64_t s0, s1;
            memcpy(&s0, sec+56, 8);
            memcpy(&s1, sec+64, 8);
            return xxh3_avalanche(seed ^ s0 ^ s1);
        }
        if (len <= 3) {
            uint32_t c1 = p[0];
            uint32_t c2 = p[len>>1];
            uint32_t c3 = p[len-1];
            uint32_t combined = (c1 << 16) | (c2 << 24) | c3 | ((uint32_t)len << 8);
            uint64_t s0, s1;
            memcpy(&s0, sec, 8);
            memcpy(&s1, sec+8, 8);
            h64 = xxh3_avalanche((s0 ^ s1) ^ (uint64_t)combined);
            return h64;
        }
        /* 4–16 bytes */
        uint64_t lo, hi;
        memcpy(&lo, p,         (len>=8)?8:4);
        memcpy(&hi, p+len-8,   (len>=8)?8:4);
        uint64_t s0, s1, s2, s3;
        memcpy(&s0, sec,    8); memcpy(&s1, sec+8,  8);
        memcpy(&s2, sec+16, 8); memcpy(&s3, sec+24, 8);
        h64 = rotl64((lo^(s0+seed)) + (hi^(s1-seed)), 17) * XXH_PRIME_4;
        h64 ^= (h64>>32); h64 *= XXH_PRIME_2; h64 ^= (h64>>29);
        return h64;
    }

    if (len <= 128) {
        uint64_t acc = (uint64_t)len * XXH_PRIME_1;
        size_t pairs = len / 16;
        for (size_t i = 0; i < pairs; i++)
            acc += xxh3_mix16(p + i*16, sec + i*16, seed);
        /* last 16 bytes */
        acc += xxh3_mix16(p + len - 16, sec + (XXH3_SECRET_SIZE-16-1), seed);
        return xxh3_avalanche(acc);
    }

    /* Long (>128 bytes): simplified accumulator */
    uint64_t acc[8] = {
        XXH_PRIME_1 + XXH_PRIME_2, XXH_PRIME_2, 0, (uint64_t)-(int64_t)XXH_PRIME_1,
        XXH_PRIME_1, (uint64_t)-(int64_t)(XXH_PRIME_1+XXH_PRIME_2), 0, XXH_PRIME_2,
    };

    size_t blocks    = len / 64;
    size_t stripes   = 8;  /* 64-byte block = 8 × 8-byte stripes */
    const uint8_t *q = p;

    for (size_t block = 0; block < blocks; block++, q += 64) {
        for (size_t s = 0; s < stripes; s++) {
            uint64_t data_val, sv;
            memcpy(&data_val, q + s*8, 8);
            memcpy(&sv,       sec + (s%23)*8, 8);
            uint64_t mixed = data_val ^ sv;
            acc[s % 8] += mixed;
            acc[s % 8] ^= data_val >> 11;
        }
    }

    /* last partial block */
    size_t remaining = len - blocks * 64;
    if (remaining) {
        const uint8_t *tail = p + len - 64;
        for (size_t s = 0; s < 8; s++) {
            uint64_t dv, sv;
            memcpy(&dv, tail + s*8, 8);
            memcpy(&sv, sec + (s%23)*8, 8);
            acc[s % 8] += dv ^ sv;
        }
    }

    /* merge */
    h64 = (uint64_t)len * XXH_PRIME_1;
    for (int i = 0; i < 8; i++) {
        uint64_t sv;
        memcpy(&sv, sec + 8 + i*8, 8);
        h64 += rotl64(acc[i] ^ sv, 17) * XXH_PRIME_4;
    }
    return xxh3_avalanche(h64);
}
