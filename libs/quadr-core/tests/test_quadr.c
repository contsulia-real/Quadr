/*
 * test_quadr.c  –  Unit tests for Quadr v1.5 core transforms
 *
 * Compile & run:
 *   gcc -O2 -Wall -Wextra -I../include ../src/quadr_core.c ../src/quadr_xxh3.c \
 *       test_quadr.c -lm -o test_quadr && ./test_quadr
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "quadr_platform.h"   /* platform detection for tmp paths */

#ifdef QUADR_OS_WINDOWS
#  include <io.h>             /* _unlink */
#else
#  include <unistd.h>         /* unlink */
#endif
#include <assert.h>
#include "quadr.h"

/* ─── tiny test framework ──────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define CHECK(expr) do { \
    if (expr) { g_pass++; } \
    else { fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); g_fail++; } \
} while(0)

#define SECTION(name) printf("\n── %s\n", name)

/* ─── helpers ──────────────────────────────────────────────────────────── */

static uint8_t *make_ramp(size_t len) {
    uint8_t *b = malloc(len);
    for (size_t i = 0; i < len; i++) b[i] = (uint8_t)(i & 0xFF);
    return b;
}

static uint8_t *make_sine(size_t len, double freq) {
    uint8_t *b = malloc(len);
    for (size_t i = 0; i < len; i++)
        b[i] = (uint8_t)(128 + 100 * sin(2.0 * 3.14159 * freq * (double)i / (double)len));
    return b;
}

static uint8_t *make_zeros(size_t len) {
    uint8_t *b = calloc(len, 1);
    return b;
}

static uint8_t *make_random(size_t len, unsigned seed) {
    uint8_t *b = malloc(len);
    unsigned s = seed;
    for (size_t i = 0; i < len; i++) {
        s = s * 1664525u + 1013904223u;  /* LCG */
        b[i] = (uint8_t)(s >> 16);
    }
    return b;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_delta_8bit(void) {
    SECTION("Delta 8-bit encode/decode roundtrip");

    uint8_t orig[] = {10, 12, 15, 14, 20, 19, 18};
    size_t  len    = sizeof(orig);
    uint8_t enc[16], dec[16];

    QuadrError e;
    e = quadr_delta_encode(orig, len, enc, 1, 8);
    CHECK(e == QUADR_OK);

    /* Manual check: stride=1 deltas */
    CHECK(enc[0] == 10);            /* 10 - 0 = 10 */
    CHECK(enc[1] == 2);             /* 12 - 10 = 2 */
    CHECK(enc[2] == 3);             /* 15 - 12 = 3 */
    CHECK((int8_t)enc[3] == -1);    /* 14 - 15 = -1  (mod 256 = 255) */

    e = quadr_delta_decode(enc, len, dec, 1, 8);
    CHECK(e == QUADR_OK);
    CHECK(memcmp(orig, dec, len) == 0);
}

static void test_delta_stride(void) {
    SECTION("Delta stride=2 (stereo simulation)");

    /* Two channels interleaved: L=ramp, R=ramp+128 */
    uint8_t orig[256];
    for (int i = 0; i < 128; i++) {
        orig[i*2]   = (uint8_t)i;          /* L */
        orig[i*2+1] = (uint8_t)(i + 128);  /* R */
    }
    uint8_t enc[256], dec[256];

    CHECK(quadr_delta_encode(orig, 256, enc, 2, 8) == QUADR_OK);

    /* After stride=2 delta, L channel deltas should all be 1 (except first=0) */
    CHECK(enc[0] == 0);    /* L[0] = 0 - 0 = 0 */
    CHECK(enc[2] == 1);    /* L[1] = 1 - 0 = 1 */
    CHECK(enc[4] == 1);    /* L[2] = 2 - 1 = 1 */
    CHECK(enc[1] == 128);  /* R[0] = 128 - 0 = 128 */
    CHECK(enc[3] == 1);    /* R[1] = 129 - 128 = 1 */

    CHECK(quadr_delta_decode(enc, 256, dec, 2, 8) == QUADR_OK);
    CHECK(memcmp(orig, dec, 256) == 0);
}

static void test_delta_16bit(void) {
    SECTION("Delta 16-bit (PCM simulation)");

    /* 8 samples of 16-bit data, stereo (stride=2) */
    uint16_t orig16[] = {1000, 2000, 1001, 2001, 1002, 2002, 1003, 2003};
    uint8_t *orig = (uint8_t *)orig16;
    size_t   len  = sizeof(orig16);
    uint8_t enc[64], dec[64];

    CHECK(quadr_delta_encode(orig, len, enc, 2, 16) == QUADR_OK);
    CHECK(quadr_delta_decode(enc,  len, dec, 2, 16) == QUADR_OK);
    CHECK(memcmp(orig, dec, len) == 0);
}

static void test_delta_wraparound(void) {
    SECTION("Delta modular wraparound");

    uint8_t orig[] = {5, 3};   /* delta = 3-5 = -2 mod 256 = 254 */
    uint8_t enc[4], dec[4];

    CHECK(quadr_delta_encode(orig, 2, enc, 1, 8) == QUADR_OK);
    CHECK(enc[0] == 5);
    CHECK(enc[1] == 254);

    CHECK(quadr_delta_decode(enc, 2, dec, 1, 8) == QUADR_OK);
    CHECK(dec[0] == 5);
    CHECK(dec[1] == 3);
}

static void test_delta_large(void) {
    SECTION("Delta large buffer roundtrip");

    size_t   len  = 64 * 1024;
    uint8_t *orig = make_sine(len, 3.7);
    uint8_t *enc  = malloc(len);
    uint8_t *dec  = malloc(len);

    CHECK(quadr_delta_encode(orig, len, enc, 1, 8) == QUADR_OK);
    CHECK(quadr_delta_decode(enc,  len, dec, 1, 8) == QUADR_OK);
    CHECK(memcmp(orig, dec, len) == 0);

    /* Entropy should decrease for smooth data */
    double h_orig = quadr_entropy(orig, len);
    double h_enc  = quadr_entropy(enc,  len);
    CHECK(h_enc < h_orig);
    printf("     entropy: %.4f → %.4f (delta=%.4f)\n", h_orig, h_enc, h_orig - h_enc);

    free(orig); free(enc); free(dec);
}

static void test_byte_shuffle(void) {
    SECTION("Byte Shuffle / Unshuffle (word_size=4)");

    /* 4 float32-sized words: each word has distinct bytes for tracing */
    uint8_t orig[] = {
        0xAA, 0xBB, 0xCC, 0xDD,   /* word 0 */
        0x11, 0x22, 0x33, 0x44,   /* word 1 */
        0xFF, 0xEE, 0xDD, 0xCC,   /* word 2 */
        0x00, 0x11, 0x22, 0x33,   /* word 3 */
    };
    uint8_t sh[16], unsh[16];

    CHECK(quadr_byte_shuffle(orig, 16, sh, 4) == QUADR_OK);

    /* byte-layer 0: bytes[0] of each word = AA 11 FF 00 */
    CHECK(sh[0]  == 0xAA);
    CHECK(sh[1]  == 0x11);
    CHECK(sh[2]  == 0xFF);
    CHECK(sh[3]  == 0x00);
    /* byte-layer 1: bytes[1] of each word = BB 22 EE 11 */
    CHECK(sh[4]  == 0xBB);
    CHECK(sh[5]  == 0x22);

    CHECK(quadr_byte_unshuffle(sh, 16, unsh, 4) == QUADR_OK);
    CHECK(memcmp(orig, unsh, 16) == 0);
}

static void test_byte_shuffle_entropy(void) {
    SECTION("Byte Shuffle reduces entropy on float32-like data");

    size_t   len  = 4096;          /* must be multiple of 4 */
    uint8_t *orig = malloc(len);

    /* Simulate float32: bytes 2-3 (exponent area) mostly constant,
       bytes 0-1 (mantissa low) vary more                          */
    for (size_t i = 0; i < len/4; i++) {
        orig[i*4+0] = (uint8_t)(i & 0xFF);          /* mantissa lo: all values */
        orig[i*4+1] = (uint8_t)((i>>8) & 0x0F);     /* mantissa hi: 0-15 */
        orig[i*4+2] = 0x80;                          /* exponent: constant */
        orig[i*4+3] = 0x3F;                          /* sign+exp: constant */
    }

    uint8_t *sh   = malloc(len);
    uint8_t *sh_d = malloc(len);

    quadr_byte_shuffle(orig, len, sh, 4);
    quadr_delta_encode(sh, len, sh_d, 1, 8);

    double h_orig = quadr_entropy(orig, len);
    double h_sh   = quadr_entropy(sh,   len);
    double h_sh_d = quadr_entropy(sh_d, len);

    printf("     entropy: orig=%.4f  shuffled=%.4f  shuffle+delta=%.4f\n",
           h_orig, h_sh, h_sh_d);

    /* Note: byte-shuffle alone does NOT change entropy (same byte frequencies,
     * different order).  Only shuffle+delta reduces entropy.                  */
    CHECK(fabs(h_sh - h_orig) < 0.01);  /* shuffle alone: entropy unchanged  */
    CHECK(h_sh_d < h_orig);             /* shuffle+delta beats raw            */
    CHECK(h_sh_d < h_sh);               /* delta on top of shuffle helps      */

    free(orig); free(sh); free(sh_d);
}

static void test_rle(void) {
    SECTION("RLE encode/decode");

    uint8_t orig[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x01};
    uint8_t enc[64], dec[16];
    size_t  enc_len = sizeof(enc);

    CHECK(quadr_rle_encode(orig, sizeof(orig), enc, &enc_len) == QUADR_OK);
    CHECK(enc_len == 6);   /* [4,FF] [1,00] [2,01] = 6 bytes */
    CHECK(enc[0] == 4 && enc[1] == 0xFF);
    CHECK(enc[2] == 1 && enc[3] == 0x00);
    CHECK(enc[4] == 2 && enc[5] == 0x01);

    CHECK(quadr_rle_decode(enc, enc_len, dec, sizeof(orig)) == QUADR_OK);
    CHECK(memcmp(orig, dec, sizeof(orig)) == 0);
}

static void test_rle_max_run(void) {
    SECTION("RLE max run length (255)");

    uint8_t orig[512];
    memset(orig, 0xAB, 512);

    uint8_t enc[64];
    size_t  enc_len = sizeof(enc);
    CHECK(quadr_rle_encode(orig, 512, enc, &enc_len) == QUADR_OK);
    /* 512 = 255 + 255 + 2  →  3 runs  →  6 bytes */
    CHECK(enc_len == 6);

    uint8_t dec[512];
    CHECK(quadr_rle_decode(enc, enc_len, dec, 512) == QUADR_OK);
    CHECK(memcmp(orig, dec, 512) == 0);
}

static void test_rle_ratio(void) {
    SECTION("RLE ratio estimator");

    uint8_t zeros[256]; memset(zeros, 0, 256);
    uint8_t *rand = make_random(256, 42);

    double r_zeros = quadr_rle_ratio(zeros, 256);
    double r_rand  = quadr_rle_ratio(rand,  256);

    printf("     rle_ratio zeros=%.4f  random=%.4f\n", r_zeros, r_rand);
    CHECK(r_zeros < 0.05);    /* 1 run of 256: 2 bytes / 256 = 0.0078 */
    CHECK(r_rand  > 0.8);     /* ~256 runs, ~512 bytes vs 256 */

    free(rand);
}

static void test_probe_delta(void) {
    SECTION("Probe: smooth data → DELTA");

    size_t       len  = 4096;
    uint8_t     *data = make_sine(len, 5.0);
    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);

    QuadrProbeResult r = quadr_probe(data, len, &opts);
    printf("     type=%d stride=%d shuffle=%d score=%.4f\n",
           r.type, r.stride, r.shuffle, r.score);
    CHECK(r.type == QUADR_BLOCK_DELTA);

    free(data);
}

static void test_probe_passthrough(void) {
    SECTION("Probe: random data → PASSTHROUGH");

    size_t       len  = 4096;
    uint8_t     *data = make_random(len, 99);
    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);

    QuadrProbeResult r = quadr_probe(data, len, &opts);
    printf("     type=%d score=%.4f\n", r.type, r.score);
    CHECK(r.type == QUADR_BLOCK_PASSTHROUGH);

    free(data);
}

static void test_probe_rle(void) {
    SECTION("Probe: zero-heavy data → RLE");

    size_t  len  = 4096;
    uint8_t *data = make_zeros(len);
    /* Sprinkle a few non-zeros */
    data[100] = 5; data[200] = 10;

    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);

    QuadrProbeResult r = quadr_probe(data, len, &opts);
    printf("     type=%d score=%.4f\n", r.type, r.score);
    CHECK(r.type == QUADR_BLOCK_RLE);

    free(data);
}

static void test_probe_float_shuffle(void) {
    SECTION("Probe: float32 data → DELTA (shuffle path wins for structured floats)");

    /*
     * Use float32 data where byte-layer structure is clear:
     * all values in [1.0, 2.0) share the same exponent byte (0x3F),
     * so after shuffle the exponent layer is a constant run → delta→zeros.
     * Compare pure-delta entropy vs shuffle+delta entropy directly.
     */
    size_t  n    = 2048;          /* 2048 float32 = 8192 bytes */
    size_t  len  = n * 4;
    uint8_t *data = malloc(len);

    /* Linear ramp [1.0, 2.0) in float32 */
    for (size_t i = 0; i < n; i++) {
        float f = 1.0f + (float)i / (float)n;
        memcpy(data + i*4, &f, 4);
    }

    /* Manually compute shuffle+delta entropy to verify it's better than delta alone */
    uint8_t *sh_buf  = malloc(len);
    uint8_t *sh_d    = malloc(len);
    uint8_t *delta1  = malloc(len);
    quadr_byte_shuffle(data, len, sh_buf, 4);
    quadr_delta_encode(sh_buf, len, sh_d,   1, 8);
    quadr_delta_encode(data,   len, delta1, 1, 8);  /* x_bit=8 byte-level */

    double h_sh_d   = quadr_entropy(sh_d,   len);
    double h_delta1 = quadr_entropy(delta1, len);
    printf("     direct byte-delta h=%.4f  shuffle+delta h=%.4f\n",
           h_delta1, h_sh_d);
    CHECK(h_sh_d < h_delta1);   /* shuffle+delta must be strictly better */

    /* Probe should choose DELTA (shuffle or not—both are valid wins) */
    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);
    opts.x_bit     = 32;
    opts.data_hint = QUADR_HINT_FLOAT;

    QuadrProbeResult r = quadr_probe(data, len, &opts);
    printf("     probe → type=%d stride=%d shuffle=%d score=%.4f\n",
           r.type, r.stride, r.shuffle, r.score);
    CHECK(r.type == QUADR_BLOCK_DELTA);
    /* score must be <= shuffle+delta entropy (probe found at least that good) */
    CHECK(r.score <= h_sh_d + 0.01);

    free(delta1); free(sh_buf); free(sh_d); free(data);
}

static void test_block_encode_decode_delta(void) {
    SECTION("Block encode/decode: DELTA path A roundtrip");

    size_t   len  = 16 * 1024;
    /* High frequency sine → adjacent bytes differ → RLE won't trigger,
       but the periodicity gives delta significant entropy benefit.        */
    uint8_t *orig = make_sine(len, 80.0);
    uint8_t *enc  = malloc(len * 2);
    uint8_t *dec  = malloc(len);
    size_t   enc_len = len * 2;

    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);
    QuadrProbeResult result;

    CHECK(quadr_block_encode(orig, len, enc, &enc_len, &opts, &result) == QUADR_OK);
    CHECK(result.type == QUADR_BLOCK_DELTA);
    printf("     stride=%d  encoded %zu → %zu bytes\n",
           result.stride, len, enc_len);

    QuadrBlockHeader hdr = {
        .uncomp_size  = (uint32_t)len,
        .comp_size    = (uint32_t)enc_len,
        .type         = result.type,
        .shuffle_flag = result.shuffle,
        .x_bit        = opts.x_bit,
        .stride       = result.stride,
        .word_size    = result.word_size,
    };
    CHECK(quadr_block_decode(enc, enc_len, dec, len, &hdr) == QUADR_OK);
    CHECK(memcmp(orig, dec, len) == 0);

    free(orig); free(enc); free(dec);
}

static void test_block_encode_decode_rle(void) {
    SECTION("Block encode/decode: RLE roundtrip");

    uint8_t orig[1024];
    memset(orig, 0x42, 512);
    memset(orig+512, 0x99, 512);
    uint8_t enc[1024], dec[1024];
    size_t  enc_len = sizeof(enc);

    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);
    QuadrProbeResult result;

    CHECK(quadr_block_encode(orig, 1024, enc, &enc_len, &opts, &result) == QUADR_OK);
    CHECK(result.type == QUADR_BLOCK_RLE);
    printf("     RLE: 1024 → %zu bytes\n", enc_len);
    CHECK(enc_len < 100);  /* should compress dramatically */

    QuadrBlockHeader hdr = {
        .uncomp_size  = 1024,
        .comp_size    = (uint32_t)enc_len,
        .type         = QUADR_BLOCK_RLE,
    };
    CHECK(quadr_block_decode(enc, enc_len, dec, 1024, &hdr) == QUADR_OK);
    CHECK(memcmp(orig, dec, 1024) == 0);
}

static void test_block_header_serialization(void) {
    SECTION("Block Header serialize/deserialize");

    QuadrBlockHeader orig = {
        .uncomp_size  = 65536,
        .comp_size    = 32000,
        .type         = QUADR_BLOCK_DELTA,
        .shuffle_flag = 1,
        .x_bit        = 32,
        .stride       = 4,
        .word_size    = 4,
    };
    uint8_t buf[16] = {0};  /* 12 bytes needed */
    QuadrBlockHeader parsed;

    CHECK(quadr_block_header_write(&orig,   buf) == QUADR_OK);
    CHECK(quadr_block_header_read (buf, &parsed) == QUADR_OK);

    CHECK(parsed.uncomp_size  == orig.uncomp_size);
    CHECK(parsed.comp_size    == orig.comp_size);
    CHECK(parsed.type         == orig.type);
    CHECK(parsed.shuffle_flag == orig.shuffle_flag);
    CHECK(parsed.x_bit        == orig.x_bit);
    CHECK(parsed.stride       == orig.stride);
    CHECK(parsed.word_size    == orig.word_size);
}

static void test_file_header_serialization(void) {
    SECTION("File Header serialize/deserialize");

    uint64_t hashes[3]  = {0xDEADBEEFCAFEBABEULL, 0x0102030405060708ULL, 0xFFFFFFFFFFFFFFFFULL};
    uint64_t offsets[3] = {512, 1024, 2048};

    QuadrFileHeader orig = {
        .magic             = QUADR_MAGIC,
        .version           = QUADR_VERSION,
        .total_uncomp_size = 196608,
        .block_count       = 3,
        .data_hint         = QUADR_HINT_FLOAT,
        .hash_table        = hashes,
        .offset_table      = offsets,
    };

    size_t   buf_len = quadr_file_header_size(3);
    uint8_t *buf     = malloc(buf_len);
    CHECK(quadr_file_header_write(&orig, buf, buf_len) == QUADR_OK);

    QuadrFileHeader parsed = {0};
    CHECK(quadr_file_header_read(buf, buf_len, &parsed) == QUADR_OK);
    CHECK(parsed.magic             == QUADR_MAGIC);
    CHECK(parsed.version           == QUADR_VERSION);
    CHECK(parsed.total_uncomp_size == orig.total_uncomp_size);
    CHECK(parsed.block_count       == 3);
    CHECK(parsed.data_hint         == QUADR_HINT_FLOAT);
    CHECK(parsed.hash_table[0]     == hashes[0]);
    CHECK(parsed.hash_table[2]     == hashes[2]);
    CHECK(parsed.offset_table[1]   == offsets[1]);

    quadr_file_header_free(&parsed);
    free(buf);
}

static void test_xxh3(void) {
    SECTION("XXH3-64 basic determinism and sensitivity");

    const char *s1 = "Quadr v1.5";
    const char *s2 = "Quadr v1.4";

    uint64_t h1 = quadr_xxh3_64(s1, strlen(s1));
    uint64_t h2 = quadr_xxh3_64(s1, strlen(s1));  /* same input */
    uint64_t h3 = quadr_xxh3_64(s2, strlen(s2));  /* different */
    uint64_t h0 = quadr_xxh3_64("",  0);

    printf("     h(\"%s\") = %016llx\n", s1, (unsigned long long)h1);
    printf("     h(\"%s\") = %016llx\n", s2, (unsigned long long)h3);
    printf("     h(\"\")   = %016llx\n",     (unsigned long long)h0);

    CHECK(h1 == h2);           /* deterministic */
    CHECK(h1 != h3);           /* sensitive to content */
    CHECK(h0 != h1);           /* empty != non-empty */

    /* Large buffer: same result on repeated calls */
    uint8_t *big = make_random(128 * 1024, 42);
    uint64_t hA  = quadr_xxh3_64(big, 128 * 1024);
    uint64_t hB  = quadr_xxh3_64(big, 128 * 1024);
    CHECK(hA == hB);
    free(big);
}

static void test_entropy(void) {
    SECTION("Entropy estimator");

    uint8_t uniform[256];
    for (int i = 0; i < 256; i++) uniform[i] = (uint8_t)i;
    double h_uniform = quadr_entropy(uniform, 256);

    uint8_t constant[256];
    memset(constant, 0x42, 256);
    double h_constant = quadr_entropy(constant, 256);

    printf("     uniform=%.4f  constant=%.4f\n", h_uniform, h_constant);
    CHECK(fabs(h_uniform - 8.0) < 0.001);   /* should be 8.0 */
    CHECK(h_constant < 0.001);              /* should be ~0 */
}

/* ─── main ─────────────────────────────────────────────────────────────── */

/* ══════════════════════════════════════════════════════════════════════════
 * Streaming API tests  (added: Route 1 engineering hardening)
 * ══════════════════════════════════════════════════════════════════════════ */

#include "quadr.h"   /* already included above via the first include */
#include <stdio.h>   /* already included, but safe to repeat */

/* ── portable temp file helper ─────────────────────────────────────────── */

static const char *tmp_path(const char *name) {
    /* Use a static buffer — fine for single-threaded tests */
    static char buf[512];
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\Temp";
    snprintf(buf, sizeof(buf), "%s\\quadr_test_%s", tmp, name);
#else
    snprintf(buf, sizeof(buf), "/tmp/quadr_test_%s", name);
#endif
    return buf;
}

/* Remove a temp file, ignore errors */
static void rm_tmp(const char *p) {
#ifdef _WIN32
    _unlink(p);
#else
    unlink(p);
#endif
}

/* ── stream encode → decode roundtrip helper ───────────────────────────── */

static int stream_roundtrip(const char *label,
                            const uint8_t *data, size_t data_len,
                            const QuadrEncodeOpts *opts) {
    const char *enc_path = tmp_path("enc.qdr");
    const char *dec_path = tmp_path("dec.bin");

    /* Encode */
    QuadrStreamCtx *enc = quadr_stream_encode_open(enc_path, opts, 0, 0, 0);
    if (!enc) {
        fprintf(stderr, "  [%s] encode open failed\n", label);
        return 0;
    }
    QuadrError e = quadr_stream_feed(enc, data, data_len);
    if (e != QUADR_OK) {
        fprintf(stderr, "  [%s] feed error: %s\n", label, quadr_strerror(e));
        quadr_stream_encode_close(enc);
        rm_tmp(enc_path);
        return 0;
    }
    e = quadr_stream_encode_close(enc);
    if (e != QUADR_OK) {
        fprintf(stderr, "  [%s] encode close error: %s\n", label, quadr_strerror(e));
        rm_tmp(enc_path);
        return 0;
    }

    /* Decode */
    QuadrStreamCtx *dec = quadr_stream_decode_open(enc_path, 0);
    if (!dec) {
        fprintf(stderr, "  [%s] decode open failed\n", label);
        rm_tmp(enc_path);
        return 0;
    }

    uint8_t *out  = malloc(data_len + 1);
    size_t   total = 0;
    if (out) {
        for (;;) {
            size_t cap = data_len + 1 - total;
            if (cap == 0) break;   /* buffer full — shouldn't happen */
            size_t got = 0;
            QuadrError pe = quadr_stream_pull(dec, out + total, cap, &got);
            total += got;
            if (pe == QUADR_ERR_TRUNC) break;
            if (pe != QUADR_OK) {
                fprintf(stderr, "  [%s] pull error: %s\n", label, quadr_strerror(pe));
                break;
            }
        }
    }
    quadr_stream_close(dec);
    rm_tmp(enc_path);
    rm_tmp(dec_path);

    int ok = out && (total == data_len) && (memcmp(data, out, data_len) == 0);
    if (!ok)
        fprintf(stderr, "  [%s] mismatch: expected %zu bytes, got %zu\n",
                label, data_len, total);
    free(out);
    return ok;
}

/* ── Edge case: empty file ─────────────────────────────────────────────── */
static void test_stream_empty(void) {
    SECTION("Stream: empty input");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);

    const char *p = tmp_path("empty.qdr");
    QuadrStreamCtx *enc = quadr_stream_encode_open(p, &opts, 0, 0, 0);
    CHECK(enc != NULL);
    if (enc) {
        /* Feed nothing — just close immediately */
        CHECK(quadr_stream_encode_close(enc) == QUADR_OK);

        QuadrStreamCtx *dec = quadr_stream_decode_open(p, 0);
        CHECK(dec != NULL);
        if (dec) {
            uint8_t buf[4];
            size_t got = 0;
            QuadrError e = quadr_stream_pull(dec, buf, sizeof(buf), &got);
            /* Empty stream: first pull should signal EOF with 0 bytes */
            CHECK(e == QUADR_ERR_TRUNC);
            CHECK(got == 0);
            quadr_stream_close(dec);
        }
    }
    rm_tmp(p);
}

/* ── Edge case: single byte ────────────────────────────────────────────── */
static void test_stream_single_byte(void) {
    SECTION("Stream: single byte");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    uint8_t one = 0xAB;
    CHECK(stream_roundtrip("1byte", &one, 1, &opts));
}

/* ── Edge case: exactly one block ──────────────────────────────────────── */
static void test_stream_exact_block(void) {
    SECTION("Stream: exactly block_size bytes");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    uint32_t bs = opts.block_size;  /* 64 KB */
    uint8_t *data = malloc(bs);
    CHECK(data != NULL);
    if (data) {
        /* Fill with a sine pattern (DELTA path) */
        for (size_t i = 0; i < bs; i++)
            data[i] = (uint8_t)(128 + 100 * sin(2.0 * 3.14159 * (double)i / 256.0));
        CHECK(stream_roundtrip("exact_block", data, bs, &opts));
        free(data);
    }
}

/* ── Edge case: exactly two blocks ────────────────────────────────────────*/
static void test_stream_two_blocks(void) {
    SECTION("Stream: exactly 2 × block_size bytes");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    size_t len = (size_t)opts.block_size * 2;
    uint8_t *data = malloc(len);
    CHECK(data != NULL);
    if (data) {
        for (size_t i = 0; i < len; i++)
            data[i] = (uint8_t)(i & 0xFF);
        CHECK(stream_roundtrip("two_blocks", data, len, &opts));
        free(data);
    }
}

/* ── Edge case: block_size + 1 (straddles a block boundary) ────────────── */
static void test_stream_straddle(void) {
    SECTION("Stream: block_size + 1 bytes (straddles boundary)");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    size_t len = (size_t)opts.block_size + 1;
    uint8_t *data = malloc(len);
    CHECK(data != NULL);
    if (data) {
        for (size_t i = 0; i < len; i++)
            data[i] = (uint8_t)(255 - (i & 0xFF));
        CHECK(stream_roundtrip("straddle", data, len, &opts));
        free(data);
    }
}

/* ── Multi-block with multiple feed() calls ────────────────────────────── */
static void test_stream_incremental_feed(void) {
    SECTION("Stream: incremental feed (1 byte at a time for first 256 bytes)");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    /* Build a 512-byte payload */
    size_t len = 512;
    uint8_t data[512];
    for (size_t i = 0; i < len; i++) data[i] = (uint8_t)(i ^ 0x55);

    const char *p = tmp_path("incr.qdr");
    QuadrStreamCtx *enc = quadr_stream_encode_open(p, &opts, 0, 0, 0);
    CHECK(enc != NULL);
    if (enc) {
        /* Feed 1 byte at a time for the first 256 */
        QuadrError e = QUADR_OK;
        for (size_t i = 0; i < 256 && e == QUADR_OK; i++)
            e = quadr_stream_feed(enc, data + i, 1);
        /* Then feed the rest in one shot */
        if (e == QUADR_OK)
            e = quadr_stream_feed(enc, data + 256, 256);
        CHECK(e == QUADR_OK);
        CHECK(quadr_stream_encode_close(enc) == QUADR_OK);

        /* Decode and verify */
        QuadrStreamCtx *dec = quadr_stream_decode_open(p, 0);
        CHECK(dec != NULL);
        if (dec) {
            uint8_t out[512]; size_t total = 0;
            for (;;) {
                size_t cap = sizeof(out) - total;
                if (cap == 0) break;
                size_t got = 0;
                QuadrError pe = quadr_stream_pull(dec, out+total, cap, &got);
                total += got;
                if (pe == QUADR_ERR_TRUNC) break;
                if (pe != QUADR_OK) break;
            }
            quadr_stream_close(dec);
            CHECK(total == len);
            CHECK(memcmp(data, out, len) == 0);
        }
    }
    rm_tmp(p);
}

/* ── RLE path through streaming ────────────────────────────────────────── */
static void test_stream_rle_path(void) {
    SECTION("Stream: RLE code path (zero-filled buffer)");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    size_t len = opts.block_size * 3 / 2;   /* 1.5 blocks */
    uint8_t *data = calloc(len, 1);
    CHECK(data != NULL);
    if (data) {
        /* Sprinkle a few non-zero values so the data is still interesting */
        data[100] = 0xAB; data[50000] = 0xFF;
        CHECK(stream_roundtrip("rle", data, len, &opts));
        free(data);
    }
}

/* ── PASSTHROUGH path through streaming ────────────────────────────────── */
static void test_stream_passthrough_path(void) {
    SECTION("Stream: PASSTHROUGH code path (pseudo-random)");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    size_t len = opts.block_size + 7777;  /* intentionally odd size */
    uint8_t *data = malloc(len);
    CHECK(data != NULL);
    if (data) {
        /* LCG pseudo-random — high entropy, PASSTHROUGH path */
        unsigned s = 0xDEADBEEFu;
        for (size_t i = 0; i < len; i++) {
            s = s * 1664525u + 1013904223u;
            data[i] = (uint8_t)(s >> 16);
        }
        CHECK(stream_roundtrip("passthrough", data, len, &opts));
        free(data);
    }
}

/* ── DELTA path: multi-channel PCM through streaming ────────────────────── */
static void test_stream_delta_pcm(void) {
    SECTION("Stream: DELTA path (stereo PCM sine wave)");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    opts.x_bit        = 8;
    opts.hint_stride  = 2;

    size_t len = opts.block_size * 3;   /* 3 blocks */
    uint8_t *data = malloc(len);
    CHECK(data != NULL);
    if (data) {
        for (size_t i = 0; i < len/2; i++) {
            data[i*2+0] = (uint8_t)(128 + 100 * sin(2.0*3.14159*440.0*(double)i/44100.0));
            data[i*2+1] = (uint8_t)(128 + 80  * sin(2.0*3.14159*880.0*(double)i/44100.0));
        }
        CHECK(stream_roundtrip("delta_pcm", data, len, &opts));
        free(data);
    }
}

/* ── stats counters ─────────────────────────────────────────────────────── */
static void test_stream_stats(void) {
    SECTION("Stream: bytes_in / bytes_out counters");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    const char *p = tmp_path("stats.qdr");
    size_t data_len = 4096;
    uint8_t *data = calloc(data_len, 1);
    CHECK(data != NULL);
    if (!data) return;

    QuadrStreamCtx *enc = quadr_stream_encode_open(p, &opts, 0, 0, 0);
    CHECK(enc != NULL);
    if (enc) {
        quadr_stream_feed(enc, data, data_len);
        uint64_t bi = quadr_stream_bytes_in(enc);
        CHECK(bi == data_len);
        quadr_stream_encode_close(enc);
    }

    QuadrStreamCtx *dec = quadr_stream_decode_open(p, 0);
    CHECK(dec != NULL);
    if (dec) {
        uint8_t buf[8192]; size_t got_total = 0;
        for (;;) {
            size_t n = 0;
            if (got_total >= sizeof(buf)) break;
            QuadrError e = quadr_stream_pull(dec, buf + got_total,
                                             sizeof(buf) - got_total, &n);
            got_total += n;
            if (e == QUADR_ERR_TRUNC) break;
            if (e != QUADR_OK) break;
        }
        uint64_t bo = quadr_stream_bytes_out(dec);
        CHECK(bo == data_len);
        CHECK(got_total == data_len);
        quadr_stream_close(dec);
    }
    rm_tmp(p);
    free(data);
}

/* ── verify: hash corruption detection ─────────────────────────────────── */
static void test_stream_hash_corruption(void) {
    SECTION("Stream: hash mismatch detected on corrupt file");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    const char *p = tmp_path("corrupt.qdr");

    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;

    QuadrStreamCtx *enc = quadr_stream_encode_open(p, &opts, 0, 0, 0);
    CHECK(enc != NULL);
    if (enc) {
        quadr_stream_feed(enc, data, sizeof(data));
        quadr_stream_encode_close(enc);
    }

    /* Corrupt one byte in the payload area of the file */
    FILE *f = fopen(p, "r+b");
    if (f) {
        fseek(f, -10, SEEK_END);  /* near the end, inside payload */
        uint8_t bad = 0xFF;
        fwrite(&bad, 1, 1, f);
        fclose(f);
    }

    QuadrStreamCtx *dec = quadr_stream_decode_open(p, 0);
    if (dec) {
        uint8_t buf[512]; size_t got = 0;
        QuadrError e = quadr_stream_pull(dec, buf, sizeof(buf), &got);
        /* Either HASH_FAIL or a transform error — both non-OK are acceptable */
        int corrupt_detected = (e != QUADR_OK);
        CHECK(corrupt_detected);
        if (!corrupt_detected)
            fprintf(stderr, "  corruption not detected! e=%d got=%zu\n",
                    (int)e, got);
        quadr_stream_close(dec);
    }
    rm_tmp(p);
}

static void test_stream_verify_ok(void) {
    SECTION("Stream: verify passes on good file");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    const char *p = tmp_path("verify_ok.qdr");
    uint8_t data[4096];
    for (int i = 0; i < 4096; i++) data[i] = (uint8_t)(i & 0xFF);

    QuadrStreamCtx *enc = quadr_stream_encode_open(p, &opts, 0, 0, 0);
    if (enc) {
        quadr_stream_feed(enc, data, sizeof(data));
        quadr_stream_encode_close(enc);
    }
    uint32_t bad = 99;
    CHECK(quadr_stream_verify(p, &bad) == QUADR_OK);
    CHECK(bad == UINT32_MAX);   /* not set when all pass */
    rm_tmp(p);
}

static void test_stream_verify_corrupt(void) {
    SECTION("Stream: verify catches corruption");
    QuadrEncodeOpts opts; quadr_encode_opts_default(&opts);
    const char *p = tmp_path("verify_bad.qdr");
    uint8_t data[4096];
    for (int i = 0; i < 4096; i++) data[i] = (uint8_t)(i * 3);

    QuadrStreamCtx *enc = quadr_stream_encode_open(p, &opts, 0, 0, 0);
    if (enc) {
        quadr_stream_feed(enc, data, sizeof(data));
        quadr_stream_encode_close(enc);
    }

    /* Corrupt a byte in the payload section */
    FILE *f = fopen(p, "r+b");
    if (f) {
        fseek(f, -8, SEEK_END);
        uint8_t bad_byte = 0xDE;
        fwrite(&bad_byte, 1, 1, f);
        fclose(f);
    }

    uint32_t bad = UINT32_MAX;
    QuadrError e = quadr_stream_verify(p, &bad);
    CHECK(e != QUADR_OK);   /* must detect the corruption */
    rm_tmp(p);
}


int main(void) {
    printf("Quadr v1.5 Unit Tests\n");
    printf("═══════════════════════════════════════\n");

    test_entropy();
    test_xxh3();
    test_rle_ratio();
    test_delta_8bit();
    test_delta_stride();
    test_delta_16bit();
    test_delta_wraparound();
    test_delta_large();
    test_byte_shuffle();
    test_byte_shuffle_entropy();
    test_rle();
    test_rle_max_run();
    test_probe_passthrough();
    test_probe_rle();
    test_probe_delta();
    test_probe_float_shuffle();
    test_block_encode_decode_delta();
    test_block_encode_decode_rle();
    test_block_header_serialization();
    test_file_header_serialization();

    /* ── Streaming API ── */
    test_stream_empty();
    test_stream_single_byte();
    test_stream_exact_block();
    test_stream_two_blocks();
    test_stream_straddle();
    test_stream_incremental_feed();
    test_stream_rle_path();
    test_stream_passthrough_path();
    test_stream_delta_pcm();
    test_stream_stats();
    test_stream_hash_corruption();
    test_stream_verify_ok();
    test_stream_verify_corrupt();

    printf("\n═══════════════════════════════════════\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

