/* cmd_bench.c - Bench command implementation */

#include "cmd_bench.h"
#include "cmd_common.h"
#include "quadr_version.h"

int cmd_bench(const char *path) {
    size_t rl = 0;
    uint8_t *raw = read_file(path, &rl);
    if (!raw) return 1;
    uint8_t *out = malloc(rl + 64);
    if (!out) { free(raw); return 1; }

    QuadrEncodeOpts opts;
    quadr_encode_opts_default(&opts);

    con_section("Benchmark");
    con_kv("file", "%s", path);
    con_kv("size", "%zu bytes", rl);
    con_kv("simd", "%s", quadr_simd_level_name(quadr_simd_level()));

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

        uint8_t *dec = malloc(rl);
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
    con_nl();
    con_kv("block_type", "%s (stride=%d shuffle=%d)", tn[probe_r.type], probe_r.stride, probe_r.shuffle);
    con_kv("probe_fast", "%.3f ms %.2f GB/s",
        bp_fast, rl ? (double)rl/1e9/(bp_fast*1e-3) : 0.0);
    con_kv("probe_full", "%.3f ms %.2f GB/s (fast %.1fx)",
        bp_full, rl ? (double)rl/1e9/(bp_full*1e-3) : 0.0,
        bp_full > 0 ? bp_full/bp_fast : 1.0);
    con_kv("encode", "%.3f ms %.2f GB/s",
        be, rl ? (double)rl/1e9/(be*1e-3) : 0.0);
    con_kv("decode", "%.3f ms %.2f GB/s",
        bd, rl ? (double)rl/1e9/(bd*1e-3) : 0.0);

    free(raw); free(out);
    return 0;
}
