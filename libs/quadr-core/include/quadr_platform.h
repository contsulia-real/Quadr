#ifndef QUADR_PLATFORM_H
#define QUADR_PLATFORM_H

/*
 * quadr_platform.h  –  Portable OS/compiler abstractions
 *
 * Targets: Windows (MinGW GCC/Clang), Linux (GCC, Clang)
 * Compiler: GCC or Clang only.
 *
 * Rules applied throughout the project:
 *   - No POSIX-only APIs on Windows
 *   - No GCC-specific builtins that Clang doesn't share
 *   - No Linux-only headers (<unistd.h> guarded, etc.)
 *   - SIMD intrinsic headers are standard across GCC/Clang on x86
 */

#include <stdint.h>
#include <stddef.h>

/* ─── Compiler detection ─────────────────────────────────────────────────── */

/* Project targets MinGW/GCC; do not include Clang-specific branches. */
#if defined(__GNUC__)
#  define QUADR_COMPILER_GCC    1
#endif

/* ─── OS detection ───────────────────────────────────────────────────────── */

#if defined(_WIN32) || defined(_WIN64)
#  define QUADR_OS_WINDOWS 1
#elif defined(__APPLE__)
#  define QUADR_OS_MACOS   1
#else
#  define QUADR_OS_LINUX   1
#endif

/* ─── inline / force-inline ──────────────────────────────────────────────── */

#define QUADR_INLINE        static inline
#define QUADR_FORCE_INLINE  static inline __attribute__((always_inline))

/* ─── restrict ───────────────────────────────────────────────────────────── */

#define QUADR_RESTRICT __restrict__

/* ─── Byte-swap ──────────────────────────────────────────────────────────── */

#define QUADR_BSWAP32(x) __builtin_bswap32(x)
#define QUADR_BSWAP64(x) __builtin_bswap64(x)

/* ─── Endianness ─────────────────────────────────────────────────────────── */
/* Quadr wire format is always little-endian. On LE hosts, loads are direct. */

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define QUADR_BIG_ENDIAN 1
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * High-resolution timer
 *
 * quadr_now_ms()  →  double milliseconds (monotonic, arbitrary epoch)
 * ───────────────────────────────────────────────────────────────────────── */

#if defined(QUADR_OS_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

QUADR_INLINE double quadr_now_ms(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1000.0 / (double)freq.QuadPart;
}

#elif defined(QUADR_OS_MACOS)

#include <mach/mach_time.h>

QUADR_INLINE double quadr_now_ms(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    uint64_t t = mach_absolute_time();
    return (double)t * tb.numer / tb.denom * 1e-6;
}

#else  /* Linux and other POSIX */

#include <time.h>

QUADR_INLINE double quadr_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

#endif /* timer */

/* ─────────────────────────────────────────────────────────────────────────
 * SIMD capability macros
 *
 * These are set by CMake (via target_compile_definitions) based on what
 * the current toolchain supports.  The headers themselves are portable
 * across GCC and Clang on x86.
 * ───────────────────────────────────────────────────────────────────────── */

#if defined(QUADR_HAVE_AVX2)
#  include <immintrin.h>   /* AVX2 + SSE4.2 + SSSE3 + SSE2 */
#elif defined(QUADR_HAVE_SSSE3)
#  include <tmmintrin.h>   /* SSSE3 + SSE2 */
#elif defined(QUADR_HAVE_SSE2)
#  include <emmintrin.h>   /* SSE2 */
#endif

#if defined(QUADR_HAVE_NEON)
#  include <arm_neon.h>
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * 128-bit integer multiply (for XXH3)
 *
 * Both GCC and Clang support __uint128_t natively.
 * ───────────────────────────────────────────────────────────────────────── */

QUADR_INLINE uint64_t quadr_mul128_high(uint64_t a, uint64_t b) {
    return (uint64_t)((__uint128_t)a * b >> 64);
}

QUADR_INLINE uint64_t quadr_mul128_low(uint64_t a, uint64_t b) {
    return (uint64_t)((__uint128_t)a * b);
}

#endif /* QUADR_PLATFORM_H */
