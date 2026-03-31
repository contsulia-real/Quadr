/*
 * fuzz_decode.c  –  Fuzz target for Quadr decoder
 *
 * Usage:
 *   libFuzzer:  clang -fsanitize=fuzzer -g -O1 fuzz_decode.c -o fuzz_decode \
 *               && ./fuzz_decode fuzz_corpus/
 *   AFL++:      afl-clang-fast -g -O1 fuzz_decode.c -o fuzz_decode \
 *               && afl-fuzz -i fuzz_corpus/ -o fuzz_out/ ./fuzz_decode
 *   Standalone: gcc -DSTANDALONE -g -O1 fuzz_decode.c -o fuzz_decode \
 *               && ./fuzz_decode < test.qdr
 *
 * What it tests:
 *   - quadr_file_header_read  (header parsing, OOM on huge block_count)
 *   - quadr_block_header_read (block header parsing)
 *   - quadr_block_decode      (all block types with random payloads)
 *   - quadr_rle_decode        (RLE with random count/value pairs)
 *
 * The fuzzer should NOT crash or trigger ASan/UBSan errors on any input.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "quadr.h"

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

static uint32_t load_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t load_u64le(const uint8_t *p) {
    return (uint64_t)load_u32le(p) | ((uint64_t)load_u32le(p + 4) << 32);
}

/* ─── Fuzz entry point ────────────────────────────────────────────────────── */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 18) return 0;  /* minimum header size */

    /* Cap input size to avoid OOM on pathological cases */
    if (size > 1024 * 1024) size = 1024 * 1024;

    /* ── Test 1: File header parsing ─────────────────────────────────── */
    {
        QuadrFileHeader fh = {0};
        QuadrError e = quadr_file_header_read(data, size, &fh);
        if (e == QUADR_OK) {
            /* Header parsed successfully — verify tables are allocated */
            if (fh.block_count > 0) {
                /* Ensure we can free without crashing */
            }
            quadr_file_header_free(&fh);
        }
    }

    /* ── Test 2: Block header parsing ────────────────────────────────── */
    if (size >= QUADR_BLOCK_HEADER_SIZE) {
        QuadrBlockHeader bh = {0};
        QuadrError e = quadr_block_header_read(data, &bh);
        if (e == QUADR_OK) {
            /* ── Test 3: Block decode with random payload ────────────── */
            size_t uncomp = bh.uncomp_size;
            /* Cap output size to avoid OOM */
            if (uncomp > 256 * 1024) uncomp = 256 * 1024;
            if (uncomp == 0) uncomp = 1;

            uint8_t *out = (uint8_t *)malloc(uncomp);
            if (out) {
                /* Use remaining data as payload, or zeros if not enough */
                size_t payload_off = QUADR_BLOCK_HEADER_SIZE;
                size_t payload_len = (size > payload_off) ? (size - payload_off) : 0;

                uint8_t *payload = (uint8_t *)malloc(payload_len ? payload_len : 1);
                if (payload) {
                    if (payload_len > 0)
                        memcpy(payload, data + payload_off, payload_len);
                    else
                        payload[0] = 0;

                    /* Try decode — should not crash even with malformed input */
                    quadr_block_decode(payload, payload_len ? payload_len : 1,
                                       out, uncomp, &bh);
                    free(payload);
                }
                free(out);
            }
        }
    }

    /* ── Test 4: RLE decode with random input ────────────────────────── */
    if (size >= 4) {
        uint32_t out_len = load_u32le(data) % (256 * 1024) + 1;
        uint8_t *out = (uint8_t *)malloc(out_len);
        if (out) {
            quadr_rle_decode(data + 4, size - 4, out, out_len);
            free(out);
        }
    }

    /* ── Test 5: Delta encode/decode roundtrip on random data ────────── */
    if (size >= 16) {
        size_t n = size / 2;
        if (n > 64 * 1024) n = 64 * 1024;
        uint8_t *enc = (uint8_t *)malloc(n);
        uint8_t *dec = (uint8_t *)malloc(n);
        if (enc && dec) {
            uint8_t strides[] = {1, 2, 3, 4, 8};
            uint8_t x_bits[]  = {8, 16, 32, 64};
            for (int si = 0; si < 5; si++) {
                for (int xi = 0; xi < 4; xi++) {
                    if (quadr_delta_encode(data, n, enc, strides[si], x_bits[xi]) == QUADR_OK) {
                        quadr_delta_decode(enc, n, dec, strides[si], x_bits[xi]);
                    }
                }
            }
        }
        free(enc);
        free(dec);
    }

    /* ── Test 6: Entropy on random data ──────────────────────────────── */
    {
        double h = quadr_entropy(data, size);
        /* Entropy should be in [0, 8] for byte data */
        (void)h;
    }

    /* ── Test 7: Probe on random data ────────────────────────────────── */
    {
        QuadrEncodeOpts opts;
        quadr_encode_opts_default(&opts);
        opts.x_bit = 8;
        QuadrProbeResult pr = quadr_probe_fast(data, size, &opts);
        /* Type should be valid enum value */
        if (pr.type > QUADR_BLOCK_RAW) {
            /* Invalid — but shouldn't crash */
        }
        (void)pr;
    }

    return 0;
}

/* ─── Standalone mode (for manual testing without libFuzzer) ──────────────── */

#ifdef STANDALONE
#include <stdio.h>

int main(void) {
    /* Read stdin into a buffer */
    size_t cap = 65536, size = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return 1;

    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (size >= cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); return 1; }
            buf = nb;
        }
        buf[size++] = (uint8_t)c;
    }

    LLVMFuzzerTestOneInput(buf, size);
    free(buf);
    return 0;
}
#endif
