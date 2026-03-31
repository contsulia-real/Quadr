/* unistd.h stub for MinGW - zlib-ng needs this for SEEK_* constants */
#ifndef _UNISTD_H
#define _UNISTD_H

/* off_t is already defined in sys/types.h via stdio.h on MinGW */

/* We don't support large file features on MinGW the same way as POSIX */
#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE 0
#endif

#endif /* _UNISTD_H */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <io.h>
#include <process.h>

/* SEEK_* constants are in stdio.h, but zlib-ng expects them from unistd.h */
/* They're already available via stdio.h which is included before this */

/* Minimal definitions needed by zlib-ng */
typedef long long off_t;

/* We don't support large file features on MinGW the same way as POSIX */
#ifndef _LFS64_LARGEFILE
#define _LFS64_LARGEFILE 0
#endif

#endif /* _UNISTD_H */
