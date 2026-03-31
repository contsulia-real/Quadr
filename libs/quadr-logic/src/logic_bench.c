#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLBenchResult ql_bench(const char *path) {
    QLBenchResult res = {0};
    res.ok = 0;

    size_t rl = 0;
    uint8_t *raw = ql_read_file(path, &rl);
    if (!raw) {
        snprintf(res.error, sizeof(res.error), "cannot read file: %s", path);
        return res;
    }
    uint8_t *out = (uint8_t *)malloc(rl + 64);
    if (!out) { free(raw); snprintf(res.error, sizeof(res.error), "out of memory"); return res; }

    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);

    res.file_size = rl;
    res.simd_level = quadr_simd_level_name(quadr_simd_level());

    double be = 1e9, bd = 1e9, bp_fast = 1e9, bp_full = 1e9;
    QuadrProbeResult probe_r;

    for (int it = 0; it < 5; it++) {
        size_t ol = rl + 64;
        double t0, t1;

        t0 = quadr_now_ms();
        probe_r = quadr_probe_fast(raw, rl, &opts, NULL);
        t1 = quadr_now_ms();
        double ms = (t1 - t0);
        if (ms < bp_fast) bp_fast = ms;

        t0 = quadr_now_ms();
        quadr_probe(raw, rl, &opts);
        t1 = quadr_now_ms();
        ms = (t1 - t0);
        if (ms < bp_full) bp_full = ms;

        t0 = quadr_now_ms();
        ol = rl + 64;
        quadr_block_encode(raw, rl, out, &ol, &opts, &probe_r, NULL);
        t1 = quadr_now_ms();
        ms = (t1 - t0);
        if (ms < be) { be = ms; }

        uint8_t *dec = (uint8_t *)malloc(rl);
        if (dec) {
            QuadrBlockHeader bh = {
                .uncomp_size = (uint32_t)rl,
                .comp_size = (uint32_t)ol,
                .type = probe_r.type,
                .shuffle_flag = probe_r.shuffle,
                .x_bit = opts.x_bit,
                .stride = probe_r.stride,
                .word_size = probe_r.word_size,
            };
            t0 = quadr_now_ms();
            quadr_block_decode(out, ol, dec, rl, &bh);
            t1 = quadr_now_ms();
            ms = (t1 - t0);
            if (ms < bd) bd = ms;
            free(dec);
        }
    }

    const char *tn[] = {"DELTA","RLE","PASSTHROUGH","XOR"};
    res.block_type = tn[probe_r.type];
    res.stride = probe_r.stride;
    res.shuffle = probe_r.shuffle;
    res.probe_fast_ms = bp_fast;
    res.probe_fast_gbps = rl ? (double)rl/1e9/(bp_fast*1e-3) : 0.0;
    res.probe_full_ms = bp_full;
    res.probe_full_gbps = rl ? (double)rl/1e9/(bp_full*1e-3) : 0.0;
    res.probe_speedup = bp_full > 0 ? bp_full/bp_fast : 1.0;
    res.encode_ms = be;
    res.encode_gbps = rl ? (double)rl/1e9/(be*1e-3) : 0.0;
    res.decode_ms = bd;
    res.decode_gbps = rl ? (double)rl/1e9/(bd*1e-3) : 0.0;

    free(raw); free(out);
    res.ok = 1;
    return res;
}
