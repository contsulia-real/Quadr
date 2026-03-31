#ifndef QUADR_ARCHIVE_H
#define QUADR_ARCHIVE_H

#include "quadr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUADR_ARCHIVE_MAGIC    UINT32_C(0x51415243)  /* "QARC" */
#define QUADR_ARCHIVE_VERSION  0x01
#define QUADR_ARCHIVE_MAX_PATH 260
#define QUADR_ARCHIVE_MAX_FILES 65535

/* ─── Archive Options ─────────────────────────────────────────────────── */

typedef struct {
    uint8_t  backend_id;
    int      backend_level;
    uint32_t block_size;
    int      num_threads;
    int      store_paths;
    const char *base_dir;
} QuadrArchiveOpts;

void quadr_archive_opts_default(QuadrArchiveOpts *opts);

/* ─── Archive Entry Info ──────────────────────────────────────────────── */

typedef struct {
    char     path[QUADR_ARCHIVE_MAX_PATH];
    uint64_t orig_size;
    uint64_t comp_size;
} QuadrArchiveEntry;

/* ─── Archive Info (returned by quadr_archive_info) ───────────────────── */

typedef struct {
    uint32_t          file_count;
    uint64_t          total_uncomp_size;
    uint64_t          total_comp_size;
    QuadrArchiveEntry *entries;
} QuadrArchiveInfo;

QuadrArchiveInfo *quadr_archive_info(const char *path);
void              quadr_archive_info_free(QuadrArchiveInfo *info);

/* ─── Pack ────────────────────────────────────────────────────────────── */

/*
 * Pack multiple files into a .qar archive.
 *
 * file_paths: array of file/directory paths to pack
 * file_count: number of paths in file_paths
 * out_path:   output archive path
 * opts:       archive options
 * progress_fn: optional callback (current, total, path, userdata)
 * progress_ud: userdata for progress callback
 */
typedef void (*QuadrArchiveProgressFn)(uint32_t current, uint32_t total,
                                        const char *path, void *userdata);

QuadrError quadr_archive_pack(const char **file_paths, uint32_t file_count,
                               const char *out_path,
                               const QuadrArchiveOpts *opts,
                               QuadrArchiveProgressFn progress_fn,
                               void *progress_ud);

/* ─── Unpack ──────────────────────────────────────────────────────────── */

/*
 * Unpack all files from a .qar archive.
 *
 * in_path:   archive path
 * out_dir:   output directory (NULL or "" for current directory)
 * num_threads: 0 = auto-detect
 * progress_fn/ud: optional progress callback
 */
QuadrError quadr_archive_unpack(const char *in_path, const char *out_dir,
                                 int num_threads,
                                 QuadrArchiveProgressFn progress_fn,
                                 void *progress_ud);

/*
 * Unpack a single file from the archive by index.
 *
 * in_path:    archive path
 * file_index: 0-based index of the file to extract
 * out_path:   specific output file path (NULL to use archive path + out_dir)
 * out_dir:    output directory (used when out_path is NULL)
 */
QuadrError quadr_archive_unpack_file(const char *in_path, uint32_t file_index,
                                      const char *out_path,
                                      const char *out_dir);

/* ─── Verify ──────────────────────────────────────────────────────────── */

QuadrError quadr_archive_verify(const char *path, uint32_t *bad_entry);

/* ─── Parallel Encode/Decode ──────────────────────────────────────────── */

typedef struct {
    int      num_threads;
    uint8_t  backend_id;
    int      backend_level;
    uint32_t block_size;
} QuadrParallelOpts;

void quadr_parallel_opts_default(QuadrParallelOpts *opts);

/* Encode a single file using the Quadr stream format (optionally parallel). */
QuadrError quadr_encode_file(const char *in_path, const char *out_path,
                              const QuadrParallelOpts *opts);

/* Decode a single Quadr file (optionally parallel). */
QuadrError quadr_decode_file(const char *in_path, const char *out_path,
                              int num_threads);

/* ─── Utilities ───────────────────────────────────────────────────────── */

int  quadr_detect_cpu_cores(void);
uint32_t quadr_select_block_size(const uint8_t *sample, size_t sample_len,
                                  const QuadrEncodeOpts *opts,
                                  uint64_t total_file_size);

#ifdef __cplusplus
}
#endif

#endif /* QUADR_ARCHIVE_H */
