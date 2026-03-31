/*
 * main.c  –  Quadr command-line tool  (v1.5)
 *
 * Commands:
 *   quadr encode [OPTIONS] <input> <output>
 *   quadr decode           <input> <output>
 *   quadr info             <file>
 *   quadr bench            <file>
 *   quadr probe   [OPTIONS] <file>    ← new: show per-block decisions
 *
 * Encode options:
 *   --hint=<generic|image|audio|sensor|float>
 *   --block=<KB>           block size in KB  (default 64)
 *   --xbit=<8|16|32|64>    sample width      (default 8)
 *   --stride=<N>           extra stride hint (default 0 = auto)
 *   --backend=<none|zlib-ng|zstd|lz4|lz4hc|7z>
 *   --level=<N>            backend level     (default: per-backend)
 *   --fast                 use fast probe (default: on)
 *
 * File wire format:
 *   [Quadr file header  18 + 16*N bytes]
 *   Per block:
 *     [uint8:  backend id]
 *     [uint32 LE: backend compressed size]
 *     [QuadrBlockHeader 12 bytes]
 *     [payload bytes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quadr_platform.h"
#include "quadr.h"
#include "quadr_zlib_compat.h"
#ifdef QUADR_HAVE_ZSTD
#  include <zstd.h>
#endif
#ifdef QUADR_HAVE_LZ4
#  include <lz4.h>
#endif
#ifdef QUADR_HAVE_LZ4HC
#  include <lz4hc.h>
#endif
/* Optional backends */
#ifdef QUADR_HAVE_7Z
#  include <lzma.h>    /* liblzma (commonly used for 7z/LZMA2) */
#endif
/* zlib-ng include handled above when QUADR_HAVE_ZLIBNG is defined */

/* ─────────────────────────────────────────────────────────────────────────
 * Backend
 * ───────────────────────────────────────────────────────────────────────── */

typedef enum {
    BACKEND_NONE  = 0,
    BACKEND_ZLIB  = 1,
    BACKEND_ZSTD  = 2,
    BACKEND_LZ4   = 3,
    BACKEND_LZ4HC = 4,
    BACKEND_7Z    = 5,
} Backend;

static const char *backend_name(Backend b) {
    switch (b) {
        case BACKEND_ZLIB:  return "zlib-ng";
        case BACKEND_ZSTD:  return "zstd";
        case BACKEND_LZ4:   return "lz4";
        case BACKEND_LZ4HC: return "lz4hc";
        case BACKEND_7Z:    return "7z";
        default:            return "none";
    }
}

static int backend_default_level(Backend b) {
    switch (b) {
        case BACKEND_LZ4:   return 0;
        case BACKEND_LZ4HC: return 9;
        case BACKEND_ZLIB:  return 6;
        case BACKEND_ZSTD:  return 3;
        case BACKEND_7Z:    return 6;
        default:            return 0;
    }
}

static size_t backend_bound(Backend b, size_t n) {
    switch (b) {
#ifdef QUADR_HAVE_ZLIBNG
        case BACKEND_ZLIB:  return QUADR_COMPRESSBOUND(n) + 16;
#endif
#ifdef QUADR_HAVE_ZSTD
        case BACKEND_ZSTD:  return ZSTD_compressBound(n) + 16;
#endif
#ifdef QUADR_HAVE_LZ4
        case BACKEND_LZ4:
        case BACKEND_LZ4HC: return (size_t)LZ4_compressBound((int)n) + 16;
#endif
#ifdef QUADR_HAVE_7Z
        case BACKEND_7Z:    return n + 64;
#endif
        default: return n + 16;
    }
}

static size_t backend_compress(Backend b, int level,
                               const uint8_t *in, size_t in_len,
                               uint8_t *out, size_t out_cap) {
    switch (b) {
    case BACKEND_NONE:
        if (out_cap < in_len) return 0;
        memcpy(out, in, in_len);
        return in_len;
#ifdef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        size_t ol = out_cap;
        return (QUADR_COMPRESS2(out, &ol, in, (uLong)in_len, level) == Z_OK)
               ? (size_t)ol : 0;
    }
#endif
#ifndef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        uLongf ol = (uLongf)out_cap;
        return (QUADR_COMPRESS2(out, &ol, in, (uLong)in_len, level) == Z_OK)
               ? (size_t)ol : 0;
    }
#endif
#ifdef QUADR_HAVE_ZSTD
    case BACKEND_ZSTD: {
        size_t r = ZSTD_compress(out, out_cap, in, in_len, level);
        return ZSTD_isError(r) ? 0 : r;
    }
#endif
#ifdef QUADR_HAVE_LZ4
    case BACKEND_LZ4: {
        int r = LZ4_compress_default((const char *)in, (char *)out,
                                     (int)in_len, (int)out_cap);
        return (r > 0) ? (size_t)r : 0;
    }
#endif
#ifdef QUADR_HAVE_LZ4HC
    case BACKEND_LZ4HC: {
        int r = LZ4_compress_HC((const char *)in, (char *)out,
                                (int)in_len, (int)out_cap, level);
        return (r > 0) ? (size_t)r : 0;
    }
#endif
#ifdef QUADR_HAVE_7Z
    case BACKEND_7Z: {
        size_t out_len = out_cap;
        uint32_t preset = (level < 0) ? 6u : (uint32_t)level;
        lzma_ret lr = lzma_easy_buffer_encode(preset,
                                              LZMA_CHECK_CRC64,
                                              NULL,
                                              in, in_len,
                                              out, &out_len, out_cap);
        return (lr == LZMA_OK) ? out_len : 0;
    }
#endif
    default:
        fprintf(stderr, "backend '%s' not compiled in\n", backend_name(b));
        return 0;
    }
}

static int backend_decompress(Backend b,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t expected) {
    switch (b) {
    case BACKEND_NONE:
        if (in_len != expected) return -1;
        memcpy(out, in, in_len);
        return 0;
#ifdef QUADR_HAVE_ZSTD
    case BACKEND_ZSTD: {
        size_t r = ZSTD_decompress(out, expected, in, in_len);
        return (!ZSTD_isError(r) && r == expected) ? 0 : -1;
    }
#endif
#ifdef QUADR_HAVE_LZ4
    case BACKEND_LZ4:
    case BACKEND_LZ4HC: {
        int r = LZ4_decompress_safe((const char *)in, (char *)out,
                                    (int)in_len, (int)expected);
        return (r == (int)expected) ? 0 : -1;
    }
#endif
#ifdef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        size_t ol = expected;
        return (QUADR_UNCOMPRESS(out, &ol, in, (uLong)in_len) == Z_OK
                && ol == expected) ? 0 : -1;
    }
#endif
#ifndef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        uLongf ol = (uLongf)expected;
        return (QUADR_UNCOMPRESS(out, &ol, in, (uLong)in_len) == Z_OK
                && (size_t)ol == expected) ? 0 : -1;
    }
#endif
#ifdef QUADR_HAVE_7Z
    case BACKEND_7Z: {
        lzma_stream strm = LZMA_STREAM_INIT;
        lzma_ret lr = lzma_stream_decoder(&strm, UINT64_MAX, 0);
        if (lr != LZMA_OK) return -1;
        strm.next_in = in;
        strm.avail_in = in_len;
        strm.next_out = out;
        strm.avail_out = expected;
        lr = lzma_code(&strm, LZMA_FINISH);
        size_t out_written = expected - strm.avail_out;
        lzma_end(&strm);
        return (lr == LZMA_STREAM_END && out_written == expected) ? 0 : -1;
    }
#endif
    default: return -1;
    }
}

/* ── Backend adapter wrappers matching QuadrBk* signatures ─────────────── */

typedef struct { Backend b; int level; } BkAdapter;

static size_t bk_adapter_compress(void *ud, int level,
                                  const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_cap) {
    BkAdapter *a = (BkAdapter *)ud;
    int lv = (level >= 0) ? level : a->level;
    return backend_compress(a->b, lv, in, in_len, out, out_cap);
}

static int bk_adapter_decompress(void *ud,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t expected) {
    BkAdapter *a = (BkAdapter *)ud;
    return backend_decompress(a->b, in, in_len, out, expected);
}

static size_t bk_adapter_bound(void *ud, size_t n) {
    BkAdapter *a = (BkAdapter *)ud;
    return backend_bound(a->b, n);
}

/* Register a backend with the global registry */
static void register_backend(Backend b) {
    static BkAdapter adapters[6];
    int idx = (int)b;
    adapters[idx].b = b;
    adapters[idx].level = backend_default_level(b);

    QuadrBackend reg = {
        .id             = (uint8_t)b,
        .name           = backend_name(b),
        .compress       = bk_adapter_compress,
        .decompress     = bk_adapter_decompress,
        .bound          = bk_adapter_bound,
        .userdata       = &adapters[idx],
        .default_level  = backend_default_level(b),
    };
    quadr_backend_register(&reg);
}

/* Initialize all compiled-in backends */
static void init_backends(void) {
#ifdef QUADR_HAVE_ZLIBNG
    register_backend(BACKEND_ZLIB);
#endif
#ifdef QUADR_HAVE_ZSTD
    register_backend(BACKEND_ZSTD);
#endif
#ifdef QUADR_HAVE_LZ4
    register_backend(BACKEND_LZ4);
    register_backend(BACKEND_LZ4HC);
#endif
#ifdef QUADR_HAVE_7Z
    register_backend(BACKEND_7Z);
#endif
}

/* ─────────────────────────────────────────────────────────────────────────
 * Encode config
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    QuadrEncodeOpts quadr;
    Backend         backend;
    int             level;
    int             use_fast_probe;
    int             mixed_backend;
} EncodeConfig;

static void encode_config_default(EncodeConfig *c) {
    quadr_encode_opts_default(&c->quadr);
    c->backend        = BACKEND_NONE;
    c->level          = -1;
    c->use_fast_probe = 1;
    c->mixed_backend  = 0;
}

static int effective_level(const EncodeConfig *c) {
    return (c->level >= 0) ? c->level : backend_default_level(c->backend);
}

/* ─────────────────────────────────────────────────────────────────────────
 * File I/O
 * ───────────────────────────────────────────────────────────────────────── */

static uint8_t *read_file(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); *len = 0; return calloc(1,1); }
    uint8_t *b = malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz)
        { free(b); fclose(f); return NULL; }
    fclose(f); *len = (size_t)sz; return b;
}

static int write_file(const char *p, const uint8_t *b, size_t n) {
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); return -1; }
    int ok = (fwrite(b, 1, n, f) == n);
    fclose(f); return ok ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Probe selection shim
 * ───────────────────────────────────────────────────────────────────────── */

static QuadrProbeResult do_probe(const uint8_t *data, size_t len,
                                 const EncodeConfig *cfg) {
    return cfg->use_fast_probe
           ? quadr_probe_fast(data, len, &cfg->quadr)
           : quadr_probe(data, len, &cfg->quadr);
}

/* ─────────────────────────────────────────────────────────────────────────
 * encode
 * ───────────────────────────────────────────────────────────────────────── */

/* Mixed backend callback: PASSTHROUGH blocks use lz4 (faster) */
static uint8_t mixed_backend_fn(const QuadrProbeResult *probe,
                                 size_t block_idx, void *ud) {
    (void)block_idx; (void)ud;
    if (probe->type == QUADR_BLOCK_PASSTHROUGH) {
#ifdef QUADR_HAVE_LZ4
        return (uint8_t)BACKEND_LZ4;
#endif
    }
    return 0;
}

static int cmd_encode(const char *in_path, const char *out_path,
                      const EncodeConfig *cfg) {
    int lv = effective_level(cfg);

    QuadrEncodeOpts final_opts = cfg->quadr;
    if (cfg->quadr.adaptive_block) {
        FILE *fprobe = fopen(in_path, "rb");
        if (fprobe) {
            uint8_t sample[16384];
            size_t  slen = fread(sample, 1, sizeof(sample), fprobe);
            fclose(fprobe);
            if (slen > 0) {
                QuadrProbeResult pr = quadr_probe_fast(sample, slen, &final_opts);
                if (pr.type == QUADR_BLOCK_DELTA)
                    final_opts.block_size = QUADR_BLOCK_SIZE_MAX;
                else if (pr.type == QUADR_BLOCK_RLE)
                    final_opts.block_size = QUADR_BLOCK_SIZE_DEFAULT / 2;
                else
                    final_opts.block_size = QUADR_BLOCK_SIZE_DEFAULT;
            }
        }
    }

    uint64_t total_input_bytes = 0;
    FILE *fstat = fopen(in_path, "rb");
    if (fstat) {
        if (fseek(fstat, 0, SEEK_END) == 0) {
            long pos = ftell(fstat);
            if (pos >= 0) total_input_bytes = (uint64_t)pos;
        }
        fclose(fstat);
    }

    QuadrStreamCtx *ctx = quadr_stream_encode_open(out_path, &final_opts,
                                                    (uint8_t)cfg->backend, lv,
                                                    total_input_bytes);
    if (!ctx) { fprintf(stderr, "failed to open output: %s\n", out_path); return 1; }

    /* Use the registry backend — no manual adapter needed */
    const QuadrBackend *bk = quadr_backend_find((uint8_t)cfg->backend);
    if (bk && bk->id != QUADR_BACKEND_ID_PASSTHROUGH) {
        quadr_stream_set_backend(ctx,
                                 bk->compress, bk->decompress, bk->bound,
                                 bk->userdata);
    }
    if (cfg->mixed_backend) {
        quadr_stream_set_block_backend_fn(ctx, mixed_backend_fn, NULL);
    }

    FILE *fin = fopen(in_path, "rb");
    if (!fin) {
        perror(in_path);
        quadr_stream_encode_close(ctx);
        return 1;
    }

    uint32_t bs = final_opts.block_size;
    uint8_t *read_buf = malloc(bs);
    if (!read_buf) {
        fclose(fin); quadr_stream_encode_close(ctx); return 1;
    }

    double t0 = quadr_now_ms();
    int    ok  = 1;

    for (;;) {
        size_t got = fread(read_buf, 1, bs, fin);
        if (got == 0) break;
        QuadrError e = quadr_stream_feed(ctx, read_buf, got);
        if (e != QUADR_OK) {
            fprintf(stderr, "encode error: %s\n", quadr_strerror(e));
            ok = 0; break;
        }
    }

    fclose(fin);
    free(read_buf);

    if (!ok) { quadr_stream_encode_close(ctx); return 1; }

    QuadrError e = quadr_stream_encode_close(ctx);
    if (e != QUADR_OK) {
        fprintf(stderr, "encode close: %s\n", quadr_strerror(e));
        return 1;
    }

    double ms     = quadr_now_ms() - t0;
    uint64_t in_b = ctx ? 0 : 0;   /* ctx is closed; get size via stat */

    /* Report using file sizes */
    FILE *fi = fopen(in_path,  "rb");
    FILE *fo = fopen(out_path, "rb");
    size_t sz_in = 0, sz_out = 0;
    if (fi) { fseek(fi,0,SEEK_END); sz_in  = (size_t)ftell(fi); fclose(fi); }
    if (fo) { fseek(fo,0,SEEK_END); sz_out = (size_t)ftell(fo); fclose(fo); }

    double ratio = sz_in ? (double)sz_out / (double)sz_in * 100.0 : 0.0;
    double gbps  = (ms > 0 && sz_in) ? (double)sz_in / 1e9 / (ms * 1e-3) : 0.0;
    (void)in_b;

    printf("Encoded  %s  ->  %s\n", in_path, out_path);
    printf("  %zu  ->  %zu bytes  (%.2f%%)   %.0f ms  %.2f GB/s\n",
           sz_in, sz_out, ratio, ms, gbps);
    printf("  block_size=%uKB  backend=%s(L%d)  SIMD=%s  probe=%s\n",
           bs/1024, backend_name(cfg->backend), lv,
           quadr_simd_level_name(quadr_simd_level()),
           cfg->use_fast_probe ? "fast" : "full");
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * decode
 * ───────────────────────────────────────────────────────────────────────── */

static int cmd_decode(const char *in_path, const char *out_path) {
    QuadrStreamCtx *ctx = quadr_stream_decode_open(in_path, 0);
    if (!ctx) { fprintf(stderr, "not a valid Quadr file: %s\n", in_path); return 1; }
    /* Backend resolution is handled by the stream layer via the registry.
     * No custom adapter needed — decode_next_block() looks up the backend
     * by ID from the per-block frame header. */

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        perror(out_path); quadr_stream_close(ctx); return 1;
    }

    uint8_t *buf = malloc(QUADR_BLOCK_SIZE_DEFAULT);
    if (!buf) { fclose(fout); quadr_stream_close(ctx); return 1; }

    double t0   = quadr_now_ms();
    size_t total = 0;
    int    ok    = 1;

    for (;;) {
        size_t written = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, QUADR_BLOCK_SIZE_DEFAULT, &written);
        /* Write whatever was pulled before checking the error code —
           TRUNC is returned even when the last partial chunk is valid. */
        if (written && fwrite(buf, 1, written, fout) != written) {
            perror(out_path); ok = 0; break;
        }
        total += written;
        if (e == QUADR_ERR_TRUNC) break;   /* clean EOF */
        if (e != QUADR_OK) {
            fprintf(stderr, "decode error: %s\n", quadr_strerror(e));
            ok = 0; break;
        }
    }

    free(buf);
    fclose(fout);
    quadr_stream_close(ctx);

    if (!ok) { return 1; }

    double ms   = quadr_now_ms() - t0;
    double gbps = (ms > 0 && total) ? (double)total / 1e9 / (ms * 1e-3) : 0.0;
    printf("Decoded  %s  ->  %s  (%zu bytes)   %.0f ms  %.2f GB/s\n",
           in_path, out_path, total, ms, gbps);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * info
 * ───────────────────────────────────────────────────────────────────────── */

static int cmd_info(const char *path) {
    size_t fl = 0;
    uint8_t *fb = read_file(path, &fl);
    if (!fb) return 1;
    QuadrFileHeader fh = {0};
    if (quadr_file_header_read(fb, fl, &fh) != QUADR_OK) {
        fprintf(stderr, "not a Quadr file\n"); free(fb); return 1;
    }
    const char *hint_names[] = {"generic","image","audio","sensor","float",
                                 "?","?","?",
                                 [0xFF & 0xF]="force_auto"};
    printf("File:        %s\n", path);
    printf("Version:     0x%02X\n", fh.version);
    printf("Original:    %llu bytes\n", (unsigned long long)fh.total_uncomp_size);
    printf("On-disk:     %zu bytes  (%.2f%%)\n", fl,
           fh.total_uncomp_size ? (double)fl/(double)fh.total_uncomp_size*100.0 : 0.0);
    printf("Blocks:      %u\n", fh.block_count);
    printf("DataHint:    0x%02X\n", fh.data_hint);
    printf("SIMD:        %s\n", quadr_simd_level_name(quadr_simd_level()));

    /* Walk blocks using offset_table — works for both streaming (reserved
       header) and non-streaming (exact header) encoded files.           */
    uint32_t tc[4] = {0};
    /* Track counts for all known backends (enum values up to BACKEND_7Z) */
    uint32_t backends[6] = {0};
    for (uint32_t i = 0; i < fh.block_count; i++) {
        size_t rp = (size_t)fh.offset_table[i];
        if (rp + 5 + QUADR_BLOCK_HEADER_SIZE > fl) break;
        uint8_t  bid   = fb[rp];
        if (bid < 6) backends[bid]++;
        QuadrBlockHeader bh;
        if (quadr_block_header_read(fb + rp + 5, &bh) == QUADR_OK)
            tc[bh.type & 3]++;
    }
    printf("Block types: DELTA=%u  RLE=%u  PASSTHROUGH=%u  RAW=%u\n",
           tc[0], tc[1], tc[2], tc[3]);
    printf("Backends:    none=%u  zlib-ng=%u  zstd=%u  lz4=%u  lz4hc=%u  7z=%u\n",
           backends[0], backends[1], backends[2], backends[3], backends[4], backends[5]);

    quadr_file_header_free(&fh); free(fb);
    return 0;
    (void)hint_names;
}

/* ─────────────────────────────────────────────────────────────────────────
 * verify
 * ───────────────────────────────────────────────────────────────────────── */

static int cmd_verify(const char *path) {
    double t0 = quadr_now_ms();

    QuadrStreamCtx *ctx = quadr_stream_decode_open(path, 0);
    if (!ctx) {
        fprintf(stderr, "FAIL  %s: not a valid Quadr file\n", path);
        return 1;
    }
    /* Backend resolution handled by stream layer via registry */

    uint8_t  buf[65536];
    uint32_t blk_ok = 0;
    int      failed = 0;
    uint32_t bad_blk = UINT32_MAX;

    for (;;) {
        uint32_t before = quadr_stream_current_bid(ctx);  /* reuse as block index proxy */
        (void)before;
        size_t got = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, sizeof(buf), &got);
        if (e == QUADR_ERR_TRUNC) { blk_ok++; break; }
        if (e != QUADR_OK) {
            bad_blk = blk_ok;
            fprintf(stderr, "FAIL  %s  block %u: %s\n",
                    path, bad_blk, quadr_strerror(e));
            failed = 1;
            break;
        }
        blk_ok++;
    }

    double ms = quadr_now_ms() - t0;
    quadr_stream_close(ctx);

    if (!failed)
        printf("OK  %s  (%u blocks  %.0f ms)\n", path, blk_ok, ms);
    return failed;
}

/* ─────────────────────────────────────────────────────────────────────────
 * probe  (per-block decision inspection)
 * ───────────────────────────────────────────────────────────────────────── */

static int cmd_probe(const char *path, const EncodeConfig *cfg) {
    size_t raw_len = 0;
    uint8_t *raw = read_file(path, &raw_len);
    if (!raw) return 1;

    uint32_t bs = cfg->quadr.block_size;
    uint32_t bc = raw_len ? (uint32_t)((raw_len + bs - 1) / bs) : 0;
    const char *type_str[] = {"DELTA","RLE","PASSTHROUGH","RAW"};

    printf("Probe: %s  (%zu bytes  %u blocks x %uKB)  SIMD=%s  mode=%s\n",
           path, raw_len, bc, bs/1024,
           quadr_simd_level_name(quadr_simd_level()),
           cfg->use_fast_probe ? "fast" : "full");
    printf("%-6s  %-12s  %-7s  %-7s  %-6s  %s\n",
           "Block", "Type", "Stride", "Shuffle", "Score", "Size");
    printf("─────────────────────────────────────────────────────────\n");

    uint32_t tc[4] = {0};
    double t_probe = 0.0;

    for (uint32_t bi = 0; bi < bc; bi++) {
        size_t off  = (size_t)bi * bs;
        size_t blen = (off + bs <= raw_len) ? bs : raw_len - off;
        const uint8_t *blk = raw + off;

        double t0, t1;
        t0 = quadr_now_ms();
        QuadrProbeResult r = do_probe(blk, blen, cfg);
        t1 = quadr_now_ms();
        t_probe += (t1 - t0);

        tc[r.type]++;
        printf("%-6u  %-12s  %-7d  %-7d  %-6.3f  %zu B\n",
               bi, type_str[r.type], r.stride, r.shuffle, r.score, blen);
    }

    printf("─────────────────────────────────────────────────────────\n");
    printf("Summary: DELTA=%u  RLE=%u  PASSTHROUGH=%u  RAW=%u\n",
           tc[0], tc[1], tc[2], tc[3]);
    printf("Probe total: %.2f ms  (%.2f ms/block  %.2f GB/s)\n",
           t_probe,
           bc ? t_probe / bc : 0.0,
           (raw_len && t_probe > 0) ? (double)raw_len / 1e9 / (t_probe * 1e-3) : 0.0);

    free(raw);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * bench
 * ───────────────────────────────────────────────────────────────────────── */

static int cmd_bench(const char *path) {
    size_t rl = 0;
    uint8_t *raw = read_file(path, &rl);
    if (!raw) return 1;
    uint8_t *out = malloc(rl + 64);
    if (!out) { free(raw); return 1; }

    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);

    printf("Bench: %s  (%zu bytes)  SIMD=%s\n",
           path, rl, quadr_simd_level_name(quadr_simd_level()));

    /* Run 5 iterations, report best */
    double be = 1e9, bd = 1e9, bp_fast = 1e9, bp_full = 1e9;
    QuadrProbeResult probe_r;

    for (int it = 0; it < 5; it++) {
        size_t ol = rl + 64;
        double t0, t1;

        /* fast probe */
        t0 = quadr_now_ms();
        probe_r = quadr_probe_fast(raw, rl, &opts);
        t1 = quadr_now_ms();
        double ms = (t1 - t0);
        if (ms < bp_fast) bp_fast = ms;

        /* full probe */
        t0 = quadr_now_ms();
        quadr_probe(raw, rl, &opts);
        t1 = quadr_now_ms();
        ms = (t1 - t0);
        if (ms < bp_full) bp_full = ms;

        /* encode (using probe result) */
        t0 = quadr_now_ms();
        ol = rl + 64;
        quadr_block_encode(raw, rl, out, &ol, &opts, &probe_r);
        t1 = quadr_now_ms();
        ms = (t1 - t0);
        if (ms < be) { be = ms; }

        /* decode */
        uint8_t *dec = malloc(rl);
        if (dec) {
            QuadrBlockHeader bh = {
                .uncomp_size  = (uint32_t)rl,
                .comp_size    = (uint32_t)ol,
                .type         = probe_r.type,
                .shuffle_flag = probe_r.shuffle,
                .x_bit        = opts.x_bit,
                .stride       = probe_r.stride,
                .word_size    = probe_r.word_size,
            };
            t0 = quadr_now_ms();
            quadr_block_decode(out, ol, dec, rl, &bh);
            t1 = quadr_now_ms();
            ms = (t1 - t0);
            if (ms < bd) bd = ms;
            free(dec);
        }
    }

    const char *tn[] = {"DELTA","RLE","PASSTHROUGH","RAW"};
    printf("  Block type:  %s  (stride=%d shuffle=%d)\n",
           tn[probe_r.type], probe_r.stride, probe_r.shuffle);
    printf("  Probe (fast): %.3f ms  %.2f GB/s\n",
           bp_fast, rl ? (double)rl/1e9/(bp_fast*1e-3) : 0.0);
    printf("  Probe (full): %.3f ms  %.2f GB/s  (fast speedup: %.1fx)\n",
           bp_full, rl ? (double)rl/1e9/(bp_full*1e-3) : 0.0,
           bp_full > 0 ? bp_full/bp_fast : 1.0);
    printf("  Encode:       %.3f ms  %.2f GB/s\n",
           be, rl ? (double)rl/1e9/(be*1e-3) : 0.0);
    printf("  Decode:       %.3f ms  %.2f GB/s\n",
           bd, rl ? (double)rl/1e9/(bd*1e-3) : 0.0);

    free(raw); free(out);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Argument parsing
 * ───────────────────────────────────────────────────────────────────────── */

static int parse_hint(const char *s, uint8_t *o) {
    if (!strcmp(s,"generic"))  { *o=QUADR_HINT_GENERIC;   return 0; }
    if (!strcmp(s,"image"))    { *o=QUADR_HINT_IMAGE;     return 0; }
    if (!strcmp(s,"audio"))    { *o=QUADR_HINT_AUDIO_PCM; return 0; }
    if (!strcmp(s,"sensor"))   { *o=QUADR_HINT_SENSOR;    return 0; }
    if (!strcmp(s,"float"))    { *o=QUADR_HINT_FLOAT;     return 0; }
    return -1;
}

static int parse_backend(const char *s, Backend *o) {
    if (!strcmp(s,"none"))  { *o=BACKEND_NONE;  return 0; }
    if (!strcmp(s,"zlib") || !strcmp(s,"zlib-ng") || !strcmp(s,"zlibng"))  { *o=BACKEND_ZLIB;  return 0; }
    if (!strcmp(s,"zstd"))  { *o=BACKEND_ZSTD;  return 0; }
    if (!strcmp(s,"lz4"))   { *o=BACKEND_LZ4;   return 0; }
    if (!strcmp(s,"lz4hc")) { *o=BACKEND_LZ4HC; return 0; }
    if (!strcmp(s,"7z"))    { *o=BACKEND_7Z;    return 0; }
    return -1;
}

/* Parse options that are shared between encode and probe */
static int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg) {
    (void)argc;
    char *arg = argv[*i];
    char *eq  = strchr(arg, '=');
    const char *val = eq ? eq+1 : "";

    if      (!strncmp(arg,"--hint=",7))    return parse_hint(val, &cfg->quadr.data_hint);
    else if (!strncmp(arg,"--block=",8))   { cfg->quadr.block_size=(uint32_t)(atoi(val)*1024); return 0; }
    else if (!strncmp(arg,"--xbit=",7))    { cfg->quadr.x_bit=(uint8_t)atoi(val); return 0; }
    else if (!strncmp(arg,"--stride=",9))  { cfg->quadr.hint_stride=(uint8_t)atoi(val); return 0; }
    else if (!strncmp(arg,"--backend=",10)) { return parse_backend(val, &cfg->backend); }
    else if (!strncmp(arg,"--level=",8))   { cfg->level=atoi(val); return 0; }
    else if (!strcmp(arg,"--fast"))             { cfg->use_fast_probe=1; return 0; }
    else if (!strcmp(arg,"--no-fast"))          { cfg->use_fast_probe=0; return 0; }
    else if (!strcmp(arg,"--mixed-backend"))    { cfg->mixed_backend=1;  return 0; }
    else if (!strcmp(arg,"--adaptive-block"))   { cfg->quadr.adaptive_block=1; return 0; }

    fprintf(stderr, "unknown option: %s\n", arg);
    return -1;
}

static void usage(const char *p) {
    printf(
        "Usage:\n"
        "  %s encode [OPTIONS] <input> <output>\n"
        "  %s decode           <input> <output>\n"
        "  %s info             <file>\n"
        "  %s bench            <file>\n"
        "  %s verify           <file>\n"
        "  %s probe   [OPTIONS] <file>\n"
        "\nOptions:\n"
        "  --hint=<generic|image|audio|sensor|float>\n"
        "  --block=<KB>           block size (default 64)\n"
        "  --xbit=<8|16|32|64>    sample width (default 8)\n"
        "  --stride=<N>           extra stride hint\n"
        "  --backend=<none|zlib-ng|zstd|lz4|lz4hc|7z>\n"
        "  --level=<N>            backend level\n"
        "  --fast / --no-fast     probe mode (default: fast)\n"
        "  --mixed-backend        DELTA→default backend, PASSTHROUGH→lz4\n"
        "  --adaptive-block       auto-select block size from data type\n"
        "\nBuilt-in backends:",
        p, p, p, p, p, p);
#if 0
/* zlib-ng handled via BACKEND_ZLIB */
#endif
#ifdef QUADR_HAVE_ZLIBNG
    printf(" zlib-ng");
#endif
#ifdef QUADR_HAVE_ZSTD
    printf(" zstd");
#endif
#ifdef QUADR_HAVE_LZ4
    printf(" lz4");
#endif
#ifdef QUADR_HAVE_LZ4HC
    printf(" lz4hc");
#endif
#ifdef QUADR_HAVE_7Z
    printf(" 7z");
#endif
    printf("\nSIMD: %s\n", quadr_simd_level_name(quadr_simd_level()));
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    init_backends();
    if (argc < 2) { usage(argv[0]); return 1; }
    const char *sub = argv[1];

    if (!strcmp(sub, "encode") || !strcmp(sub, "probe")) {
        EncodeConfig cfg;
        encode_config_default(&cfg);
        int i = 2;
        for (; i < argc && argv[i][0] == '-'; i++)
            if (parse_encode_opts(argc, argv, &i, &cfg) != 0) return 1;

        if (!strcmp(sub, "encode")) {
            if (argc - i < 2) { usage(argv[0]); return 1; }
            return cmd_encode(argv[i], argv[i+1], &cfg);
        } else {
            if (argc - i < 1) { usage(argv[0]); return 1; }
            return cmd_probe(argv[i], &cfg);
        }
    }
    if (!strcmp(sub,"decode") && argc >= 4) return cmd_decode(argv[2], argv[3]);
    if (!strcmp(sub,"info")   && argc >= 3) return cmd_info(argv[2]);
    if (!strcmp(sub,"bench")  && argc >= 3) return cmd_bench(argv[2]);
    if (!strcmp(sub,"verify") && argc >= 3) return cmd_verify(argv[2]);

    usage(argv[0]); return 1;
}
