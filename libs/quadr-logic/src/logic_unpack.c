#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLUnpackResult ql_unpack(const char *in_path, const char *out_dir,
                         int num_threads, uint32_t specific_file) {
    QLUnpackResult res = {0};
    res.ok = 0;
    res.specific_file = -1;

    double t0 = quadr_now_ms();
    QuadrError e;

    if (specific_file != UINT32_MAX) {
        e = quadr_archive_unpack_file(in_path, specific_file, NULL, out_dir);
        res.specific_file = (int)specific_file;
    } else {
        e = quadr_archive_unpack(in_path, out_dir, num_threads, NULL, NULL);
    }

    if (e != QUADR_OK) {
        snprintf(res.error, sizeof(res.error), "unpack failed: %s", quadr_strerror(e));
        return res;
    }

    double ms = quadr_now_ms() - t0;
    QuadrArchiveInfo *info = quadr_archive_info(in_path);
    if (info) {
        if (specific_file != UINT32_MAX && specific_file < info->file_count) {
            strncpy(res.specific_file_path, info->entries[specific_file].path,
                    sizeof(res.specific_file_path) - 1);
            res.files_extracted = 1;
        } else {
            res.files_extracted = info->file_count;
        }
        quadr_archive_info_free(info);
    }

    res.time_ms = ms;
    res.ok = 1;
    return res;
}
