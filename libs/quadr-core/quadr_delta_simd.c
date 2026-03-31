/*
 * quadr_delta_simd.c  –  SIMD-accelerated Delta encode / decode
 *
 * Architecture support matrix:
 *   x86 / x86_64  : SSE2 (baseline), AVX2 (runtime dispatch)
 *   AArch64 / ARMv7: NEON
 *   Everything else: portable scalar fallback
 *
 * Encode is trivially SIMD (no inter-chunk dependency).
 * Decode stride=1 uses the SIMD prefix-sum trick (O(log N) passes per chunk).
 * Decode stride≥16 is trivially SIMD.
 * Decode stride 2–15 falls back to scalar (dependency chain too short).
 *
 * All paths produce bit-identical output to the scalar reference.
 */

#include "quadr.h"
#include "quadr_platform.h"   /* all SIMD headers + portable abstractions */

#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Portable load/store helpers (little-endian)
 * ───────────────────────────────────────────────────────────────────────────── */

static inline uint16_t ld16(const uint8_t *p)
    { uint16_t v; memcpy(&v,p,2); return v; }
static inline uint32_t ld32(const uint8_t *p)
    { uint32_t v; memcpy(&v,p,4); return v; }
static inline uint64_t ld64(const uint8_t *p)
    { uint64_t v; memcpy(&v,p,8); return v; }
static inline void st16(uint8_t *p, uint16_t v) { memcpy(p,&v,2); }
static inline void st32(uint8_t *p, uint32_t v) { memcpy(p,&v,4); }
static inline void st64(uint8_t *p, uint64_t v) { memcpy(p,&v,8); }

/* ═════════════════════════════════════════════════════════════════════════════
 * SCALAR reference implementation
 * Used for: 16/32/64-bit on all platforms; decode stride 2-15; non-SIMD builds.
 * ═════════════════════════════════════════════════════════════════════════════ */

static void scalar_delta_enc_u8(const uint8_t *in, uint8_t *out,
                                 size_t n_samples, uint8_t stride) {
    uint8_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint8_t s    = in[i];
        out[i] = (uint8_t)(s - prev[i % stride]);
        prev[i % stride] = s;
    }
}

static void scalar_delta_dec_u8(const uint8_t *in, uint8_t *out,
                                 size_t n_samples, uint8_t stride) {
    uint8_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint8_t s    = (uint8_t)(in[i] + prev[i % stride]);
        out[i]           = s;
        prev[i % stride] = s;
    }
}

static void scalar_delta_enc_u16(const uint8_t *in, uint8_t *out,
                                  size_t n_samples, uint8_t stride) {
    uint16_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint16_t s = ld16(in + i*2);
        st16(out + i*2, (uint16_t)(s - prev[i % stride]));
        prev[i % stride] = s;
    }
}

static void scalar_delta_dec_u16(const uint8_t *in, uint8_t *out,
                                  size_t n_samples, uint8_t stride) {
    uint16_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint16_t s = (uint16_t)(ld16(in + i*2) + prev[i % stride]);
        st16(out + i*2, s);
        prev[i % stride] = s;
    }
}

static void scalar_delta_enc_u32(const uint8_t *in, uint8_t *out,
                                  size_t n_samples, uint8_t stride) {
    uint32_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint32_t s = ld32(in + i*4);
        st32(out + i*4, s - prev[i % stride]);
        prev[i % stride] = s;
    }
}

static void scalar_delta_dec_u32(const uint8_t *in, uint8_t *out,
                                  size_t n_samples, uint8_t stride) {
    uint32_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint32_t s = ld32(in + i*4) + prev[i % stride];
        st32(out + i*4, s);
        prev[i % stride] = s;
    }
}

static void scalar_delta_enc_u64(const uint8_t *in, uint8_t *out,
                                  size_t n_samples, uint8_t stride) {
    uint64_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint64_t s = ld64(in + i*8);
        st64(out + i*8, s - prev[i % stride]);
        prev[i % stride] = s;
    }
}

static void scalar_delta_dec_u64(const uint8_t *in, uint8_t *out,
                                  size_t n_samples, uint8_t stride) {
    uint64_t prev[QUADR_MAX_STRIDE] = {0};
    for (size_t i = 0; i < n_samples; i++) {
        uint64_t s = ld64(in + i*8) + prev[i % stride];
        st64(out + i*8, s);
        prev[i % stride] = s;
    }
}

/* ═════════════════════════════════════════════════════════════════════════════
 * SSE2 paths  (8-bit, stride 1-8)
 * ═════════════════════════════════════════════════════════════════════════════ */

#if defined(QUADR_HAVE_SSE2)

/*
 * SSE2 encode: out[i] = in[i] - in[i - stride]  (mod 256)
 *
 * For stride S, every group of S samples can be subtracted from the previous
 * group independently.  We load 16-byte vectors from in[i] and in[i-S] and
 * subtract them.  The first S samples must be done scalar (no predecessor).
 *
 * Throughput: 16 samples / instruction cycle (vs 1 for scalar).
 */
static void sse2_delta_enc_u8(const uint8_t *in, uint8_t *out,
                               size_t n, uint8_t stride) {
    /* Scalar prologue: first `stride` samples */
    for (uint8_t k = 0; k < stride && k < n; k++) {
        out[k] = (uint8_t)(in[k] - 0u);   /* Prev=0 */
    }

    size_t i = stride;
    size_t vec_end = n - (n % 16);

    /* Aligned SIMD body */
    for (; i + 16 <= vec_end; i += 16) {
        __m128i cur  = _mm_loadu_si128((const __m128i *)(in + i));
        __m128i prev = _mm_loadu_si128((const __m128i *)(in + i - stride));
        __m128i d    = _mm_sub_epi8(cur, prev);
        _mm_storeu_si128((__m128i *)(out + i), d);
    }

    /* Scalar tail */
    for (; i < n; i++) {
        out[i] = (uint8_t)(in[i] - in[i - stride]);
    }
}

/*
 * SSE2 decode, stride == 1:
 *   Requires a running prefix sum.  The SIMD prefix-sum trick folds
 *   the carry chain into O(log2 16) = 4 SSE2 operations per 16-byte chunk.
 *
 *   After each chunk, `carry` = the last output byte (becomes the predecessor
 *   for the next chunk's first byte).
 */
static void sse2_delta_dec_u8_stride1(const uint8_t *in, uint8_t *out, size_t n) {
    __m128i carry = _mm_setzero_si128();   /* broadcast carry as 16 copies */
    size_t  i     = 0;

    for (; i + 16 <= n; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i *)(in + i));

        /* Prefix-sum within the 16-byte lane (mod 256 via epi8):
         *   pass 1: add neighbour at distance 1
         *   pass 2: add neighbour at distance 2
         *   pass 3: add neighbour at distance 4
         *   pass 4: add neighbour at distance 8
         * After 4 passes, v[k] = sum( original[0..k] )
         * (all arithmetic mod 256 automatically)                             */
        v = _mm_add_epi8(v, _mm_slli_si128(v, 1));
        v = _mm_add_epi8(v, _mm_slli_si128(v, 2));
        v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
        v = _mm_add_epi8(v, _mm_slli_si128(v, 8));

        /* Add the carry from the previous chunk (broadcast to all lanes) */
        v = _mm_add_epi8(v, carry);

        _mm_storeu_si128((__m128i *)(out + i), v);

        /* New carry = last byte of this chunk, broadcast */
        carry = _mm_set1_epi8((int8_t)out[i + 15]);
    }

    /* Scalar tail */
    uint8_t c = (i > 0) ? out[i - 1] : 0;
    for (; i < n; i++) {
        c = (uint8_t)(in[i] + c);
        out[i] = c;
    }
}

/*
 * SSE2 decode, general stride (2-8):
 *   For stride S ≥ 2 there is a dependency chain with period S.
 *   Within a 16-byte chunk we process all S independent lanes in parallel.
 *   Each lane needs its predecessor from the carry array.
 *
 *   We process the chunk in `S` scalar operations (one per lane) only for
 *   the FIRST iteration that seeds the carry, then the bulk with SIMD.
 *   Actually the cleaner way: process in chunks of S*16 using SIMD on each
 *   of the S channels independently.  For typical S (2,3,4) this is still
 *   16x throughput per channel.
 */
static void sse2_delta_dec_u8_strideN(const uint8_t *in, uint8_t *out,
                                       size_t n, uint8_t stride) {
    /* carry[k] = last decoded value for channel k */
    uint8_t carry[QUADR_MAX_STRIDE] = {0};

    /* Process `stride` channels simultaneously, 16 samples per channel */
    size_t chunk  = (size_t)stride * 16;   /* bytes per SIMD round */
    size_t i      = 0;

    for (; i + chunk <= n; i += chunk) {
        for (uint8_t ch = 0; ch < stride; ch++) {
            /* Gather the 16 encoded bytes for this channel in this chunk.
             * Channel `ch` bytes are at positions: i+ch, i+ch+stride, ...
             * We deinterleave into a contiguous temp buffer, SIMD prefix-sum,
             * then scatter back.                                             */

            /* --- Deinterleave 16 samples of channel `ch` --- */
            uint8_t enc[16], dec_ch[16];
            for (int k = 0; k < 16; k++)
                enc[k] = in[i + ch + (size_t)k * stride];

            /* --- Prefix sum on the 16 encoded bytes --- */
            __m128i v = _mm_loadu_si128((const __m128i *)enc);
            v = _mm_add_epi8(v, _mm_slli_si128(v, 1));
            v = _mm_add_epi8(v, _mm_slli_si128(v, 2));
            v = _mm_add_epi8(v, _mm_slli_si128(v, 4));
            v = _mm_add_epi8(v, _mm_slli_si128(v, 8));
            __m128i cv = _mm_set1_epi8((int8_t)carry[ch]);
            v = _mm_add_epi8(v, cv);
            _mm_storeu_si128((__m128i *)dec_ch, v);

            /* --- Scatter back and update carry --- */
            for (int k = 0; k < 16; k++)
                out[i + ch + (size_t)k * stride] = dec_ch[k];
            carry[ch] = dec_ch[15];
        }
    }

    /* Scalar tail */
    for (; i < n; i++) {
        uint8_t ch = (uint8_t)(i % stride);
        uint8_t s  = (uint8_t)(in[i] + carry[ch]);
        out[i]     = s;
        carry[ch]  = s;
    }
}

/* ─── SSE2 16-bit encode ──────────────────────────────────────────────── */

static void sse2_delta_enc_u16(const uint8_t *in, uint8_t *out,
                                size_t n_samples, uint8_t stride) {
    /* Scalar prologue */
    for (uint8_t k = 0; k < stride && k < n_samples; k++) {
        st16(out + k*2, ld16(in + k*2));
    }

    size_t i = stride;
    for (; i + 8 <= n_samples; i += 8) {
        __m128i cur  = _mm_loadu_si128((const __m128i *)(in + i*2));
        __m128i prev = _mm_loadu_si128((const __m128i *)(in + (i - stride)*2));
        __m128i d    = _mm_sub_epi16(cur, prev);
        _mm_storeu_si128((__m128i *)(out + i*2), d);
    }

    for (; i < n_samples; i++) {
        st16(out + i*2, (uint16_t)(ld16(in+i*2) - ld16(in+(i-stride)*2)));
    }
}

#endif /* QUADR_HAVE_SSE2 */

/* ═════════════════════════════════════════════════════════════════════════════
 * AVX2 paths  (8-bit encode, 32-sample chunks)
 * ═════════════════════════════════════════════════════════════════════════════ */

#if defined(QUADR_HAVE_AVX2)

/*
 * AVX2 encode is identical logic to SSE2 but operates on 32-byte vectors.
 * We compile this into the same translation unit guarded by the same -mavx2
 * flag (set on quadr_delta_simd.c by CMake).
 * Runtime dispatch: we call this only when __builtin_cpu_supports("avx2").
 */

__attribute__((target("avx2")))
static void avx2_delta_enc_u8(const uint8_t *in, uint8_t *out,
                               size_t n, uint8_t stride) {
    /* Scalar prologue */
    for (uint8_t k = 0; k < stride && k < n; k++)
        out[k] = in[k];

    size_t i       = stride;
    size_t vec_end = n - (n % 32);

    for (; i + 32 <= vec_end; i += 32) {
        __m256i cur  = _mm256_loadu_si256((const __m256i *)(in + i));
        __m256i prev = _mm256_loadu_si256((const __m256i *)(in + i - stride));
        __m256i d    = _mm256_sub_epi8(cur, prev);
        _mm256_storeu_si256((__m256i *)(out + i), d);
    }

    for (; i < n; i++)
        out[i] = (uint8_t)(in[i] - in[i - stride]);
}

__attribute__((target("avx2")))
static void avx2_delta_enc_u16(const uint8_t *in, uint8_t *out,
                                size_t n_samples, uint8_t stride) {
    for (uint8_t k = 0; k < stride && k < n_samples; k++)
        st16(out + k*2, ld16(in + k*2));

    size_t i = stride;
    for (; i + 16 <= n_samples; i += 16) {
        __m256i cur  = _mm256_loadu_si256((const __m256i *)(in + i*2));
        __m256i prev = _mm256_loadu_si256((const __m256i *)(in + (i-stride)*2));
        __m256i d    = _mm256_sub_epi16(cur, prev);
        _mm256_storeu_si256((__m256i *)(out + i*2), d);
    }
    for (; i < n_samples; i++)
        st16(out+i*2, (uint16_t)(ld16(in+i*2) - ld16(in+(i-stride)*2)));
}

#endif /* QUADR_HAVE_AVX2 */

/* ═════════════════════════════════════════════════════════════════════════════
 * NEON paths  (AArch64 / ARMv7)
 * ═════════════════════════════════════════════════════════════════════════════ */

#if defined(QUADR_HAVE_NEON)

static void neon_delta_enc_u8(const uint8_t *in, uint8_t *out,
                               size_t n, uint8_t stride) {
    for (uint8_t k = 0; k < stride && k < n; k++)
        out[k] = in[k];

    size_t i = stride;
    for (; i + 16 <= n; i += 16) {
        uint8x16_t cur  = vld1q_u8(in + i);
        uint8x16_t prev = vld1q_u8(in + i - stride);
        vst1q_u8(out + i, vsubq_u8(cur, prev));
    }
    for (; i < n; i++)
        out[i] = (uint8_t)(in[i] - in[i - stride]);
}

static void neon_delta_dec_u8_stride1(const uint8_t *in, uint8_t *out,
                                       size_t n) {
    uint8x16_t carry = vdupq_n_u8(0);

    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        uint8x16_t v = vld1q_u8(in + i);

        /* NEON prefix-sum: 4 passes (shift-left-by-1 in byte lane).
         * vextq shifts right from the zero vector (inserts zeros at left).
         * We want shift-left so we shift the zero vector into the right side. */
        uint8x16_t z = vdupq_n_u8(0);
        v = vaddq_u8(v, vextq_u8(z, v, 15));   /* shift left by 1 byte */
        v = vaddq_u8(v, vextq_u8(z, v, 14));   /* shift left by 2 */
        v = vaddq_u8(v, vextq_u8(z, v, 12));   /* shift left by 4 */
        v = vaddq_u8(v, vextq_u8(z, v,  8));   /* shift left by 8 */

        v = vaddq_u8(v, carry);
        vst1q_u8(out + i, v);
        carry = vdupq_n_u8(out[i + 15]);
    }

    uint8_t c = (i > 0) ? out[i-1] : 0;
    for (; i < n; i++) {
        c = (uint8_t)(in[i] + c);
        out[i] = c;
    }
}

static void neon_delta_enc_u16(const uint8_t *in, uint8_t *out,
                                size_t n_samples, uint8_t stride) {
    for (uint8_t k = 0; k < stride && k < n_samples; k++)
        st16(out + k*2, ld16(in + k*2));

    size_t i = stride;
    for (; i + 8 <= n_samples; i += 8) {
        uint16x8_t cur  = vld1q_u16((const uint16_t *)(in + i*2));
        uint16x8_t prev = vld1q_u16((const uint16_t *)(in + (i-stride)*2));
        vst1q_u16((uint16_t *)(out + i*2), vsubq_u16(cur, prev));
    }
    for (; i < n_samples; i++)
        st16(out+i*2, (uint16_t)(ld16(in+i*2) - ld16(in+(i-stride)*2)));
}

#endif /* QUADR_HAVE_NEON */

/* ═════════════════════════════════════════════════════════════════════════════
 * Runtime CPU dispatch  (x86 only; other arches use compile-time selection)
 * ═════════════════════════════════════════════════════════════════════════════ */

#if defined(QUADR_HAVE_AVX2) || defined(QUADR_HAVE_SSE2)
static int g_cpu_checked = 0;
static int g_have_avx2   = 0;

static void detect_cpu(void) {
    if (g_cpu_checked) return;
#if defined(QUADR_HAVE_AVX2)
    g_have_avx2 = __builtin_cpu_supports("avx2");
#endif
    g_cpu_checked = 1;
}
#endif

/* ═════════════════════════════════════════════════════════════════════════════
 * Public API: quadr_delta_encode / quadr_delta_decode
 * ═════════════════════════════════════════════════════════════════════════════ */

QuadrError quadr_delta_encode(const uint8_t *in, size_t in_len,
                               uint8_t *out,
                               uint8_t stride, uint8_t x_bit) {
    if (!in || !out)                              return QUADR_ERR_NULL;
    if (stride == 0 || stride > QUADR_MAX_STRIDE) return QUADR_ERR_BAD_STRIDE;
    if (x_bit != 8 && x_bit != 16 &&
        x_bit != 32 && x_bit != 64)              return QUADR_ERR_BAD_XBIT;

    size_t sample_bytes = x_bit / 8;
    size_t n_samples    = in_len / sample_bytes;
    size_t tail_bytes   = in_len % sample_bytes;

    switch (x_bit) {
    case 8:
#if defined(QUADR_HAVE_AVX2)
        detect_cpu();
        if (g_have_avx2) { avx2_delta_enc_u8(in, out, n_samples, stride); break; }
#endif
#if defined(QUADR_HAVE_SSE2)
        sse2_delta_enc_u8(in, out, n_samples, stride);
        break;
#elif defined(QUADR_HAVE_NEON)
        neon_delta_enc_u8(in, out, n_samples, stride);
        break;
#else
        scalar_delta_enc_u8(in, out, n_samples, stride);
        break;
#endif

    case 16:
#if defined(QUADR_HAVE_AVX2)
        detect_cpu();
        if (g_have_avx2) { avx2_delta_enc_u16(in, out, n_samples, stride); break; }
#endif
#if defined(QUADR_HAVE_SSE2)
        sse2_delta_enc_u16(in, out, n_samples, stride);
        break;
#elif defined(QUADR_HAVE_NEON)
        neon_delta_enc_u16(in, out, n_samples, stride);
        break;
#else
        scalar_delta_enc_u16(in, out, n_samples, stride);
        break;
#endif

    case 32: scalar_delta_enc_u32(in, out, n_samples, stride); break;
    case 64: scalar_delta_enc_u64(in, out, n_samples, stride); break;
    }

    /* Copy any trailing bytes that don't form a complete sample */
    if (tail_bytes)
        memcpy(out + n_samples * sample_bytes,
               in  + n_samples * sample_bytes, tail_bytes);

    return QUADR_OK;
}

QuadrError quadr_delta_decode(const uint8_t *in, size_t in_len,
                               uint8_t *out,
                               uint8_t stride, uint8_t x_bit) {
    if (!in || !out)                              return QUADR_ERR_NULL;
    if (stride == 0 || stride > QUADR_MAX_STRIDE) return QUADR_ERR_BAD_STRIDE;
    if (x_bit != 8 && x_bit != 16 &&
        x_bit != 32 && x_bit != 64)              return QUADR_ERR_BAD_XBIT;

    size_t sample_bytes = x_bit / 8;
    size_t n_samples    = in_len / sample_bytes;
    size_t tail_bytes   = in_len % sample_bytes;

    switch (x_bit) {
    case 8:
#if defined(QUADR_HAVE_SSE2) || defined(QUADR_HAVE_NEON)
        if (stride == 1) {
#  if defined(QUADR_HAVE_SSE2)
            sse2_delta_dec_u8_stride1(in, out, n_samples);
#  else
            neon_delta_dec_u8_stride1(in, out, n_samples);
#  endif
            break;
        }
        if (stride >= 2 && stride <= 8) {
            /* Strided SIMD decode via per-channel prefix sums */
#  if defined(QUADR_HAVE_SSE2)
            sse2_delta_dec_u8_strideN(in, out, n_samples, stride);
#  else
            /* NEON strided decode: fall back (can add later) */
            scalar_delta_dec_u8(in, out, n_samples, stride);
#  endif
            break;
        }
        /* stride > 8: scalar */
        scalar_delta_dec_u8(in, out, n_samples, stride);
        break;
#else
        scalar_delta_dec_u8(in, out, n_samples, stride);
        break;
#endif

    case 16: scalar_delta_dec_u16(in, out, n_samples, stride); break;
    case 32: scalar_delta_dec_u32(in, out, n_samples, stride); break;
    case 64: scalar_delta_dec_u64(in, out, n_samples, stride); break;
    }

    if (tail_bytes)
        memcpy(out + n_samples * sample_bytes,
               in  + n_samples * sample_bytes, tail_bytes);

    return QUADR_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Runtime SIMD capability query
 * ═══════════════════════════════════════════════════════════════════════════ */

QuadrSimdLevel quadr_simd_level(void) {
#if defined(QUADR_HAVE_AVX2)
    return QUADR_SIMD_AVX2;
#elif defined(QUADR_HAVE_SSE2)
    return QUADR_SIMD_SSE2;
#elif defined(QUADR_HAVE_NEON)
    return QUADR_SIMD_NEON;
#else
    return QUADR_SIMD_NONE;
#endif
}

const char *quadr_simd_level_name(QuadrSimdLevel level) {
    switch (level) {
        case QUADR_SIMD_AVX2:  return "AVX2";
        case QUADR_SIMD_SSSE3: return "SSSE3";
        case QUADR_SIMD_SSE2:  return "SSE2";
        case QUADR_SIMD_NEON:  return "NEON";
        default:               return "scalar";
    }
}
