#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mixed_backend_fn(const QuadrProbeResult *probe,
                                 size_t block_idx, void *ud) {
    (void)block_idx; (void)ud;
    if (probe->type == QUADR_BLOCK_PASSTHROUGH) {
        return (uint8_t)QL_BACKEND_LZ4;
    }
    return 0;
}

static uint64_t get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long pos = ftell(f);
    fclose(f);
    return (pos >= 0) ? (uint64_t)pos : 0;
}

QLEncodeResult ql_encode(const char *in_path, const char *out_path, const QLEncodeConfig *cfg) {
    QLEncodeResult res = {0};
    res.ok = 0;
    res.input_size = get_file_size(in_path);

    if (cfg->parallel) {
        double t0 = quadr_now_ms();

        QuadrParallelOpts popts;
        quadr_parallel_opts_default(&popts);
        popts.num_threads = cfg->num_threads;
        popts.backend_id = (uint8_t)cfg->backend;
        popts.backend_level = ql_effective_level(cfg);
        popts.block_size = cfg->quadr.block_size;

        QuadrError e = quadr_encode_file(in_path, out_path, &popts);
        if (e != QUADR_OK) {
            snprintf(res.error, sizeof(res.error), "encode failed: %s", quadr_strerror(e));
            return res;
        }

        double ms = quadr_now_ms() - t0;
        res.output_size = get_file_size(out_path);
        res.time_ms = ms;
        res.speed_gbps = (ms > 0 && res.input_size) ? (double)res.input_size / 1e9 / (ms * 1e-3) : 0.0;
        res.ratio_pct = res.input_size ? (double)res.output_size / (double)res.input_size * 100.0 : 0.0;
        res.parallel = 1;
        res.num_threads = cfg->num_threads;
        res.backend = cfg->backend;
        res.level = ql_effective_level(cfg);
        res.block_size = cfg->quadr.block_size;
        res.simd_level = quadr_simd_level_name(quadr_simd_level());
        res.ok = 1;
        return res;
    }

    int lv = ql_effective_level(cfg);
    QuadrEncodeOpts final_opts = cfg->quadr;

    if (cfg->quadr.adaptive_block) {
        FILE *fprobe = fopen(in_path, "rb");
        if (fprobe) {
            uint8_t sample[65536];
            size_t slen = fread(sample, 1, sizeof(sample), fprobe);
            fclose(fprobe);
            if (slen > 0) {
                final_opts.block_size = quadr_select_block_size(
                    sample, slen, &final_opts, res.input_size);
            }
        }
    }

    QuadrStreamCtx *ctx = quadr_stream_encode_open(out_path, &final_opts,
        (uint8_t)cfg->backend, lv, res.input_size);
    if (!ctx) {
        snprintf(res.error, sizeof(res.error), "failed to open output: %s", out_path);
        return res;
    }

    const QuadrBackend *bk = quadr_backend_find((uint8_t)cfg->backend);
    if (bk && bk->id != QUADR_BACKEND_ID_PASSTHROUGH) {
        quadr_stream_set_backend(ctx,
            bk->compress, bk->decompress, bk->bound,
            bk->userdata);
    }
    if (cfg->mixed_backend) {
        quadr_stream_set_block_backend_fn(ctx, mixed_backend_fn, NULL);
    }

    FILE *fin = fopen(in_path, "rb");
    if (!fin) {
        snprintf(res.error, sizeof(res.error), "cannot open '%s'", in_path);
        quadr_stream_encode_close(ctx);
        return res;
    }

    uint32_t bs = final_opts.block_size;
    uint8_t *read_buf = (uint8_t *)malloc(bs);
    if (!read_buf) {
        fclose(fin); quadr_stream_encode_close(ctx);
        snprintf(res.error, sizeof(res.error), "out of memory");
        return res;
    }

    double t0 = quadr_now_ms();
    int ok = 1;

    for (;;) {
        size_t got = fread(read_buf, 1, bs, fin);
        if (got == 0) break;
        QuadrError e = quadr_stream_feed(ctx, read_buf, got);
        if (e != QUADR_OK) {
            snprintf(res.error, sizeof(res.error), "encode failed: %s", quadr_strerror(e));
            ok = 0; break;
        }
    }

    fclose(fin);
    free(read_buf);

    if (!ok) { quadr_stream_encode_close(ctx); return res; }

    QuadrError e = quadr_stream_encode_close(ctx);
    if (e != QUADR_OK) {
        snprintf(res.error, sizeof(res.error), "encode close: %s", quadr_strerror(e));
        return res;
    }

    double ms = quadr_now_ms() - t0;
    res.output_size = get_file_size(out_path);
    res.time_ms = ms;
    res.speed_gbps = (ms > 0 && res.input_size) ? (double)res.input_size / 1e9 / (ms * 1e-3) : 0.0;
    res.ratio_pct = res.input_size ? (double)res.output_size / (double)res.input_size * 100.0 : 0.0;
    res.parallel = 0;
    res.backend = cfg->backend;
    res.level = lv;
    res.block_size = bs;
    res.simd_level = quadr_simd_level_name(quadr_simd_level());
    res.probe_mode = cfg->use_fast_probe ? "fast" : "full";
    res.ok = 1;
    return res;
}
