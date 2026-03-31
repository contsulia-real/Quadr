#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long pos = ftell(f);
    fclose(f);
    return (pos >= 0) ? (uint64_t)pos : 0;
}

QLDecodeResult ql_decode(const char *in_path, const char *out_path, int parallel, int num_threads) {
    QLDecodeResult res = {0};
    res.ok = 0;

    if (parallel) {
        double t0 = quadr_now_ms();

        QuadrError e = quadr_decode_file(in_path, out_path, num_threads);
        if (e != QUADR_OK) {
            snprintf(res.error, sizeof(res.error), "decode failed: %s", quadr_strerror(e));
            return res;
        }

        double ms = quadr_now_ms() - t0;
        res.output_size = get_file_size(out_path);
        res.time_ms = ms;
        res.speed_gbps = (ms > 0 && res.output_size) ? (double)res.output_size / 1e9 / (ms * 1e-3) : 0.0;
        res.parallel = 1;
        res.num_threads = num_threads;
        res.ok = 1;
        return res;
    }

    QuadrStreamCtx *ctx = quadr_stream_decode_open(in_path, 0);
    if (!ctx) {
        snprintf(res.error, sizeof(res.error), "not a valid Quadr file: %s", in_path);
        return res;
    }

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        snprintf(res.error, sizeof(res.error), "cannot create '%s'", out_path);
        quadr_stream_close(ctx);
        return res;
    }

    uint8_t *buf = (uint8_t *)malloc(QUADR_BLOCK_SIZE_DEFAULT);
    if (!buf) {
        fclose(fout); quadr_stream_close(ctx);
        snprintf(res.error, sizeof(res.error), "out of memory");
        return res;
    }

    double t0 = quadr_now_ms();
    size_t total = 0;
    int ok = 1;

    for (;;) {
        size_t written = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, QUADR_BLOCK_SIZE_DEFAULT, &written);
        if (written && fwrite(buf, 1, written, fout) != written) {
            snprintf(res.error, sizeof(res.error), "write failed: %s", out_path);
            ok = 0; break;
        }
        total += written;
        if (e == QUADR_ERR_TRUNC) break;
        if (e != QUADR_OK) {
            snprintf(res.error, sizeof(res.error), "decode failed: %s", quadr_strerror(e));
            ok = 0; break;
        }
    }

    free(buf);
    fclose(fout);
    quadr_stream_close(ctx);

    if (!ok) return res;

    double ms = quadr_now_ms() - t0;
    res.output_size = total;
    res.time_ms = ms;
    res.speed_gbps = (ms > 0 && total) ? (double)total / 1e9 / (ms * 1e-3) : 0.0;
    res.parallel = 0;
    res.ok = 1;
    return res;
}
