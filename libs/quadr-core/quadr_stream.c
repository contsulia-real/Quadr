/*
 * quadr_stream.c  –  Streaming file encode / decode
 *
 * Header reservation strategy (encode)
 * ─────────────────────────────────────
 * The caller passes total_input_bytes to quadr_stream_encode_open().
 * From that we compute block_count = ceil(input / block_size) and write
 * a placeholder of exactly quadr_file_header_size(block_count) bytes at
 * the start of the file.  Blocks are then appended after it.  On close,
 * we seek back to offset 0 and overwrite with the real header — which is
 * guaranteed to be exactly the same size.
 *
 * For unknown-size streams (total_input_bytes == 0) we fall back to a
 * conservative reservation of STREAM_RESERVE_BLOCKS (1024) blocks.
 *
 * Backend compress/decompress
 * ────────────────────────────
 * The stream layer accepts a pair of function pointers so that the
 * application can inject any backend (zlib, zstd, lz4, …) without
 * coupling the library to those dependencies.  The quadr.h API
 * provides a "no-op" (passthrough) default.  The CLI (main.c)
 * provides its own wrappers that call zlib/zstd/lz4 as compiled in.
 */

#include "quadr.h"
#include "quadr_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ─── Portable large-file seek/tell ─────────────────────────────────────── */

#if defined(QUADR_OS_WINDOWS)
#  include <windows.h>  /* already pulled in by quadr_platform.h */
   /* Use MSVC CRT 64-bit seek */
#  define quadr_fseek64(f, off, w) _fseeki64((f), (__int64)(off), (w))
#  define quadr_ftell64(f)         (int64_t)_ftelli64(f)
#elif defined(__linux__) || defined(__APPLE__)
#  define quadr_fseek64(f, off, w) fseeko((f), (off_t)(off), (w))
#  define quadr_ftell64(f)         (int64_t)ftello(f)
#else
#  define quadr_fseek64(f, off, w) fseek((f), (long)(off), (w))
#  define quadr_ftell64(f)         (int64_t)ftell(f)
#endif

/* ─── LE helpers ─────────────────────────────────────────────────────────── */

static void   w32le(uint8_t *p, uint32_t v)
    { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static uint32_t r32le(const uint8_t *p)
    { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

/* ─── Block frame constants ──────────────────────────────────────────────── */

/* Per-block framing: [1 byte backend_id][4 bytes comp_size][12 bytes QuadrBlockHeader] */
#define FRAME_HDR  (1u + 4u + QUADR_BLOCK_HEADER_SIZE)

/* Conservative fallback reservation when input size is unknown */
#define STREAM_RESERVE_BLOCKS  1024u

/* ─────────────────────────────────────────────────────────────────────────
 * Backend function pointer types (internal aliases of the public ones)
 * ───────────────────────────────────────────────────────────────────────── */

typedef size_t (*QuadrBkCompress)  (void *userdata, int level,
                                    const uint8_t *in,  size_t in_len,
                                    uint8_t       *out, size_t out_cap);
typedef int    (*QuadrBkDecompress)(void *userdata,
                                    const uint8_t *in,  size_t in_len,
                                    uint8_t       *out, size_t expected);
typedef size_t (*QuadrBkBound)     (void *userdata, size_t in_len);

/* Resolve backend functions from the registry.  Returns the passthrough
 * backend if the requested ID is not found.                               */
static const QuadrBackend *resolve_backend(uint8_t id) {
    const QuadrBackend *bk = quadr_backend_find(id);
    return bk ? bk : quadr_backend_passthrough();
}

/* ─────────────────────────────────────────────────────────────────────────
 * Context
 * ───────────────────────────────────────────────────────────────────────── */

struct QuadrStreamCtx {
    int is_encode;
    FILE *fp;

    /* backend */
    uint8_t          backend_id;
    int              backend_level;
    QuadrBkCompress  bk_compress;
    QuadrBkDecompress bk_decompress;
    QuadrBkBound     bk_bound;
    void            *bk_userdata;

    /* encode state */
    QuadrEncodeOpts opts;
    uint8_t *in_buf;     /* accumulator, block_size bytes */
    size_t   in_pos;
    uint8_t *q_buf;
    uint8_t *sh_buf;     /* shuffle work buffer, block_size bytes */
    uint8_t *bk_buf;
    size_t   bk_buf_cap;

    /* block metadata */
    uint32_t block_count;
    uint32_t block_reserved; /* how many slots are in the on-disk header */
    uint64_t *hash_table;
    uint64_t *offset_table;
    uint32_t  meta_cap;      /* allocated array size */

    /* decode state */
    QuadrFileHeader fhdr;
    uint8_t *dec_buf;
    size_t   dec_len;
    size_t   dec_pos;
    uint32_t cur_block;

    /* stats */
    uint64_t bytes_in;
    uint64_t bytes_out;

    /* per-block backend override */
    QuadrBlockBackendFn blk_backend_fn;
    void               *blk_backend_ud;
};

/* ─────────────────────────────────────────────────────────────────────────
 * Grow the metadata arrays
 * ───────────────────────────────────────────────────────────────────────── */

static int ensure_meta_cap(QuadrStreamCtx *ctx) {
    if (ctx->block_count < ctx->meta_cap) return 0;
    uint32_t nc = ctx->meta_cap ? ctx->meta_cap * 2 : 256;
    uint64_t *ht = realloc(ctx->hash_table,   nc * sizeof(uint64_t));
    uint64_t *ot = realloc(ctx->offset_table, nc * sizeof(uint64_t));
    if (!ht || !ot) return -1;
    ctx->hash_table   = ht;
    ctx->offset_table = ot;
    ctx->meta_cap     = nc;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Flush one block to disk
 * ───────────────────────────────────────────────────────────────────────── */

static QuadrError flush_block(QuadrStreamCtx *ctx,
                              const uint8_t *data, size_t data_len) {
    if (!data_len) return QUADR_OK;
    if (ensure_meta_cap(ctx)) return QUADR_ERR_OOM;

    /* Quadr transform */
    QuadrProbeResult probe;
    size_t q_len = data_len + 64;
    QuadrError e = quadr_block_encode(data, data_len, ctx->q_buf, &q_len,
                                      &ctx->opts, &probe, ctx->sh_buf);
    if (e != QUADR_OK) return e;

    /* Per-block backend override: let the callback pick a different backend
       based on the probe result (e.g. DELTA→lz4hc, PASSTHROUGH→lz4).    */
    uint8_t saved_bid    = ctx->backend_id;
    uint8_t active_bid   = ctx->backend_id;
    int     active_level = ctx->backend_level;
    if (ctx->blk_backend_fn) {
        uint8_t override = ctx->blk_backend_fn(&probe, ctx->block_count,
                                               ctx->blk_backend_ud);
        if (override != 0) active_bid = override;
    }
    /* Temporarily expose the active bid so the app-layer bk_compress
     * wrapper can read it via quadr_stream_current_bid(ctx).           */
    ctx->backend_id = active_bid;

    /* Backend compress */
    size_t bk_cap = ctx->bk_compress(ctx->bk_userdata, active_level,
                                     ctx->q_buf, q_len,
                                     ctx->bk_buf, ctx->bk_buf_cap);

    ctx->backend_id = saved_bid;   /* restore default for next block */
    /* If backend failed or expanded, fallback to passthrough */
    if (!bk_cap || bk_cap > ctx->bk_buf_cap) {
        const QuadrBackend *pt = quadr_backend_passthrough();
        bk_cap = pt->compress(pt->userdata, 0, ctx->q_buf, q_len,
                              ctx->bk_buf, ctx->bk_buf_cap);
        if (!bk_cap) return QUADR_ERR_BUF_SMALL;
        active_bid = 0;   /* record as passthrough so decoder uses memcpy */
    }

    /* Record metadata */
    uint32_t bi = ctx->block_count;
    ctx->hash_table[bi]   = quadr_xxh3_64(data, data_len);
    ctx->offset_table[bi] = (uint64_t)quadr_ftell64(ctx->fp);

    /* Write frame header */
    uint8_t fhdr[FRAME_HDR];
    fhdr[0] = active_bid;
    w32le(fhdr + 1, (uint32_t)bk_cap);

    QuadrBlockHeader bh = {
        .uncomp_size  = (uint32_t)data_len,
        .comp_size    = (uint32_t)q_len,
        .type         = probe.type,
        .shuffle_flag = probe.shuffle,
        .x_bit        = ctx->opts.x_bit,
        .stride       = probe.stride,
        .word_size    = probe.word_size,
    };
    quadr_block_header_write(&bh, fhdr + 5);

    if (fwrite(fhdr, 1, FRAME_HDR, ctx->fp) != FRAME_HDR) return QUADR_ERR_IO;
    if (fwrite(ctx->bk_buf, 1, bk_cap, ctx->fp) != bk_cap) return QUADR_ERR_IO;

    ctx->block_count++;
    ctx->bytes_out += FRAME_HDR + bk_cap;
    return QUADR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_encode_open
 * ───────────────────────────────────────────────────────────────────────── */

QuadrStreamCtx *quadr_stream_encode_open(const char *out_path,
                                          const QuadrEncodeOpts *opts,
                                          uint8_t backend_id,
                                          int     backend_level,
                                          uint64_t total_input_bytes) {
    if (!out_path || !opts) return NULL;

    QuadrStreamCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->is_encode     = 1;
    ctx->opts          = *opts;
    ctx->backend_id    = backend_id;
    ctx->backend_level = backend_level;

    const QuadrBackend *def = quadr_backend_find(backend_id);
    if (!def) def = quadr_backend_passthrough();
    ctx->bk_compress   = def->compress;
    ctx->bk_decompress = def->decompress;
    ctx->bk_bound      = def->bound;
    ctx->bk_userdata   = def->userdata;

    uint32_t bs = opts->block_size;

    if (bs == 0 && opts->adaptive_block && total_input_bytes > 0) {
        /* Auto-select block size: aim for ~4 blocks, clamped to [MIN, MAX] */
        uint64_t target = total_input_bytes / 4;
        if (target < QUADR_BLOCK_SIZE_MIN) target = QUADR_BLOCK_SIZE_MIN;
        if (target > QUADR_BLOCK_SIZE_MAX) target = QUADR_BLOCK_SIZE_MAX;
        bs = (uint32_t)target;
    } else if (bs == 0) {
        bs = QUADR_BLOCK_SIZE_DEFAULT;
    }

    /* Clamp block size to supported range to avoid pathological values */
    if (bs < QUADR_BLOCK_SIZE_MIN) bs = QUADR_BLOCK_SIZE_MIN;
    if (bs > QUADR_BLOCK_SIZE_MAX) bs = QUADR_BLOCK_SIZE_MAX;
    ctx->opts.block_size = bs;
    ctx->opts.adaptive_block = opts->adaptive_block;

    size_t bk_max = ctx->bk_bound(ctx->bk_userdata, bs) + 64;
    ctx->bk_buf_cap = bk_max;
    ctx->in_buf = malloc(bs + 64);
    ctx->q_buf  = malloc(bs + 64);
    /* sh_buf is passed as work_buf to quadr_block_encode → quadr_probe_fast,
     * which expects: [0..len-1] full_tmp, [len..2*len-1] sh_buf,
     * [2*len..2*len+4095] tmp. Total: 2*len + PROBE_SAMPLE_LEN (4096). */
    ctx->sh_buf = malloc(2 * bs + 4096 + 64);
    ctx->bk_buf = malloc(bk_max);
    if (!ctx->in_buf || !ctx->q_buf || !ctx->sh_buf || !ctx->bk_buf) goto fail;

    ctx->fp = fopen(out_path, "wb");
    if (!ctx->fp) goto fail;

    /*
     * Write placeholder header.
     * We use STREAM_RESERVE_BLOCKS as the reservation; the real block count
     * is written on close.  The data section starts at offset
     * quadr_file_header_size(STREAM_RESERVE_BLOCKS) so the header rewrite
     * never touches block data (as long as actual blocks ≤ reservation).
     * If more blocks are needed, they still get written correctly — only
     * the offset_table for those extra blocks won't fit in the placeholder,
     * which is fine because we rewrite the whole header on close and the
     * extra blocks come after the original reservation area.
     *
     * For typical use (64 KB blocks, ≤ 64 GB files): 1 M blocks max needed.
     * STREAM_RESERVE_BLOCKS = 1024 supports ≤ 64 MB files without gap waste.
     * For larger files we fall back to a temp-file strategy (see close).
     */
    /* Determine reservation: if caller supplied total_input_bytes, compute
     * exact block count and reserve just that many slots to avoid large
     * header overhead for small files. Otherwise fall back to conservative
     * STREAM_RESERVE_BLOCKS. */
    if (total_input_bytes > 0) {
        uint32_t exact_blocks = (uint32_t)((total_input_bytes + bs - 1) / bs);
        if (exact_blocks == 0) exact_blocks = 1;
        ctx->block_reserved = exact_blocks;
    } else {
        ctx->block_reserved = STREAM_RESERVE_BLOCKS;
    }
    size_t hdr_reserved = quadr_file_header_size(ctx->block_reserved);
    uint8_t *ph = calloc(1, hdr_reserved);
    if (!ph) goto fail;
    fwrite(ph, 1, hdr_reserved, ctx->fp);
    free(ph);

    return ctx;

fail:
    if (ctx->fp)   fclose(ctx->fp);
    free(ctx->in_buf); free(ctx->q_buf); free(ctx->sh_buf); free(ctx->bk_buf);
    free(ctx);
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_set_backend  (call after open, before any feed)
 * ───────────────────────────────────────────────────────────────────────── */

void quadr_stream_set_backend(QuadrStreamCtx *ctx,
                               QuadrBkCompress   compress_fn,
                               QuadrBkDecompress decompress_fn,
                               QuadrBkBound      bound_fn,
                               void             *userdata) {
    if (!ctx) return;
    const QuadrBackend *pt = quadr_backend_passthrough();
    ctx->bk_compress   = compress_fn   ? compress_fn   : pt->compress;
    ctx->bk_decompress = decompress_fn ? decompress_fn : pt->decompress;
    ctx->bk_bound      = bound_fn      ? bound_fn      : pt->bound;
    ctx->bk_userdata   = userdata;
    /* Resize bk_buf if the new backend has a larger bound */
    size_t new_cap = ctx->bk_bound(ctx->bk_userdata, ctx->opts.block_size) + 64;
    if (new_cap > ctx->bk_buf_cap) {
        uint8_t *nb = realloc(ctx->bk_buf, new_cap);
        if (nb) { ctx->bk_buf = nb; ctx->bk_buf_cap = new_cap; }
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_feed
 * ───────────────────────────────────────────────────────────────────────── */

QuadrError quadr_stream_feed(QuadrStreamCtx *ctx,
                              const uint8_t *data, size_t len) {
    if (!ctx || !ctx->is_encode || !data) return QUADR_ERR_NULL;
    size_t bs = ctx->opts.block_size;
    while (len > 0) {
        size_t copy = len < (bs - ctx->in_pos) ? len : (bs - ctx->in_pos);
        memcpy(ctx->in_buf + ctx->in_pos, data, copy);
        ctx->in_pos  += copy;
        data         += copy;
        len          -= copy;
        ctx->bytes_in += copy;
        if (ctx->in_pos == bs) {
            QuadrError e = flush_block(ctx, ctx->in_buf, bs);
            if (e != QUADR_OK) return e;
            ctx->in_pos = 0;
        }
    }
    return QUADR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_encode_close
 *
 * If actual block_count ≤ block_reserved: seek to 0, overwrite header.
 * If actual block_count > block_reserved: the offset_table entries beyond
 *   the reservation are still correct (blocks sit after the reserved area),
 *   but we need to shift block data forward to make room. We do this by
 *   writing to a temp file, then renaming — only triggered for very large
 *   streams, uncommon in practice.
 * ───────────────────────────────────────────────────────────────────────── */

QuadrError quadr_stream_encode_close(QuadrStreamCtx *ctx) {
    if (!ctx || !ctx->is_encode) return QUADR_ERR_NULL;

    /* Flush partial block */
    if (ctx->in_pos > 0) {
        QuadrError e = flush_block(ctx, ctx->in_buf, ctx->in_pos);
        if (e != QUADR_OK) { quadr_stream_close(ctx); return e; }
    }

    size_t real_hdr = quadr_file_header_size(ctx->block_count);
    size_t res_hdr  = quadr_file_header_size(ctx->block_reserved);

    QuadrFileHeader fh = {
        .magic             = QUADR_MAGIC,
        .version           = QUADR_VERSION,
        .total_uncomp_size = ctx->bytes_in,
        .block_count       = ctx->block_count,
        .data_hint         = ctx->opts.data_hint,
        .hash_table        = ctx->hash_table,
        .offset_table      = ctx->offset_table,
    };

    if (real_hdr <= res_hdr) {
        /*
         * Common case: actual block count fits within the reservation.
         * Seek back to 0 and write the real header.  Any unused reservation
         * bytes are zeroed (they were calloc'd) — decoders skip past them
         * using the offset_table anyway.
         *
         * BUT: the offsets stored in offset_table were recorded as absolute
         * file positions *including* the reserved header space, so they are
         * already correct and don't need adjustment.
         */
        uint8_t *hdr_buf = malloc(res_hdr);
        if (!hdr_buf) { fclose(ctx->fp); ctx->fp = NULL; quadr_stream_close(ctx); return QUADR_ERR_OOM; }
        memset(hdr_buf, 0, res_hdr);
        quadr_file_header_write(&fh, hdr_buf, real_hdr);
        if (quadr_fseek64(ctx->fp, 0, SEEK_SET) != 0) { free(hdr_buf); fclose(ctx->fp); ctx->fp = NULL; quadr_stream_close(ctx); return QUADR_ERR_SEEK; }
        if (fwrite(hdr_buf, 1, res_hdr, ctx->fp) != res_hdr) { free(hdr_buf); fclose(ctx->fp); ctx->fp = NULL; quadr_stream_close(ctx); return QUADR_ERR_IO; }
        free(hdr_buf);
        fclose(ctx->fp);
        ctx->fp = NULL;
    } else {
        /*
         * Rare case: more blocks than reserved (stream > STREAM_RESERVE_BLOCKS
         * × block_size bytes). The blocks were written starting at res_hdr,
         * but we now need real_hdr bytes for the header, so we need to shift
         * the block data forward by (real_hdr - res_hdr) bytes.
         *
         * Strategy: copy blocks section to a temp buffer, rewrite entire file.
         */
        int64_t data_start  = (int64_t)res_hdr;
        int64_t data_end    = quadr_ftell64(ctx->fp);
        int64_t data_size   = data_end - data_start;
        int64_t shift       = (int64_t)(real_hdr - res_hdr);

        uint8_t *block_data = malloc((size_t)data_size);
        if (!block_data) { fclose(ctx->fp); ctx->fp = NULL; quadr_stream_close(ctx); return QUADR_ERR_OOM; }

        if (quadr_fseek64(ctx->fp, data_start, SEEK_SET) != 0) { free(block_data); fclose(ctx->fp); ctx->fp = NULL; quadr_stream_close(ctx); return QUADR_ERR_SEEK; }
        if (fread(block_data, 1, (size_t)data_size, ctx->fp) != (size_t)data_size) {
            free(block_data); fclose(ctx->fp); ctx->fp = NULL;
            quadr_stream_close(ctx); return QUADR_ERR_IO;
        }

        /* Adjust all offsets */
        for (uint32_t i = 0; i < ctx->block_count; i++)
            ctx->offset_table[i] += (uint64_t)shift;

        /* Rewrite file from the beginning */
        uint8_t *hdr_buf = malloc(real_hdr);
        if (!hdr_buf) { free(block_data); fclose(ctx->fp); ctx->fp = NULL;
                        quadr_stream_close(ctx); return QUADR_ERR_OOM; }
        quadr_file_header_write(&fh, hdr_buf, real_hdr);

        if (quadr_fseek64(ctx->fp, 0, SEEK_SET) != 0) { free(hdr_buf); free(block_data); fclose(ctx->fp); ctx->fp = NULL;
                        quadr_stream_close(ctx); return QUADR_ERR_SEEK; }
        if (fwrite(hdr_buf, 1, real_hdr,       ctx->fp) != real_hdr) { free(hdr_buf); free(block_data); fclose(ctx->fp); ctx->fp = NULL;
                        quadr_stream_close(ctx); return QUADR_ERR_IO; }
        if (fwrite(block_data, 1, (size_t)data_size, ctx->fp) != (size_t)data_size) { free(hdr_buf); free(block_data); fclose(ctx->fp); ctx->fp = NULL;
                        quadr_stream_close(ctx); return QUADR_ERR_IO; }
        free(hdr_buf);
        free(block_data);
        fclose(ctx->fp);
        ctx->fp = NULL;
    }

    quadr_stream_close(ctx);
    return QUADR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_decode_open
 * ───────────────────────────────────────────────────────────────────────── */

QuadrStreamCtx *quadr_stream_decode_open(const char *in_path,
                                          uint8_t backend_id_hint) {
    (void)backend_id_hint;
    if (!in_path) return NULL;

    FILE *fp = fopen(in_path, "rb");
    if (!fp) return NULL;

    /* Read just enough to get block_count */
    uint8_t mini[18];
    if (fread(mini, 1, 18, fp) != 18) { fclose(fp); return NULL; }
    if (r32le(mini) != QUADR_MAGIC)   { fclose(fp); return NULL; }

    uint32_t block_count = r32le(mini + 13);
    size_t hdr_size = quadr_file_header_size(block_count);
    rewind(fp);

    uint8_t *hdr_buf = malloc(hdr_size);
    if (!hdr_buf) { fclose(fp); return NULL; }
    if (fread(hdr_buf, 1, hdr_size, fp) != hdr_size) { free(hdr_buf); fclose(fp); return NULL; }

    QuadrStreamCtx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) { free(hdr_buf); fclose(fp); return NULL; }

    QuadrError e = quadr_file_header_read(hdr_buf, hdr_size, &ctx->fhdr);
    free(hdr_buf);
    if (e != QUADR_OK) { fclose(fp); free(ctx); return NULL; }

    ctx->fp        = fp;
    ctx->is_encode = 0;

    /* Initialize encode options in decode context to a sensible default.
     * This prevents uninitialized reads of ctx->opts.block_size when the
     * caller calls quadr_stream_set_backend (which queries bk_bound with
     * ctx->opts.block_size). Without initialization this can lead to a
     * huge allocation attempt or other undefined behaviour and cause
     * decode to fail (observed as zero-byte output files).
     */
    quadr_encode_opts_default(&ctx->opts);

    /* Allocate decode buffers */
    size_t bkmax = QUADR_BLOCK_SIZE_MAX * 4;
    ctx->dec_buf = malloc(QUADR_BLOCK_SIZE_MAX + 64);
    ctx->q_buf   = malloc(QUADR_BLOCK_SIZE_MAX + 64);
    ctx->sh_buf  = malloc(QUADR_BLOCK_SIZE_MAX + 64);
    ctx->bk_buf  = malloc(bkmax);
    ctx->bk_buf_cap = bkmax;
    if (!ctx->dec_buf || !ctx->q_buf || !ctx->sh_buf || !ctx->bk_buf) {
        quadr_stream_close(ctx); return NULL;
    }

    /* Set passthrough backend; caller can override */
    const QuadrBackend *pt = quadr_backend_passthrough();
    ctx->bk_compress   = pt->compress;
    ctx->bk_decompress = pt->decompress;
    ctx->bk_bound      = pt->bound;
    ctx->bk_userdata   = pt->userdata;

    /*
     * The file header may have been written with a reservation (zeros after
     * the real data).  The first real block starts at offset_table[0].
     * Seek there directly so we don't try to parse padding bytes as a frame.
     */
    if (ctx->fhdr.block_count > 0) {
        quadr_fseek64(ctx->fp, (int64_t)ctx->fhdr.offset_table[0], SEEK_SET);
    }

    return ctx;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Decode one block from file
 * ───────────────────────────────────────────────────────────────────────── */

static QuadrError decode_next_block(QuadrStreamCtx *ctx) {
    if (ctx->cur_block >= ctx->fhdr.block_count)
        return QUADR_ERR_TRUNC;  /* clean EOF */

    /* Use the offset_table to seek to the exact block position */
    uint64_t blk_off = ctx->fhdr.offset_table[ctx->cur_block];
    quadr_fseek64(ctx->fp, (int64_t)blk_off, SEEK_SET);

    uint8_t fhdr[FRAME_HDR];
    if (fread(fhdr, 1, FRAME_HDR, ctx->fp) != FRAME_HDR) return QUADR_ERR_IO;

    uint8_t  bid   = fhdr[0];
    uint32_t bklen = r32le(fhdr + 1);

    QuadrBlockHeader bh;
    QuadrError e = quadr_block_header_read(fhdr + 5, &bh);
    if (e != QUADR_OK) return e;

    if (bklen > ctx->bk_buf_cap) return QUADR_ERR_BAD_BLOCK;
    if (fread(ctx->bk_buf, 1, bklen, ctx->fp) != bklen) return QUADR_ERR_IO;

    /* Backend decompress.
     * The per-block backend_id is stored in decode context so the
     * app's decompress function can read it from the userdata.          */
    int dr = -1;
    if (bid == 0) {
        /* passthrough: comp_size == bklen */
        const QuadrBackend *pt = quadr_backend_passthrough();
        dr = pt->decompress(pt->userdata, ctx->bk_buf, bklen,
                            ctx->q_buf, bh.comp_size);
    } else {
        const QuadrBackend *bk = quadr_backend_find(bid);
        if (bk) {
            dr = bk->decompress(bk->userdata,
                                ctx->bk_buf, bklen,
                                ctx->q_buf, bh.comp_size);
        } else if (ctx->bk_decompress) {
            /* Fall back to injected backend if registry doesn't have it */
            ctx->backend_id = bid;
            dr = ctx->bk_decompress(ctx->bk_userdata,
                                    ctx->bk_buf, bklen,
                                    ctx->q_buf, bh.comp_size);
        }
    }
    if (dr != 0) return QUADR_ERR_BACKEND;

    /* Quadr inverse transform (use sh_buf as scratch to avoid malloc) */
    e = quadr_block_decode_ex(ctx->q_buf, bh.comp_size,
                              ctx->dec_buf, bh.uncomp_size, &bh, ctx->sh_buf);
    if (e != QUADR_OK) return e;

    /* Hash verify */
    if (quadr_xxh3_64(ctx->dec_buf, bh.uncomp_size)
            != ctx->fhdr.hash_table[ctx->cur_block])
        return QUADR_ERR_HASH_FAIL;

    ctx->dec_len = bh.uncomp_size;
    ctx->dec_pos = 0;
    ctx->cur_block++;
    ctx->bytes_in += FRAME_HDR + bklen;
    return QUADR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_pull
 * ───────────────────────────────────────────────────────────────────────── */

QuadrError quadr_stream_pull(QuadrStreamCtx *ctx,
                              uint8_t *buf, size_t buf_cap,
                              size_t *written) {
    if (!ctx || ctx->is_encode || !buf || !written) return QUADR_ERR_NULL;
    *written = 0;
    if (buf_cap == 0) return QUADR_OK;   /* nothing to fill */
    while (*written < buf_cap) {
        /* Refill from next block when current is exhausted */
        if (ctx->dec_pos >= ctx->dec_len) {
            QuadrError e = decode_next_block(ctx);
            if (e == QUADR_ERR_TRUNC) {
                /* Return whatever was already copied before signalling EOF.
                   Caller checks *written first, then the return code.    */
                return QUADR_ERR_TRUNC;
            }
            if (e != QUADR_OK) return e;
        }
        size_t take = ctx->dec_len - ctx->dec_pos;
        if (take > buf_cap - *written) take = buf_cap - *written;
        memcpy(buf + *written, ctx->dec_buf + ctx->dec_pos, take);
        ctx->dec_pos   += take;
        ctx->bytes_out += take;
        *written       += take;
    }
    return QUADR_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Close / stats
 * ───────────────────────────────────────────────────────────────────────── */

void quadr_stream_close(QuadrStreamCtx *ctx) {
    if (!ctx) return;
    if (ctx->fp) { fclose(ctx->fp); ctx->fp = NULL; }
    free(ctx->in_buf);
    free(ctx->q_buf);
    free(ctx->sh_buf);
    free(ctx->bk_buf);
    free(ctx->dec_buf);
    free(ctx->hash_table);
    free(ctx->offset_table);
    quadr_file_header_free(&ctx->fhdr);
    free(ctx);
}

uint64_t quadr_stream_bytes_in (const QuadrStreamCtx *ctx) { return ctx ? ctx->bytes_in  : 0; }
uint64_t quadr_stream_bytes_out(const QuadrStreamCtx *ctx) { return ctx ? ctx->bytes_out : 0; }

uint8_t quadr_stream_current_bid(const QuadrStreamCtx *ctx) {
    return ctx ? ctx->backend_id : 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_verify  –  Verify every block hash without full decode
 *
 * Reads the file block by block, decompresses with the backend,
 * applies the Quadr inverse transform, and checks XXH3-64 against the
 * hash table.  Like "gzip -t" but for .qdr files.
 *
 * Returns QUADR_OK if all blocks pass, or the first error encountered.
 * `bad_block` (optional): receives the 0-based index of the failing block.
 * ───────────────────────────────────────────────────────────────────────── */

QuadrError quadr_stream_verify(const char *path,
                                uint32_t   *bad_block) {
    if (bad_block) *bad_block = UINT32_MAX;
    if (!path) return QUADR_ERR_NULL;

    /* Reuse the decode path — easiest way to verify is to decode and check */
    QuadrStreamCtx *ctx = quadr_stream_decode_open(path, 0);
    if (!ctx) return QUADR_ERR_IO;

    /* Pull everything; decode_next_block already checks the hash */
    uint8_t *buf = malloc(QUADR_BLOCK_SIZE_DEFAULT);
    if (!buf) { quadr_stream_close(ctx); return QUADR_ERR_OOM; }

    QuadrError result = QUADR_OK;
    uint32_t   blk    = 0;

    for (;;) {
        uint32_t before = ctx->cur_block;
        size_t   got    = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, QUADR_BLOCK_SIZE_DEFAULT, &got);

        /* Track which block we just processed */
        if (ctx->cur_block > before) blk = ctx->cur_block - 1;

        if (e == QUADR_ERR_TRUNC) break;      /* clean EOF — all blocks OK */
        if (e != QUADR_OK) {
            if (bad_block) *bad_block = blk;
            result = e;
            break;
        }
    }

    free(buf);
    quadr_stream_close(ctx);
    return result;
}

/* ─────────────────────────────────────────────────────────────────────────
 * quadr_stream_set_block_backend_fn
 * ───────────────────────────────────────────────────────────────────────── */
void quadr_stream_set_block_backend_fn(QuadrStreamCtx    *ctx,
                                        QuadrBlockBackendFn fn,
                                        void              *userdata) {
    if (!ctx) return;
    ctx->blk_backend_fn = fn;
    ctx->blk_backend_ud = userdata;
}
