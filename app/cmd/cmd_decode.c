/* cmd_decode.c - Decode command implementation */

#include "cmd_decode.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_decode(const char *in_path, const char *out_path, int parallel, int num_threads) {
    if (parallel) {
        double t0 = quadr_now_ms();

        QuadrError e = quadr_decode_file(in_path, out_path, num_threads);
        if (e != QUADR_OK) {
            con_error("decode failed: %s", quadr_strerror(e));
            return 1;
        }

        double ms = quadr_now_ms() - t0;
        FILE *fi = fopen(in_path, "rb");
        FILE *fo = fopen(out_path, "rb");
        size_t sz_in = 0, sz_out = 0;
        if (fi) { fseek(fi,0,SEEK_END); sz_in = (size_t)ftell(fi); fclose(fi); }
        if (fo) { fseek(fo,0,SEEK_END); sz_out = (size_t)ftell(fo); fclose(fo); }

        double gbps = (ms > 0 && sz_out) ? (double)sz_out / 1e9 / (ms * 1e-3) : 0.0;

        char sz_b[32];
        print_size_human(sz_out, sz_b, sizeof(sz_b));

        con_ok("decoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET " (parallel)", in_path, out_path);
        con_kv("size", "%s (%.2f GB/s)", sz_b, gbps);
        con_kv("time", "%.0f ms threads=%d", ms,
            num_threads <= 0 ? quadr_detect_cpu_cores() : num_threads);
        (void)sz_in;
        return 0;
    }

    QuadrStreamCtx *ctx = quadr_stream_decode_open(in_path, 0);
    if (!ctx) { con_error("not a valid Quadr file: %s", in_path); return 1; }

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        con_error("cannot create '%s'", out_path);
        quadr_stream_close(ctx); return 1;
    }

    uint8_t *buf = malloc(QUADR_BLOCK_SIZE_DEFAULT);
    if (!buf) { fclose(fout); quadr_stream_close(ctx); return 1; }

    double t0 = quadr_now_ms();
    size_t total = 0;
    int ok = 1;

    for (;;) {
        size_t written = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, QUADR_BLOCK_SIZE_DEFAULT, &written);
        if (written && fwrite(buf, 1, written, fout) != written) {
            con_error("write failed: %s", out_path); ok = 0; break;
        }
        total += written;
        if (e == QUADR_ERR_TRUNC) break;
        if (e != QUADR_OK) {
            con_error("decode failed: %s", quadr_strerror(e));
            ok = 0; break;
        }
    }

    free(buf);
    fclose(fout);
    quadr_stream_close(ctx);

    if (!ok) { return 1; }

    double ms = quadr_now_ms() - t0;
    double gbps = (ms > 0 && total) ? (double)total / 1e9 / (ms * 1e-3) : 0.0;

    char sz_b[32];
    print_size_human(total, sz_b, sizeof(sz_b));

    con_ok("decoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET, in_path, out_path);
    con_kv("size", "%s (%.2f GB/s)", sz_b, gbps);
    con_kv("time", "%.0f ms", ms);
    return 0;
}
