#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLPackResult ql_pack(const char **files, uint32_t file_count,
                     const char *out_path, QLBackend backend,
                     int level, uint32_t block_size, int num_threads,
                     int store_paths, const char *base_dir) {
    QLPackResult res = {0};
    res.ok = 0;

    if (file_count > QUADR_ARCHIVE_MAX_FILES) {
        snprintf(res.error, sizeof(res.error), "too many files (max %u)", QUADR_ARCHIVE_MAX_FILES);
        return res;
    }

    QuadrArchiveOpts aopts;
    quadr_archive_opts_default(&aopts);
    aopts.backend_id = (uint8_t)backend;
    aopts.backend_level = (level >= 0) ? level : ql_backend_default_level(backend);
    aopts.block_size = block_size;
    aopts.num_threads = num_threads;
    aopts.store_paths = store_paths;
    aopts.base_dir = base_dir;

    double t0 = quadr_now_ms();

    QuadrError e = quadr_archive_pack(files, file_count, out_path, &aopts, NULL, NULL);
    if (e != QUADR_OK) {
        snprintf(res.error, sizeof(res.error), "pack failed: %s", quadr_strerror(e));
        return res;
    }

    double ms = quadr_now_ms() - t0;

    QuadrArchiveInfo *info = quadr_archive_info(out_path);
    if (info) {
        res.file_count = info->file_count;
        res.total_uncomp_size = info->total_uncomp_size;
        res.total_comp_size = info->total_comp_size;
        res.ratio_pct = info->total_uncomp_size ?
            (double)info->total_comp_size / (double)info->total_uncomp_size * 100.0 : 0.0;
        quadr_archive_info_free(info);
    }

    res.time_ms = ms;
    res.backend = backend;
    res.ok = 1;
    return res;
}
