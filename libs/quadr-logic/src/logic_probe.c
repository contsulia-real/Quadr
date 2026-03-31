#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLProbeResult ql_probe(const char *path, const QLEncodeConfig *cfg) {
    QLProbeResult res = {0};
    res.ok = 0;

    size_t raw_len = 0;
    uint8_t *raw = ql_read_file(path, &raw_len);
    if (!raw) {
        snprintf(res.error, sizeof(res.error), "cannot read file: %s", path);
        return res;
    }

    uint32_t bs = cfg->quadr.block_size;
    uint32_t bc = raw_len ? (uint32_t)((raw_len + bs - 1) / bs) : 0;

    res.file_size = raw_len;
    res.block_count = bc;
    res.block_size_kb = bs / 1024;
    res.simd_level = quadr_simd_level_name(quadr_simd_level());
    res.probe_mode = cfg->use_fast_probe ? "fast" : "full";

    if (bc > 0) {
        res.blocks = (QLProbeBlockInfo *)calloc(bc, sizeof(QLProbeBlockInfo));
        if (!res.blocks) {
            free(raw);
            snprintf(res.error, sizeof(res.error), "out of memory");
            return res;
        }
    }
    res.num_blocks = bc;

    double t_probe = 0.0;

    for (uint32_t bi = 0; bi < bc; bi++) {
        size_t off = (size_t)bi * bs;
        size_t blen = (off + bs <= raw_len) ? bs : raw_len - off;
        const uint8_t *blk = raw + off;

        double t0 = quadr_now_ms();
        QuadrProbeResult r = cfg->use_fast_probe
            ? quadr_probe_fast(blk, blen, &cfg->quadr, NULL)
            : quadr_probe(blk, blen, &cfg->quadr);
        double t1 = quadr_now_ms();
        t_probe += (t1 - t0);

        res.type_counts[r.type]++;
        res.blocks[bi].block_index = bi;
        res.blocks[bi].type = r.type;
        res.blocks[bi].stride = r.stride;
        res.blocks[bi].shuffle = r.shuffle;
        res.blocks[bi].score = r.score;
        res.blocks[bi].size = blen;
    }

    res.total_probe_ms = t_probe;
    res.avg_probe_ms = bc ? t_probe / bc : 0.0;
    res.probe_gbps = (raw_len && t_probe > 0) ? (double)raw_len / 1e9 / (t_probe * 1e-3) : 0.0;

    free(raw);
    res.ok = 1;
    return res;
}
