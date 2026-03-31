#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLListResult ql_list(const char *path) {
    QLListResult res = {0};
    res.ok = 0;

    QuadrArchiveInfo *info = quadr_archive_info(path);
    if (!info) {
        snprintf(res.error, sizeof(res.error), "not a valid Quadr archive: %s", path);
        return res;
    }

    res.file_count = info->file_count;
    res.total_uncomp_size = info->total_uncomp_size;
    res.total_comp_size = info->total_comp_size;
    res.total_ratio_pct = info->total_uncomp_size > 0 ?
        (double)info->total_comp_size / (double)info->total_uncomp_size * 100.0 : 0.0;

    if (info->file_count > 0) {
        res.entries = (QLArchiveEntryInfo *)calloc(info->file_count, sizeof(QLArchiveEntryInfo));
        if (!res.entries) {
            quadr_archive_info_free(info);
            snprintf(res.error, sizeof(res.error), "out of memory");
            return res;
        }
        res.num_entries = info->file_count;

        for (uint32_t i = 0; i < info->file_count; i++) {
            const QuadrArchiveEntry *e = &info->entries[i];
            res.entries[i].orig_size = e->orig_size;
            res.entries[i].comp_size = e->comp_size;
            res.entries[i].ratio_pct = e->orig_size > 0 ?
                (double)e->comp_size / (double)e->orig_size * 100.0 : 0.0;
            strncpy(res.entries[i].path, e->path, sizeof(res.entries[i].path) - 1);
        }
    }

    quadr_archive_info_free(info);
    res.ok = 1;
    return res;
}
