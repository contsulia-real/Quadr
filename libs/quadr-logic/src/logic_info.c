#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLInfoResult ql_info(const char *path) {
    QLInfoResult res = {0};
    res.ok = 0;

    size_t fl = 0;
    uint8_t *fb = ql_read_file(path, &fl);
    if (!fb) {
        snprintf(res.error, sizeof(res.error), "cannot read file: %s", path);
        return res;
    }

    QuadrFileHeader fh = {0};
    if (quadr_file_header_read(fb, fl, &fh) != QUADR_OK) {
        snprintf(res.error, sizeof(res.error), "not a Quadr file: %s", path);
        free(fb);
        return res;
    }

    res.version = fh.version;
    res.original_size = fh.total_uncomp_size;
    res.on_disk_size = (uint64_t)fl;
    res.block_count = fh.block_count;
    res.data_hint = fh.data_hint;
    res.simd_level = quadr_simd_level_name(quadr_simd_level());

    for (uint32_t i = 0; i < fh.block_count; i++) {
        size_t rp = (size_t)fh.offset_table[i];
        if (rp + 5 + QUADR_BLOCK_HEADER_SIZE > fl) break;
        uint8_t bid = fb[rp];
        if (bid < 6) res.block_stats.backends[bid]++;
        QuadrBlockHeader bh;
        if (quadr_block_header_read(fb + rp + 5, &bh) == QUADR_OK)
            res.block_stats.block_types[bh.type & 3]++;
    }

    quadr_file_header_free(&fh);
    free(fb);
    res.ok = 1;
    return res;
}
