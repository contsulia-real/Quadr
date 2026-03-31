/* quadr_zlib_compat.h
 * Compatibility wrapper that exposes QUADR_COMPRESS2, QUADR_UNCOMPRESS and
 * QUADR_COMPRESSBOUND with a consistent API (uses size_t for lengths).
 * This header hides differences between zlib-ng (which exposes zng_* symbols
 * and uses size_t for the output-length pointer) and system zlib (which
 * uses uLongf for output-length pointers).
 */
#ifndef QUADR_ZLIB_COMPAT_H
#define QUADR_ZLIB_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef QUADR_HAVE_ZLIBNG
/* zlib-ng: include name-mangling header first then public header */
/* zlib-ng's configure sets Z_HAVE_UNISTD_H=1 for MinGW, but unistd.h doesn't
 * exist on Windows. Undefine it before including zlib-ng headers. */
#  undef Z_HAVE_UNISTD_H
#  include <zlib_name_mangling-ng.h>
#  include <zlib-ng.h>
/* zlib-ng: include name-mangling header first then public header */
#  define ZLIBNG_CONST
#  include <zlib_name_mangling-ng.h>
#  include <zlib-ng.h>
/* zlib-ng: include name-mangling header first then public header */
#  include <zlib_name_mangling-ng.h>
#  include <zlib-ng.h>

static inline int quadr_compat_compress2(unsigned char *dest, size_t *destLen,
                                         const unsigned char *src, size_t srcLen,
                                         int level) {
    /* zng_compress2 already uses size_t for destLen */
    return zng_compress2(dest, destLen, src, (uLong)srcLen, level);
}

static inline int quadr_compat_uncompress(unsigned char *dest, size_t *destLen,
                                          const unsigned char *src, size_t srcLen) {
    return zng_uncompress(dest, destLen, src, (uLong)srcLen);
}

static inline size_t quadr_compat_compressBound(size_t n) {
    return zng_compressBound((uLong)n);
}

#else /* system zlib */
#  include <zlib.h>

static inline int quadr_compat_compress2(unsigned char *dest, size_t *destLen,
                                         const unsigned char *src, size_t srcLen,
                                         int level) {
    uLongf ol = (uLongf)*destLen;
    int r = compress2(dest, &ol, src, (uLong)srcLen, level);
    *destLen = (size_t)ol;
    return r;
}

static inline int quadr_compat_uncompress(unsigned char *dest, size_t *destLen,
                                          const unsigned char *src, size_t srcLen) {
    uLongf ol = (uLongf)*destLen;
    int r = uncompress(dest, &ol, src, (uLong)srcLen);
    *destLen = (size_t)ol;
    return r;
}

static inline size_t quadr_compat_compressBound(size_t n) {
    return compressBound((uLong)n);
}

#endif /* QUADR_HAVE_ZLIBNG */

#define QUADR_COMPRESS2 quadr_compat_compress2
#define QUADR_UNCOMPRESS quadr_compat_uncompress
#define QUADR_COMPRESSBOUND quadr_compat_compressBound

#endif /* QUADR_ZLIB_COMPAT_H */

