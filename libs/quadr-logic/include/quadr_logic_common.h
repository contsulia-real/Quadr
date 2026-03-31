#ifndef QUADR_LOGIC_COMMON_H
#define QUADR_LOGIC_COMMON_H

#include "quadr.h"
#include "quadr_archive.h"
#include "quadr_platform.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Backend Types ─────────────────────────────────────────────────────── */

typedef enum {
    QL_BACKEND_NONE = 0,
    QL_BACKEND_ZLIB = 1,
    QL_BACKEND_ZSTD = 2,
    QL_BACKEND_LZ4 = 3,
    QL_BACKEND_LZ4HC = 4,
    QL_BACKEND_7Z = 5,
} QLBackend;

/* ─── Encode Configuration ─────────────────────────────────────────────── */

typedef struct {
    QuadrEncodeOpts quadr;
    QLBackend backend;
    int level;
    int use_fast_probe;
    int mixed_backend;
    int parallel;
    int num_threads;
    int auto_configure;       /* 1 = auto-detect file type and choose config */
    int auto_mode;            /* 0=balance, 1=ratio, 2=speed */
} QLEncodeConfig;

/* ─── Result Types ──────────────────────────────────────────────────────── */

typedef struct {
    int ok;
    uint64_t input_size;
    uint64_t output_size;
    double time_ms;
    double speed_gbps;
    double ratio_pct;
    int parallel;
    int num_threads;
    QLBackend backend;
    int level;
    uint32_t block_size;
    const char *simd_level;
    const char *probe_mode;
    char error[256];
} QLEncodeResult;

typedef struct {
    int ok;
    uint64_t output_size;
    double time_ms;
    double speed_gbps;
    int parallel;
    int num_threads;
    char error[256];
} QLDecodeResult;

typedef struct {
    uint32_t block_types[4];
    uint32_t backends[6];
} QLBlockStats;

typedef struct {
    int ok;
    uint32_t version;
    uint64_t original_size;
    uint64_t on_disk_size;
    uint32_t block_count;
    uint8_t data_hint;
    const char *simd_level;
    QLBlockStats block_stats;
    char error[256];
} QLInfoResult;

typedef struct {
    int ok;
    uint64_t file_size;
    const char *simd_level;
    const char *block_type;
    int stride;
    int shuffle;
    double probe_fast_ms;
    double probe_fast_gbps;
    double probe_full_ms;
    double probe_full_gbps;
    double probe_speedup;
    double encode_ms;
    double encode_gbps;
    double decode_ms;
    double decode_gbps;
    char error[256];
} QLBenchResult;

typedef struct {
    int ok;
    uint32_t blocks_verified;
    double time_ms;
    char error[256];
} QLVerifyResult;

typedef struct {
    int ok;
    uint32_t file_count;
    double time_ms;
    char error[256];
} QLVerifyArchiveResult;

typedef struct {
    uint32_t block_index;
    int type;
    int stride;
    int shuffle;
    double score;
    size_t size;
} QLProbeBlockInfo;

typedef struct {
    int ok;
    uint64_t file_size;
    uint32_t block_count;
    uint32_t block_size_kb;
    const char *simd_level;
    const char *probe_mode;
    QLProbeBlockInfo *blocks;
    uint32_t num_blocks;
    uint32_t type_counts[4];
    double total_probe_ms;
    double avg_probe_ms;
    double probe_gbps;
    char error[256];
} QLProbeResult;

typedef struct {
    int ok;
    uint32_t file_count;
    uint64_t total_uncomp_size;
    uint64_t total_comp_size;
    double time_ms;
    double ratio_pct;
    QLBackend backend;
    char error[256];
} QLPackResult;

typedef struct {
    int ok;
    uint32_t files_extracted;
    double time_ms;
    int specific_file;
    char specific_file_path[512];
    char error[256];
} QLUnpackResult;

typedef struct {
    uint64_t orig_size;
    uint64_t comp_size;
    double ratio_pct;
    char path[512];
} QLArchiveEntryInfo;

typedef struct {
    int ok;
    uint32_t file_count;
    uint64_t total_uncomp_size;
    uint64_t total_comp_size;
    double total_ratio_pct;
    QLArchiveEntryInfo *entries;
    uint32_t num_entries;
    char error[256];
} QLListResult;

/* ─── Helpers ───────────────────────────────────────────────────────────── */

const char *ql_backend_name(QLBackend b);
int ql_backend_default_level(QLBackend b);
int ql_effective_level(const QLEncodeConfig *c);
void ql_encode_config_default(QLEncodeConfig *c);

uint8_t *ql_read_file(const char *path, size_t *len);
int ql_write_file(const char *path, const uint8_t *buf, size_t len);
void ql_print_size_human(uint64_t size, char *buf, size_t buf_size);

int ql_parse_hint(const char *s, uint8_t *out);
int ql_parse_backend(const char *s, QLBackend *out);

/* ─── Operations ────────────────────────────────────────────────────────── */

QLEncodeResult ql_encode(const char *in_path, const char *out_path, const QLEncodeConfig *cfg);
QLDecodeResult ql_decode(const char *in_path, const char *out_path, int parallel, int num_threads);
QLInfoResult ql_info(const char *path);
QLBenchResult ql_bench(const char *path);
QLVerifyResult ql_verify(const char *path);
QLVerifyArchiveResult ql_verify_archive(const char *path);
QLProbeResult ql_probe(const char *path, const QLEncodeConfig *cfg);
QLPackResult ql_pack(const char **files, uint32_t file_count,
                     const char *out_path, QLBackend backend,
                     int level, uint32_t block_size, int num_threads,
                     int store_paths, const char *base_dir);
QLUnpackResult ql_unpack(const char *in_path, const char *out_dir,
                         int num_threads, uint32_t specific_file);
QLListResult ql_list(const char *path);

void ql_probe_result_free(QLProbeResult *r);
void ql_list_result_free(QLListResult *r);

/* ─── File Type Detection ─────────────────────────────────────────────── */

typedef enum {
    QL_FILETYPE_UNKNOWN    = 0,
    QL_FILETYPE_TEXT       = 1,
    QL_FILETYPE_BINARY     = 2,
    QL_FILETYPE_IMAGE      = 3,
    QL_FILETYPE_AUDIO      = 4,
    QL_FILETYPE_VIDEO      = 5,
    QL_FILETYPE_COMPRESSED = 6,
} QLFileType;

typedef struct {
    QLFileType  type;
    const char *mime;           /* e.g. "text/html", "image/png" */
    const char *description;    /* human-readable description */
    int         is_text;        /* 1 if text-based */
    int         already_compressed; /* 1 if further compression unlikely */
} QLFileTypeResult;

QLFileTypeResult ql_detect_file_type(const char *path);
QLFileTypeResult ql_detect_file_type_from_data(const uint8_t *data, size_t len);

/* ─── Auto Configuration ──────────────────────────────────────────────── */

typedef enum {
    QL_AUTO_BALANCE = 0,    /* Best balance of ratio and speed */
    QL_AUTO_RATIO = 1,      /* Maximum compression ratio */
    QL_AUTO_SPEED = 2,      /* Maximum speed */
} QLAutoMode;

void ql_auto_configure(QLEncodeConfig *cfg, const char *in_path);
int ql_parse_auto_mode(const char *s, int *out);

#ifdef __cplusplus
}
#endif

#endif /* QUADR_LOGIC_COMMON_H */
