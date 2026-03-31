#ifndef QUADR_H
#define QUADR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Quadr Compression Protocol v1.5
 *
 * Pre-filter layer: transforms numerical data before LZ77 + Huffman/ANS.
 *
 * Standard pipeline:
 *   raw data  ->  [quadr_block_encode]  ->  transformed bytes
 *             ->  [LZ77 + Huffman/ANS]  ->  compressed output
 *
 * Inverse:
 *   compressed  ->  [LZ77 + Huffman/ANS decode]  ->  transformed bytes
 *               ->  [quadr_block_decode]           ->  raw data
 * ───────────────────────────────────────────────────────────────────────────── */

#define QUADR_MAGIC    UINT32_C(0x51554452)   /* "QUDR" */
#define QUADR_VERSION  0x15

#define QUADR_BLOCK_SIZE_DEFAULT  (64u  * 1024u)
#define QUADR_BLOCK_SIZE_MIN      (4u   * 1024u)
#define QUADR_BLOCK_SIZE_MAX      (256u * 1024u)
#define QUADR_MAX_STRIDE          8u

/*
 * Block Header wire layout – 12 bytes:
 *   [0..3]  uncomp_size   uint32 LE
 *   [4..7]  comp_size     uint32 LE
 *   [8]     flags: bits[1:0]=type  bit[2]=shuffle_flag  bits[6:3]=word_size
 *   [9]     x_bit
 *   [10]    stride
 *   [11]    reserved (0)
 */
#define QUADR_BLOCK_HEADER_SIZE  12u

/* ─── Block Type ─────────────────────────────────────────────────────────── */
typedef enum {
    QUADR_BLOCK_DELTA       = 0x00,
    QUADR_BLOCK_RLE         = 0x01,
    QUADR_BLOCK_PASSTHROUGH = 0x02,
    QUADR_BLOCK_XOR         = 0x03,
} QuadrBlockType;

/* ─── Data Hint ──────────────────────────────────────────────────────────── */
typedef enum {
    QUADR_HINT_GENERIC    = 0x00,
    QUADR_HINT_IMAGE      = 0x01,
    QUADR_HINT_AUDIO_PCM  = 0x02,
    QUADR_HINT_SENSOR     = 0x03,
    QUADR_HINT_FLOAT      = 0x04,
    QUADR_HINT_FORCE_AUTO = 0xFF,
    QUADR_HINT_STANDALONE = 0x80,
} QuadrDataHint;

/* ─── Errors ─────────────────────────────────────────────────────────────── */
typedef enum {
    QUADR_OK              =  0,
    QUADR_ERR_NULL        = -1,
    QUADR_ERR_BUF_SMALL   = -2,
    QUADR_ERR_BAD_MAGIC   = -3,
    QUADR_ERR_BAD_VERSION = -4,
    QUADR_ERR_BAD_XBIT    = -5,
    QUADR_ERR_BAD_STRIDE  = -6,
    QUADR_ERR_HASH_FAIL   = -7,
    QUADR_ERR_TRUNC       = -8,
    QUADR_ERR_OOM         = -9,
    QUADR_ERR_BAD_BLOCK   = -10,
    QUADR_ERR_IO          = -11,
    QUADR_ERR_BACKEND     = -12,
    QUADR_ERR_INVALID     = -13,
    QUADR_ERR_NOT_IMPL    = -14,
    QUADR_ERR_SEEK        = -15,
} QuadrError;

const char *quadr_strerror(QuadrError err);

/* ─── Block Header ───────────────────────────────────────────────────────── */
typedef struct {
    uint32_t       uncomp_size;
    uint32_t       comp_size;
    QuadrBlockType type;
    uint8_t        shuffle_flag;
    uint8_t        x_bit;
    uint8_t        stride;
    uint8_t        word_size;
} QuadrBlockHeader;

/* ─── File Header ────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t  magic;
    uint8_t   version;
    uint64_t  total_uncomp_size;
    uint32_t  block_count;
    uint8_t   data_hint;
    uint64_t *hash_table;
    uint64_t *offset_table;
} QuadrFileHeader;

/* ─── Encoder options ────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  data_hint;
    uint32_t block_size;       /* fixed block size; 0 = use adaptive      */
    uint8_t  x_bit;            /* sample width for delta: 8/16/32/64      */
    uint8_t  hint_stride;      /* extra stride candidate, 0 = none        */
    double   delta_threshold;  /* min entropy gain for DELTA, default 0.05*/
    int      adaptive_block;   /* 1 = auto-select block size per segment  */
} QuadrEncodeOpts;



void quadr_encode_opts_default(QuadrEncodeOpts *opts);

/* ─── Probe result ───────────────────────────────────────────────────────── */
typedef struct {
    QuadrBlockType type;
    uint8_t        stride;
    uint8_t        shuffle;
    uint8_t        word_size;
    double         score;
} QuadrProbeResult;

/* Per-block backend selection callback (see quadr_stream_set_block_backend_fn).
 * Return 0 to use the default backend; return a non-zero backend_id to
 * override for this specific block.                                      */
typedef uint8_t (*QuadrBlockBackendFn)(const QuadrProbeResult *probe,
                                       size_t                  block_idx,
                                       void                   *userdata);

/* ═══════════════════════════════════════════════════════════════════════════
 * Core transforms
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Delta encode/decode  (§4.1 path A).
 *
 * SIMD-accelerated for x_bit==8 and stride in {1,2,4,8}:
 *   - AVX2  : 32 bytes/cycle
 *   - SSSE3 : 16 bytes/cycle  (_mm_alignr_epi8)
 *   - SSE2  : 16 bytes/cycle  (shift-and-OR)
 *   - NEON  : 16 bytes/cycle  (vextq_u8)
 * Scalar fallback for all other combinations.
 *
 * in/out may alias only when out == in (true in-place).
 */
QuadrError quadr_delta_encode(const uint8_t *in, size_t in_len,
                              uint8_t *out,
                              uint8_t stride, uint8_t x_bit);

QuadrError quadr_delta_decode(const uint8_t *in, size_t in_len,
                              uint8_t *out,
                              uint8_t stride, uint8_t x_bit);

/*
 * Byte Shuffle / Unshuffle  (§4.1 path B).
 * in_len must be a multiple of word_size.
 * in and out must not overlap.
 */
QuadrError quadr_byte_shuffle  (const uint8_t *in, size_t in_len,
                                uint8_t *out, uint8_t word_size);
QuadrError quadr_byte_unshuffle(const uint8_t *in, size_t in_len,
                                uint8_t *out, uint8_t word_size);

/*
 * XOR encode/decode (§4.1 path C).
 * out[i] = in[i] ^ in[i-stride].  Better than Delta for floats, pointers,
 * and hash-like data where bit patterns change but magnitude doesn't.
 */
QuadrError quadr_xor_encode(const uint8_t *in, size_t in_len,
                            uint8_t *out,
                            uint8_t stride, uint8_t x_bit);
QuadrError quadr_xor_decode(const uint8_t *in, size_t in_len,
                            uint8_t *out,
                            uint8_t stride, uint8_t x_bit);

/* RLE (§4.2). out_len: capacity on entry, bytes written on exit. */
QuadrError quadr_rle_encode(const uint8_t *in, size_t in_len,
                            uint8_t *out, size_t *out_len);
QuadrError quadr_rle_decode(const uint8_t *in, size_t in_len,
                            uint8_t *out, size_t expected_out_len);

/* Probe: choose best block encoding. */
QuadrProbeResult quadr_probe(const uint8_t *data, size_t len,
                             const QuadrEncodeOpts *opts);

/* Block encode: probe + transform. result (out) needed for BlockHeader.
   work_buf: optional scratch buffer (>= in_len bytes) to avoid per-block malloc.
             If NULL, the function allocates internally. */
QuadrError quadr_block_encode(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t *out_len,
                              const QuadrEncodeOpts *opts,
                              QuadrProbeResult *result,
                              uint8_t *work_buf);

/* Block decode: inverse transform per hdr. */
QuadrError quadr_block_decode(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t expected_out_len,
                              const QuadrBlockHeader *hdr);
/* Extended variant accepting an optional work_buf (>= in_len bytes) to avoid malloc. */
QuadrError quadr_block_decode_ex(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t expected_out_len,
                                 const QuadrBlockHeader *hdr,
                                 uint8_t *work_buf);

/* ═══════════════════════════════════════════════════════════════════════════
 * Wire format helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

QuadrError quadr_block_header_write(const QuadrBlockHeader *hdr,
                                    uint8_t buf[QUADR_BLOCK_HEADER_SIZE]);
QuadrError quadr_block_header_read (const uint8_t buf[QUADR_BLOCK_HEADER_SIZE],
                                    QuadrBlockHeader *hdr);

size_t     quadr_file_header_size  (uint32_t block_count);
QuadrError quadr_file_header_write (const QuadrFileHeader *hdr,
                                    uint8_t *buf, size_t buf_len);
QuadrError quadr_file_header_read  (const uint8_t *buf, size_t buf_len,
                                    QuadrFileHeader *hdr);
void       quadr_file_header_free  (QuadrFileHeader *hdr);

/* ═══════════════════════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════════════════════ */

double   quadr_entropy      (const uint8_t *data, size_t len);
double   quadr_entropy_fast (const uint8_t *data, size_t len);
QuadrProbeResult quadr_probe_fast(const uint8_t *data, size_t len, const QuadrEncodeOpts *opts, uint8_t *work_buf);
double   quadr_rle_ratio    (const uint8_t *data, size_t len);
size_t   quadr_max_encoded_size(size_t raw_len);
uint64_t quadr_xxh3_64     (const void *data, size_t len);

/* Runtime SIMD capability (for diagnostics). */
typedef enum {
    QUADR_SIMD_NONE  = 0,
    QUADR_SIMD_SSE2  = 1,
    QUADR_SIMD_SSSE3 = 2,
    QUADR_SIMD_AVX2  = 3,
    QUADR_SIMD_NEON  = 4,
} QuadrSimdLevel;

QuadrSimdLevel quadr_simd_level     (void);
const char    *quadr_simd_level_name(QuadrSimdLevel level);

/* ═══════════════════════════════════════════════════════════════════════════
 * Streaming file API
 *
 * Handles files of arbitrary size by reading/writing in blocks.
 * Never loads the full input into memory simultaneously.
 *
 * Usage (encode):
 *   QuadrStreamCtx *ctx = quadr_stream_encode_open(out_path, opts);
 *   while (have_data) quadr_stream_feed(ctx, buf, len);
 *   quadr_stream_encode_close(ctx);
 *
 * Usage (decode):
 *   QuadrStreamCtx *ctx = quadr_stream_decode_open(in_path);
 *   while (quadr_stream_pull(ctx, buf, sizeof(buf), &written) == QUADR_OK) { ... }
 *   quadr_stream_close(ctx);
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct QuadrStreamCtx QuadrStreamCtx;

/* Backend function pointer types for the streaming API.
 * The application injects its own compress/decompress so the library
 * stays decoupled from zlib/zstd/lz4.                                 */
typedef size_t (*QuadrBkCompress)  (void *ud, int level,
                                    const uint8_t *in,  size_t in_len,
                                    uint8_t       *out, size_t out_cap);
typedef int    (*QuadrBkDecompress)(void *ud,
                                    const uint8_t *in,  size_t in_len,
                                    uint8_t       *out, size_t expected);
typedef size_t (*QuadrBkBound)     (void *ud, size_t in_len);

/* ═══════════════════════════════════════════════════════════════════════════
 * Backend registry
 *
 * Backends are identified by a numeric ID (1-255). ID 0 is reserved for
 * passthrough (no compression).  The registry allows applications to
 * register compression backends once at startup; the stream layer then
 * resolves backend IDs during encode/decode automatically.
 *
 * Usage:
 *   QuadrBackend bk = {
 *       .id = 1, .name = "zstd",
 *       .compress = my_zstd_compress,
 *       .decompress = my_zstd_decompress,
 *       .bound = my_zstd_bound,
 *       .default_level = 3,
 *   };
 *   quadr_backend_register(&bk);
 * ═══════════════════════════════════════════════════════════════════════════ */

#define QUADR_BACKEND_ID_PASSTHROUGH  0
#define QUADR_BACKEND_ID_NONE         0
#define QUADR_BACKEND_ID_MAX          255

typedef struct {
    uint8_t            id;
    const char        *name;
    QuadrBkCompress    compress;
    QuadrBkDecompress  decompress;
    QuadrBkBound       bound;
    void              *userdata;
    int                default_level;
} QuadrBackend;

QuadrError             quadr_backend_register      (const QuadrBackend *backend);
const QuadrBackend    *quadr_backend_find          (uint8_t id);
const QuadrBackend    *quadr_backend_find_by_name  (const char *name);
size_t                 quadr_backend_count         (void);
const QuadrBackend    *quadr_backend_get           (size_t index);
const QuadrBackend    *quadr_backend_passthrough   (void);

/* --- encode --- */
/* total_input_bytes: if >0, the stream will reserve an exact-sized file
 * header based on the expected block count (avoids the default large
 * reservation used for unknown-size streams). Pass 0 to keep legacy
 * behavior. */
QuadrStreamCtx *quadr_stream_encode_open (const char *out_path,
                                          const QuadrEncodeOpts *opts,
                                          uint8_t backend_id,
                                          int     backend_level,
                                          uint64_t total_input_bytes);
/* Per-block backend override: the callback is called once per block to
 * allow the application to choose a different backend for each block.
 * Example use: DELTA blocks → lz4hc (small), PASSTHROUGH → lz4 (fast). */
void            quadr_stream_set_block_backend_fn(QuadrStreamCtx    *ctx,
                                                  QuadrBlockBackendFn fn,
                                                  void              *userdata);

/* Inject a custom backend (call before any quadr_stream_feed calls).   */
void            quadr_stream_set_backend (QuadrStreamCtx    *ctx,
                                          QuadrBkCompress    compress_fn,
                                          QuadrBkDecompress  decompress_fn,
                                          QuadrBkBound       bound_fn,
                                          void              *userdata);
QuadrError      quadr_stream_feed        (QuadrStreamCtx *ctx,
                                          const uint8_t  *data,
                                          size_t          len);
QuadrError      quadr_stream_encode_close(QuadrStreamCtx *ctx);

/* --- decode --- */
QuadrStreamCtx *quadr_stream_decode_open (const char *in_path,
                                          uint8_t backend_id_hint);
/* Pull up to buf_cap decoded bytes.
   Returns QUADR_ERR_TRUNC on clean EOF (normal termination).           */
QuadrError      quadr_stream_pull        (QuadrStreamCtx *ctx,
                                          uint8_t        *buf,
                                          size_t          buf_cap,
                                          size_t         *written);
void            quadr_stream_close       (QuadrStreamCtx *ctx);

/* Progress counters (safe to call at any time).                        */
/* Returns the backend_id of the block currently being decoded.
 * Call from inside a QuadrBkDecompress callback to know which backend
 * to use.  Returns 0 (passthrough) if not in a decode callback.        */
uint8_t         quadr_stream_current_bid (const QuadrStreamCtx *ctx);

uint64_t        quadr_stream_bytes_in    (const QuadrStreamCtx *ctx);
uint64_t        quadr_stream_bytes_out   (const QuadrStreamCtx *ctx);

/*
 * quadr_stream_verify: re-read an existing .qdr file, decode every block,
 * and verify each block's XXH3-64 hash.  Analogous to "gzip -t".
 *
 * Returns QUADR_OK if all blocks pass.
 * On failure, *bad_block (if non-NULL) receives the 0-based block index.
 */
QuadrError      quadr_stream_verify      (const char *path,
                                          uint32_t   *bad_block);

#ifdef __cplusplus
}
#endif
#endif /* QUADR_H */
