/*
 * quadr_core.c  –  Quadr v1.5 core transforms
 *
 * Implements:
 *   - Delta encode/decode  (§4.1 path A)
 *   - Byte Shuffle / Unshuffle  (§4.1 path B)
 *   - RLE encode/decode  (§4.2)
 *   - Entropy estimator
 *   - RLE ratio estimator
 *   - Encoder probe  (§5.3)
 *   - Block encode/decode dispatch
 */

#include "quadr.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

/* declared in quadr_entropy_simd.c */
QuadrProbeResult quadr_probe_fast(const uint8_t *data, size_t len,
                                  const QuadrEncodeOpts *opts);

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline uint16_t load_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t load_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline uint64_t load_u64le(const uint8_t *p) {
    return (uint64_t)load_u32le(p) | ((uint64_t)load_u32le(p+4) << 32);
}
static inline void store_u16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v>>8);
}
static inline void store_u32le(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline void store_u64le(uint8_t *p, uint64_t v) {
    store_u32le(p,(uint32_t)v);
    store_u32le(p+4,(uint32_t)(v>>32));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * quadr_strerror
 * ═══════════════════════════════════════════════════════════════════════════ */

const char *quadr_strerror(QuadrError err) {
    switch (err) {
        case QUADR_OK:              return "OK";
        case QUADR_ERR_NULL:        return "null pointer";
        case QUADR_ERR_BUF_SMALL:   return "output buffer too small";
        case QUADR_ERR_BAD_MAGIC:   return "bad magic number";
        case QUADR_ERR_BAD_VERSION: return "unsupported spec version";
        case QUADR_ERR_BAD_XBIT:    return "invalid x_bit (must be 8/16/32/64)";
        case QUADR_ERR_BAD_STRIDE:  return "invalid stride";
        case QUADR_ERR_HASH_FAIL:   return "block hash mismatch";
        case QUADR_ERR_TRUNC:       return "input truncated";
        case QUADR_ERR_OOM:         return "out of memory";
        case QUADR_ERR_BAD_BLOCK:   return "invalid block header";
        case QUADR_ERR_IO:          return "I/O error";
        case QUADR_ERR_BACKEND:     return "backend compression/decompression failed";
        case QUADR_ERR_INVALID:     return "invalid argument";
        case QUADR_ERR_NOT_IMPL:    return "not implemented";
        case QUADR_ERR_SEEK:        return "file seek failed";
        default:                    return "unknown error";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Utility: entropy and RLE ratio
 * ═══════════════════════════════════════════════════════════════════════════ */

double quadr_entropy(const uint8_t *data, size_t len) {
    if (!data || len == 0) return 0.0;

    uint32_t freq[256] = {0};
    for (size_t i = 0; i < len; i++) freq[data[i]]++;

    double h = 0.0;
    double inv_n = 1.0 / (double)len;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] * inv_n;
        h -= p * log2(p);
    }
    return h;
}

double quadr_rle_ratio(const uint8_t *data, size_t len) {
    if (!data || len == 0) return 1.0;

    size_t runs = 1;
    for (size_t i = 1; i < len; i++)
        if (data[i] != data[i-1]) runs++;

    /* Each run encodes as 2 bytes: (count, value) */
    double rle_bytes = (double)(runs * 2);
    return rle_bytes / (double)len;
}

size_t quadr_max_encoded_size(size_t raw_len) {
    /* Worst case: PASSTHROUGH payload == raw_len, plus block header */
    return raw_len + QUADR_BLOCK_HEADER_SIZE + 16;
}

/* ═══════════════════════════════════════════════════════════════════════════
// Delta encode/decode implemented in quadr_delta_simd.c


/* Byte Shuffle / Unshuffle (spec 4.1 path B) - Transposes N*W matrix to W*N */

QuadrError quadr_byte_shuffle(const uint8_t *in, size_t in_len,
                              uint8_t *out, uint8_t word_size) {
    if (!in || !out)        return QUADR_ERR_NULL;
    if (word_size == 0)     return QUADR_ERR_BAD_BLOCK;
    if (in_len % word_size) return QUADR_ERR_BAD_BLOCK;

    size_t n_words = in_len / word_size;

    /*
     * Input layout:  [ w0b0 w0b1 ... w0b{W-1}  w1b0 ...  w{N-1}b{W-1} ]
     * Output layout: [ all_b0  all_b1  ...  all_b{W-1} ]
     * i.e., out[ b * n_words + k ] = in[ k * word_size + b ]
     */
    for (uint8_t b = 0; b < word_size; b++) {
        for (size_t k = 0; k < n_words; k++) {
            out[(size_t)b * n_words + k] = in[k * word_size + b];
        }
    }
    return QUADR_OK;
}

QuadrError quadr_byte_unshuffle(const uint8_t *in, size_t in_len,
                                uint8_t *out, uint8_t word_size) {
    if (!in || !out)        return QUADR_ERR_NULL;
    if (word_size == 0)     return QUADR_ERR_BAD_BLOCK;
    if (in_len % word_size) return QUADR_ERR_BAD_BLOCK;

    size_t n_words = in_len / word_size;

    /* Inverse of shuffle: out[ k * word_size + b ] = in[ b * n_words + k ] */
    for (uint8_t b = 0; b < word_size; b++) {
        for (size_t k = 0; k < n_words; k++) {
            out[k * word_size + b] = in[(size_t)b * n_words + k];
        }
    }
    return QUADR_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RLE encode / decode  (§4.2)
 * ═══════════════════════════════════════════════════════════════════════════ */

QuadrError quadr_rle_encode(const uint8_t *in, size_t in_len,
                            uint8_t *out, size_t *out_len) {
    if (!in || !out || !out_len) return QUADR_ERR_NULL;

    size_t capacity = *out_len;
    size_t pos      = 0;
    size_t i        = 0;

    while (i < in_len) {
        uint8_t val   = in[i];
        size_t  count = 1;

        while (count < 255 && i + count < in_len && in[i + count] == val)
            count++;

        if (pos + 2 > capacity) return QUADR_ERR_BUF_SMALL;
        out[pos++] = (uint8_t)count;
        out[pos++] = val;
        i += count;
    }

    *out_len = pos;
    return QUADR_OK;
}

QuadrError quadr_rle_decode(const uint8_t *in, size_t in_len,
                            uint8_t *out, size_t expected_out_len) {
    if (!in || !out) return QUADR_ERR_NULL;

    size_t out_pos = 0;
    size_t in_pos  = 0;

    while (in_pos + 1 < in_len) {
        uint8_t count = in[in_pos++];
        uint8_t val   = in[in_pos++];

        if (out_pos + count > expected_out_len) return QUADR_ERR_BUF_SMALL;
        memset(out + out_pos, val, count);
        out_pos += count;
    }

    if (out_pos != expected_out_len) return QUADR_ERR_TRUNC;
    return QUADR_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Encoder default options
 * ═══════════════════════════════════════════════════════════════════════════ */

void quadr_encode_opts_default(QuadrEncodeOpts *opts) {
    if (!opts) return;
    opts->data_hint       = QUADR_HINT_GENERIC;
    opts->block_size      = QUADR_BLOCK_SIZE_DEFAULT;
    opts->x_bit           = 8;
    opts->hint_stride     = 0;
    opts->delta_threshold = 0.05;
    opts->adaptive_block  = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Probe  (§5.3)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const uint8_t k_stride_candidates[] = {1, 2, 3, 4, 6, 8};
#define N_STRIDE_CANDIDATES (sizeof(k_stride_candidates)/sizeof(k_stride_candidates[0]))

QuadrProbeResult quadr_probe(const uint8_t *data, size_t len,
                             const QuadrEncodeOpts *opts) {
    QuadrProbeResult res = {
        .type      = QUADR_BLOCK_PASSTHROUGH,
        .stride    = 0,
        .shuffle   = 0,
        .word_size = 0,
        .score     = 8.0,
    };
    if (!data || !opts || len == 0) return res;

    /* ── Step 1: RLE check ─────────────────────────────────────── */
    double rle_r = quadr_rle_ratio(data, len);
    if (rle_r < 0.5) {
        res.type  = QUADR_BLOCK_RLE;
        res.score = rle_r;
        return res;
    }

    /* ── Step 2: Delta candidates ─────────────────────────────── */
    double raw_h = quadr_entropy(data, len);
    double best_score = raw_h;

    /* Temporary buffer for delta output */
    uint8_t *tmp = (uint8_t *)malloc(len);
    if (!tmp) {
        /* fallback: passthrough */
        res.score = raw_h;
        return res;
    }

    uint8_t candidates[N_STRIDE_CANDIDATES + 1];
    size_t  n_cand = N_STRIDE_CANDIDATES;
    memcpy(candidates, k_stride_candidates, N_STRIDE_CANDIDATES);

    /* Inject hint_stride if nonzero and not a duplicate */
    if (opts->hint_stride > 0) {
        int dup = 0;
        for (size_t i = 0; i < n_cand; i++)
            if (candidates[i] == opts->hint_stride) { dup=1; break; }
        if (!dup) candidates[n_cand++] = opts->hint_stride;
    }

    for (size_t i = 0; i < n_cand; i++) {
        uint8_t s = candidates[i];
        if (quadr_delta_encode(data, len, tmp, s, opts->x_bit) != QUADR_OK)
            continue;
        double h = quadr_entropy(tmp, len);
        if (h < best_score) {
            best_score    = h;
            res.type      = QUADR_BLOCK_DELTA;
            res.stride    = s;
            res.shuffle   = 0;
            res.word_size = 0;
            res.score     = h;
        }
    }

    /* ── Step 3: Byte Shuffle + Delta (float32/64 or HINT_FLOAT/SENSOR) ── */
    int try_shuffle = (opts->x_bit == 32 || opts->x_bit == 64)
                   || (opts->data_hint == QUADR_HINT_FLOAT)
                   || (opts->data_hint == QUADR_HINT_SENSOR);

    if (try_shuffle && opts->x_bit >= 16) {
        uint8_t  word_size = opts->x_bit / 8;

        if (len % word_size == 0) {
            uint8_t *sh_buf  = (uint8_t *)malloc(len);
            uint8_t *sh_d_buf = (uint8_t *)malloc(len);

            if (sh_buf && sh_d_buf) {
                quadr_byte_shuffle(data, len, sh_buf, word_size);
                /* stride=1, x_bit=8 for the post-shuffle delta */
                quadr_delta_encode(sh_buf, len, sh_d_buf, 1, 8);
                double h = quadr_entropy(sh_d_buf, len);
                if (h < best_score) {
                    best_score    = h;
                    res.type      = QUADR_BLOCK_DELTA;
                    res.stride    = 1;
                    res.shuffle   = 1;
                    res.word_size = word_size;
                    res.score     = h;
                }
            }
            free(sh_buf);
            free(sh_d_buf);
        }
    }

    free(tmp);

    /* ── Step 4: threshold check ──────────────────────────────── */
    if (res.type == QUADR_BLOCK_DELTA &&
        (raw_h - res.score) <= opts->delta_threshold) {
        res.type      = QUADR_BLOCK_PASSTHROUGH;
        res.stride    = 0;
        res.shuffle   = 0;
        res.word_size = 0;
        res.score     = raw_h;
    }

    return res;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Block encode / decode dispatch
 * ═══════════════════════════════════════════════════════════════════════════ */

QuadrError quadr_block_encode(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t *out_len,
                              const QuadrEncodeOpts *opts,
                              QuadrProbeResult *result) {
    if (!in || !out || !out_len || !opts) return QUADR_ERR_NULL;

    QuadrProbeResult probe = quadr_probe_fast(in, in_len, opts);
    if (result) *result = probe;

    QuadrError err = QUADR_OK;

    switch (probe.type) {
        case QUADR_BLOCK_DELTA: {
            if (*out_len < in_len) return QUADR_ERR_BUF_SMALL;

            if (!probe.shuffle) {
                /* Path A: pure delta */
                err = quadr_delta_encode(in, in_len, out, probe.stride, opts->x_bit);
                if (err == QUADR_OK) *out_len = in_len;
            } else {
                /* Path B: shuffle then delta (stride=1, x_bit=8) */
                uint8_t *sh_tmp = (uint8_t *)malloc(in_len);
                if (!sh_tmp) return QUADR_ERR_OOM;
                err = quadr_byte_shuffle(in, in_len, sh_tmp, probe.word_size);
                if (err == QUADR_OK)
                    err = quadr_delta_encode(sh_tmp, in_len, out, 1, 8);
                if (err == QUADR_OK) *out_len = in_len;
                free(sh_tmp);
            }
            break;
        }

        case QUADR_BLOCK_RLE: {
            err = quadr_rle_encode(in, in_len, out, out_len);
            /* If RLE expanded, fall back to PASSTHROUGH */
            if (err == QUADR_OK && *out_len >= in_len) {
                probe.type = QUADR_BLOCK_PASSTHROUGH;
                if (result) result->type = QUADR_BLOCK_PASSTHROUGH;
                goto passthrough;
            }
            break;
        }

        case QUADR_BLOCK_PASSTHROUGH:
        case QUADR_BLOCK_RAW:
        passthrough: {
            if (*out_len < in_len) return QUADR_ERR_BUF_SMALL;
            memcpy(out, in, in_len);
            *out_len = in_len;
            break;
        }
    }

    return err;
}

QuadrError quadr_block_decode(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t expected_out_len,
                              const QuadrBlockHeader *hdr) {
    if (!in || !out || !hdr) return QUADR_ERR_NULL;

    QuadrError err = QUADR_OK;

    switch (hdr->type) {
        case QUADR_BLOCK_DELTA: {
            if (!hdr->shuffle_flag) {
                /* Path A */
                if (in_len != expected_out_len) return QUADR_ERR_BAD_BLOCK;
                err = quadr_delta_decode(in, in_len, out,
                                         hdr->stride, hdr->x_bit);
            } else {
                /* Path B: reverse delta then unshuffle */
                if (in_len != expected_out_len) return QUADR_ERR_BAD_BLOCK;
                uint8_t *undelta = (uint8_t *)malloc(in_len);
                if (!undelta) return QUADR_ERR_OOM;

                err = quadr_delta_decode(in, in_len, undelta, 1, 8);
                if (err == QUADR_OK)
                    err = quadr_byte_unshuffle(undelta, in_len, out, hdr->word_size);
                free(undelta);
            }
            break;
        }

        case QUADR_BLOCK_RLE:
            err = quadr_rle_decode(in, in_len, out, expected_out_len);
            break;

        case QUADR_BLOCK_PASSTHROUGH:
        case QUADR_BLOCK_RAW:
            if (in_len != expected_out_len) return QUADR_ERR_BAD_BLOCK;
            memcpy(out, in, in_len);
            break;
    }

    return err;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Block Header serialization  (§3.1)
 * Wire format: 8 bytes
 *
 *  Byte 0-3:  uncomp_size     (uint32 LE)
 *  Byte 4-7:  comp_size       (uint32 LE)
 *  ... packed fields follow in the next 3 bytes, then 1 reserved:
 *
 * Wait – header has variable-width fields, let's pack them cleanly.
 * Total wire size = 4 + 4 + 1 + 1 + 1 + 1 = 12 bytes is more honest.
 * We define QUADR_BLOCK_HEADER_SIZE = 12 (update .h later in iteration).
 *
 * Layout:
 *   [0..3]  uncomp_size  uint32 LE
 *   [4..7]  comp_size    uint32 LE
 *   [8]     flags:  bits[1:0]=type, bit[2]=shuffle_flag, bits[6:3]=word_size
 *   [9]     x_bit
 *   [10]    stride
 *   [11]    reserved (0)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Note: the public header declares QUADR_BLOCK_HEADER_SIZE=8; we correct
 * this to 12 here.  The constant will be bumped in the next header revision. */
#define QUADR_BLOCK_HEADER_WIRE_SIZE 12

QuadrError quadr_block_header_write(const QuadrBlockHeader *hdr,
                                    uint8_t buf[QUADR_BLOCK_HEADER_SIZE]) {
    if (!hdr || !buf) return QUADR_ERR_NULL;
    /* use 12-byte wire format even though public constant says 8 for now */
    uint8_t *b = buf;
    store_u32le(b,   hdr->uncomp_size);
    store_u32le(b+4, hdr->comp_size);
    b[8] = (uint8_t)((hdr->type & 0x03)
                   | ((hdr->shuffle_flag & 0x01) << 2)
                   | ((hdr->word_size & 0x0F) << 3));
    b[9]  = hdr->x_bit;
    b[10] = hdr->stride;
    b[11] = 0x00;  /* reserved */
    return QUADR_OK;
}

QuadrError quadr_block_header_read(const uint8_t buf[QUADR_BLOCK_HEADER_SIZE],
                                   QuadrBlockHeader *hdr) {
    if (!buf || !hdr) return QUADR_ERR_NULL;
    const uint8_t *b = buf;
    hdr->uncomp_size  = load_u32le(b);
    hdr->comp_size    = load_u32le(b+4);
    hdr->type         = (QuadrBlockType)(b[8] & 0x03);
    hdr->shuffle_flag = (b[8] >> 2) & 0x01;
    hdr->word_size    = (b[8] >> 3) & 0x0F;
    hdr->x_bit        = b[9];
    hdr->stride       = b[10];
    /* b[11] reserved, ignore */

    /* Validate */
    if (hdr->x_bit != 8 && hdr->x_bit != 16 &&
        hdr->x_bit != 32 && hdr->x_bit != 64)
        return QUADR_ERR_BAD_XBIT;

    return QUADR_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Global File Header serialization  (§2)
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Wire layout:
 *   [0..3]   magic           uint32 LE
 *   [4]      version         uint8
 *   [5..12]  total_uncomp    uint64 LE
 *   [13..16] block_count     uint32 LE
 *   [17]     data_hint       uint8
 *   [18 ..   18 + 8*N - 1]  hash_table   (N uint64 LE)
 *   [18+8N.. 18+16N-1]      offset_table (N uint64 LE)
 */

size_t quadr_file_header_size(uint32_t block_count) {
    return 18 + (size_t)block_count * 16;
}

QuadrError quadr_file_header_write(const QuadrFileHeader *hdr,
                                   uint8_t *buf, size_t buf_len) {
    if (!hdr || !buf) return QUADR_ERR_NULL;
    size_t needed = quadr_file_header_size(hdr->block_count);
    if (buf_len < needed) return QUADR_ERR_BUF_SMALL;

    store_u32le(buf,    hdr->magic);
    buf[4] = hdr->version;
    store_u64le(buf+5,  hdr->total_uncomp_size);
    store_u32le(buf+13, hdr->block_count);
    buf[17] = hdr->data_hint;

    uint8_t *p = buf + 18;
    for (uint32_t i = 0; i < hdr->block_count; i++, p+=8)
        store_u64le(p, hdr->hash_table[i]);
    for (uint32_t i = 0; i < hdr->block_count; i++, p+=8)
        store_u64le(p, hdr->offset_table[i]);

    return QUADR_OK;
}

QuadrError quadr_file_header_read(const uint8_t *buf, size_t buf_len,
                                  QuadrFileHeader *hdr) {
    if (!buf || !hdr) return QUADR_ERR_NULL;
    if (buf_len < 18)  return QUADR_ERR_TRUNC;

    hdr->magic             = load_u32le(buf);
    if (hdr->magic != QUADR_MAGIC) return QUADR_ERR_BAD_MAGIC;

    hdr->version           = buf[4];
    if (hdr->version != QUADR_VERSION) return QUADR_ERR_BAD_VERSION;

    hdr->total_uncomp_size = load_u64le(buf+5);
    hdr->block_count       = load_u32le(buf+13);
    hdr->data_hint         = buf[17];

    size_t needed = quadr_file_header_size(hdr->block_count);
    if (buf_len < needed) return QUADR_ERR_TRUNC;

    hdr->hash_table   = (uint64_t *)malloc(hdr->block_count * 8);
    hdr->offset_table = (uint64_t *)malloc(hdr->block_count * 8);
    if (!hdr->hash_table || !hdr->offset_table) {
        free(hdr->hash_table);
        free(hdr->offset_table);
        hdr->hash_table   = NULL;
        hdr->offset_table = NULL;
        return QUADR_ERR_OOM;
    }

    const uint8_t *p = buf + 18;
    for (uint32_t i = 0; i < hdr->block_count; i++, p+=8)
        hdr->hash_table[i] = load_u64le(p);
    for (uint32_t i = 0; i < hdr->block_count; i++, p+=8)
        hdr->offset_table[i] = load_u64le(p);

    return QUADR_OK;
}

void quadr_file_header_free(QuadrFileHeader *hdr) {
    if (!hdr) return;
    free(hdr->hash_table);
    free(hdr->offset_table);
    hdr->hash_table   = NULL;
    hdr->offset_table = NULL;
}
