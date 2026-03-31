/* cmd_encode.c - Encode command implementation */

#include "cmd_encode.h"
#include "cmd_common.h"
#include "quadr_version.h"

/* Mixed backend callback: PASSTHROUGH blocks use lz4 (faster) */
static uint8_t mixed_backend_fn(const QuadrProbeResult *probe,
                                 size_t block_idx, void *ud) {
    (void)block_idx; (void)ud;
    if (probe->type == QUADR_BLOCK_PASSTHROUGH) {
#ifdef QUADR_HAVE_LZ4
        return (uint8_t)BACKEND_LZ4;
#endif
    }
    return 0;
}

int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg) {
    (void)argc;
    char *arg = argv[*i];
    char *eq = strchr(arg, '=');
    const char *val = eq ? eq+1 : "";

    if (!strncmp(arg,"--hint=",7)) { if (parse_hint(val, &cfg->quadr.data_hint) != 0) { con_error("invalid hint '%s'", val); return -1; } return 0; }
    else if (!strncmp(arg,"--block=",8)) { if (val[0]=='\0') { con_error("--block= requires a value"); return -1; } cfg->quadr.block_size=(uint32_t)(atoi(val)*1024); return 0; }
    else if (!strncmp(arg,"--xbit=",7)) { if (val[0]=='\0') { con_error("--xbit= requires a value"); return -1; } cfg->quadr.x_bit=(uint8_t)atoi(val); return 0; }
    else if (!strncmp(arg,"--stride=",9)) { if (val[0]=='\0') { con_error("--stride= requires a value"); return -1; } cfg->quadr.hint_stride=(uint8_t)atoi(val); return 0; }
    else if (!strncmp(arg,"--backend=",10)) { Backend bk; if (parse_backend(val, &bk) != 0) { con_error("unknown backend '%s'", val); return -1; } cfg->backend = (uint8_t)bk; return 0; }
    else if (!strncmp(arg,"--level=",8)) { if (val[0]=='\0') { con_error("--level= requires a value"); return -1; } cfg->level=atoi(val); return 0; }
    else if (!strcmp(arg,"--fast")) { cfg->use_fast_probe=1; return 0; }
    else if (!strcmp(arg,"--no-fast")) { cfg->use_fast_probe=0; return 0; }
    else if (!strcmp(arg,"--mixed-backend")) { cfg->mixed_backend=1; return 0; }
    else if (!strcmp(arg,"--adaptive-block")) { cfg->quadr.adaptive_block=1; return 0; }
    else if (!strcmp(arg,"--parallel")) { cfg->parallel=1; return 0; }
    else if (!strncmp(arg,"--threads=",10)) { if (val[0]=='\0') { con_error("--threads= requires a value"); return -1; } cfg->num_threads=atoi(val); return 0; }

    con_error("unknown option '%s'", arg);
    return -1;
}

int cmd_encode(const char *in_path, const char *out_path, const EncodeConfig *cfg) {
    if (cfg->parallel) {
        double t0 = quadr_now_ms();

        QuadrParallelOpts popts;
        quadr_parallel_opts_default(&popts);
        popts.num_threads = cfg->num_threads;
        popts.backend_id = (uint8_t)cfg->backend;
        popts.backend_level = effective_level(cfg);
        popts.block_size = cfg->quadr.block_size;

        QuadrError e = quadr_encode_file(in_path, out_path, &popts);
        if (e != QUADR_OK) {
            con_error("encode failed: %s", quadr_strerror(e));
            return 1;
        }

        double ms = quadr_now_ms() - t0;
        FILE *fi = fopen(in_path, "rb");
        FILE *fo = fopen(out_path, "rb");
        size_t sz_in = 0, sz_out = 0;
        if (fi) { fseek(fi,0,SEEK_END); sz_in = (size_t)ftell(fi); fclose(fi); }
        if (fo) { fseek(fo,0,SEEK_END); sz_out = (size_t)ftell(fo); fclose(fo); }

        double ratio = sz_in ? (double)sz_out / (double)sz_in * 100.0 : 0.0;
        double gbps = (ms > 0 && sz_in) ? (double)sz_in / 1e9 / (ms * 1e-3) : 0.0;

        char sz_a[32], sz_b[32];
        print_size_human(sz_in, sz_a, sizeof(sz_a));
        print_size_human(sz_out, sz_b, sizeof(sz_b));

        con_ok("encoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET " (parallel)", in_path, out_path);
        con_kv("size", "%s -> %s (%s%.2f%%" CON_RESET ")", sz_a, sz_b, CON_BRIGHT_GREEN);
        con_kv("speed", "%.2f GB/s (%.0f ms)", gbps, ms);
        con_kv("config", "threads=%d backend=%s(L%d) SIMD=%s",
            cfg->num_threads <= 0 ? quadr_detect_cpu_cores() : cfg->num_threads,
            backend_name(cfg->backend), effective_level(cfg),
            quadr_simd_level_name(quadr_simd_level()));
        return 0;
    }

    int lv = effective_level(cfg);

    QuadrEncodeOpts final_opts = cfg->quadr;
    if (cfg->quadr.adaptive_block) {
        uint64_t fsize = 0;
        FILE *fstat = fopen(in_path, "rb");
        if (fstat) {
            if (fseek(fstat, 0, SEEK_END) == 0) {
                long pos = ftell(fstat);
                if (pos >= 0) fsize = (uint64_t)pos;
            }
            fclose(fstat);
        }

        FILE *fprobe = fopen(in_path, "rb");
        if (fprobe) {
            uint8_t sample[65536];
            size_t slen = fread(sample, 1, sizeof(sample), fprobe);
            fclose(fprobe);
            if (slen > 0) {
                final_opts.block_size = quadr_select_block_size(
                    sample, slen, &final_opts, fsize);
            }
        }
    }

    uint64_t total_input_bytes = 0;
    FILE *fstat = fopen(in_path, "rb");
    if (fstat) {
        if (fseek(fstat, 0, SEEK_END) == 0) {
            long pos = ftell(fstat);
            if (pos >= 0) total_input_bytes = (uint64_t)pos;
        }
        fclose(fstat);
    }

    QuadrStreamCtx *ctx = quadr_stream_encode_open(out_path, &final_opts,
        (uint8_t)cfg->backend, lv, total_input_bytes);
    if (!ctx) { con_error("failed to open output: %s", out_path); return 1; }

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
        con_error("cannot open '%s'", in_path);
        quadr_stream_encode_close(ctx);
        return 1;
    }

    uint32_t bs = final_opts.block_size;
    uint8_t *read_buf = malloc(bs);
    if (!read_buf) {
        fclose(fin); quadr_stream_encode_close(ctx); return 1;
    }

    double t0 = quadr_now_ms();
    int ok = 1;

    for (;;) {
        size_t got = fread(read_buf, 1, bs, fin);
        if (got == 0) break;
        QuadrError e = quadr_stream_feed(ctx, read_buf, got);
        if (e != QUADR_OK) {
            con_error("encode failed: %s", quadr_strerror(e));
            ok = 0; break;
        }
    }

    fclose(fin);
    free(read_buf);

    if (!ok) { quadr_stream_encode_close(ctx); return 1; }

    QuadrError e = quadr_stream_encode_close(ctx);
    if (e != QUADR_OK) {
        con_error("encode close: %s", quadr_strerror(e));
        return 1;
    }

    double ms = quadr_now_ms() - t0;

    FILE *fi = fopen(in_path, "rb");
    FILE *fo = fopen(out_path, "rb");
    size_t sz_in = 0, sz_out = 0;
    if (fi) { fseek(fi,0,SEEK_END); sz_in = (size_t)ftell(fi); fclose(fi); }
    if (fo) { fseek(fo,0,SEEK_END); sz_out = (size_t)ftell(fo); fclose(fo); }

    double ratio = sz_in ? (double)sz_out / (double)sz_in * 100.0 : 0.0;
    double gbps = (ms > 0 && sz_in) ? (double)sz_in / 1e9 / (ms * 1e-3) : 0.0;

    char sz_a[32], sz_b[32];
    print_size_human(sz_in, sz_a, sizeof(sz_a));
    print_size_human(sz_out, sz_b, sizeof(sz_b));

    con_ok("encoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET, in_path, out_path);
    con_kv("size", "%s -> %s (%s%.2f%%" CON_RESET ")", sz_a, sz_b, CON_BRIGHT_GREEN);
    con_kv("speed", "%.2f GB/s (%.0f ms)", gbps, ms);
    con_kv("config", "block=%uKB backend=%s(L%d) SIMD=%s probe=%s",
        bs/1024, backend_name(cfg->backend), lv,
        quadr_simd_level_name(quadr_simd_level()),
        cfg->use_fast_probe ? "fast" : "full");
    return 0;
}
