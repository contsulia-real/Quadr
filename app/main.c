/*
 * main.c  –  Quadr command-line tool
 *
 * Commands:
 *   quadr encode [OPTIONS] <input> <output>
 *   quadr decode   [OPTIONS] <input> <output>
 *   quadr info             <file>
 *   quadr bench            <file>
 *   quadr verify           <file>
 *   quadr probe   [OPTIONS] <file>
 *   quadr pack    [OPTIONS] -o <output.qar> <files...>
 *   quadr unpack  [OPTIONS] <archive.qar> [output_dir]
 *   quadr list             <archive.qar>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quadr.h"
#include "quadr_zlib_compat.h"
#include "quadr_console.h"

#include "cmd_common.h"
#include "cmd_encode.h"
#include "cmd_decode.h"
#include "cmd_info.h"
#include "cmd_bench.h"
#include "cmd_verify.h"
#include "cmd_probe.h"
#include "cmd_pack.h"
#include "cmd_unpack.h"
#include "cmd_list.h"
#include "cmd_help.h"

#ifdef QUADR_HAVE_ZSTD
#  include <zstd.h>
#endif
#ifdef QUADR_HAVE_LZ4
#  include <lz4.h>
#endif
#ifdef QUADR_HAVE_LZ4HC
#  include <lz4hc.h>
#endif
#ifdef QUADR_HAVE_7Z
#  include <lzma.h>
#endif

/* ─────────────────────────────────────────────────────────────────────────
 * Backend
 * ───────────────────────────────────────────────────────────────────────── */

static size_t backend_bound(Backend b, size_t n) {
    switch (b) {
#ifdef QUADR_HAVE_ZLIBNG
        case BACKEND_ZLIB:  return QUADR_COMPRESSBOUND(n) + 16;
#endif
#ifdef QUADR_HAVE_ZSTD
        case BACKEND_ZSTD:  return ZSTD_compressBound(n) + 16;
#endif
#ifdef QUADR_HAVE_LZ4
        case BACKEND_LZ4:
        case BACKEND_LZ4HC: return (size_t)LZ4_compressBound((int)n) + 16;
#endif
#ifdef QUADR_HAVE_7Z
        case BACKEND_7Z:    return n + 64;
#endif
        default: return n + 16;
    }
}

static size_t backend_compress(Backend b, int level,
                               const uint8_t *in, size_t in_len,
                               uint8_t *out, size_t out_cap) {
    switch (b) {
    case BACKEND_NONE:
        if (out_cap < in_len) return 0;
        memcpy(out, in, in_len);
        return in_len;
#ifdef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        size_t ol = out_cap;
        return (QUADR_COMPRESS2(out, &ol, in, (uLong)in_len, level) == Z_OK)
               ? (size_t)ol : 0;
    }
#endif
#ifndef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        uLongf ol = (uLongf)out_cap;
        return (QUADR_COMPRESS2(out, &ol, in, (uLong)in_len, level) == Z_OK)
               ? (size_t)ol : 0;
    }
#endif
#ifdef QUADR_HAVE_ZSTD
    case BACKEND_ZSTD: {
        size_t r = ZSTD_compress(out, out_cap, in, in_len, level);
        return ZSTD_isError(r) ? 0 : r;
    }
#endif
#ifdef QUADR_HAVE_LZ4
    case BACKEND_LZ4: {
        int r = LZ4_compress_default((const char *)in, (char *)out,
                                     (int)in_len, (int)out_cap);
        return (r > 0) ? (size_t)r : 0;
    }
#endif
#ifdef QUADR_HAVE_LZ4HC
    case BACKEND_LZ4HC: {
        int r = LZ4_compress_HC((const char *)in, (char *)out,
                                (int)in_len, (int)out_cap, level);
        return (r > 0) ? (size_t)r : 0;
    }
#endif
#ifdef QUADR_HAVE_7Z
    case BACKEND_7Z: {
        size_t out_len = out_cap;
        uint32_t preset = (level < 0) ? 6u : (uint32_t)level;
        lzma_ret lr = lzma_easy_buffer_encode(preset,
                                              LZMA_CHECK_CRC64,
                                              NULL,
                                              in, in_len,
                                              out, &out_len, out_cap);
        return (lr == LZMA_OK) ? out_len : 0;
    }
#endif
    default:
        con_error("backend '%s' not compiled in", backend_name(b));
        return 0;
    }
}

static int backend_decompress(Backend b,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t expected) {
    switch (b) {
    case BACKEND_NONE:
        if (in_len != expected) return -1;
        memcpy(out, in, in_len);
        return 0;
#ifdef QUADR_HAVE_ZSTD
    case BACKEND_ZSTD: {
        size_t r = ZSTD_decompress(out, expected, in, in_len);
        return (!ZSTD_isError(r) && r == expected) ? 0 : -1;
    }
#endif
#ifdef QUADR_HAVE_LZ4
    case BACKEND_LZ4:
    case BACKEND_LZ4HC: {
        int r = LZ4_decompress_safe((const char *)in, (char *)out,
                                    (int)in_len, (int)expected);
        return (r == (int)expected) ? 0 : -1;
    }
#endif
#ifdef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        size_t ol = expected;
        return (QUADR_UNCOMPRESS(out, &ol, in, (uLong)in_len) == Z_OK
                && ol == expected) ? 0 : -1;
    }
#endif
#ifndef QUADR_HAVE_ZLIBNG
    case BACKEND_ZLIB: {
        uLongf ol = (uLongf)expected;
        return (QUADR_UNCOMPRESS(out, &ol, in, (uLong)in_len) == Z_OK
                && (size_t)ol == expected) ? 0 : -1;
    }
#endif
#ifdef QUADR_HAVE_7Z
    case BACKEND_7Z: {
        lzma_stream strm = LZMA_STREAM_INIT;
        lzma_ret lr = lzma_stream_decoder(&strm, UINT64_MAX, 0);
        if (lr != LZMA_OK) return -1;
        strm.next_in = in;
        strm.avail_in = in_len;
        strm.next_out = out;
        strm.avail_out = expected;
        lr = lzma_code(&strm, LZMA_FINISH);
        size_t out_written = expected - strm.avail_out;
        lzma_end(&strm);
        return (lr == LZMA_STREAM_END && out_written == expected) ? 0 : -1;
    }
#endif
    default: return -1;
    }
}

/* ── Backend adapter wrappers ──────────────────────────────────────────── */

typedef struct { Backend b; int level; } BkAdapter;

static size_t bk_adapter_compress(void *ud, int level,
                                  const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_cap) {
    BkAdapter *a = (BkAdapter *)ud;
    int lv = (level >= 0) ? level : a->level;
    return backend_compress(a->b, lv, in, in_len, out, out_cap);
}

static int bk_adapter_decompress(void *ud,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t expected) {
    BkAdapter *a = (BkAdapter *)ud;
    return backend_decompress(a->b, in, in_len, out, expected);
}

static size_t bk_adapter_bound(void *ud, size_t n) {
    BkAdapter *a = (BkAdapter *)ud;
    return backend_bound(a->b, n);
}

static void register_backend(Backend b) {
    static BkAdapter adapters[6];
    int idx = (int)b;
    adapters[idx].b = b;
    adapters[idx].level = backend_default_level(b);

    QuadrBackend reg = {
        .id             = (uint8_t)b,
        .name           = backend_name(b),
        .compress       = bk_adapter_compress,
        .decompress     = bk_adapter_decompress,
        .bound          = bk_adapter_bound,
        .userdata       = &adapters[idx],
        .default_level  = backend_default_level(b),
    };
    quadr_backend_register(&reg);
}

static void init_backends(void) {
#ifdef QUADR_HAVE_ZLIBNG
    register_backend(BACKEND_ZLIB);
#endif
#ifdef QUADR_HAVE_ZSTD
    register_backend(BACKEND_ZSTD);
#endif
#ifdef QUADR_HAVE_LZ4
    register_backend(BACKEND_LZ4);
    register_backend(BACKEND_LZ4HC);
#endif
#ifdef QUADR_HAVE_7Z
    register_backend(BACKEND_7Z);
#endif
}

/* ─────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    con_init();
    init_backends();

    if (argc < 2) { usage_short(argv[0]); return 1; }

    const char *sub = argv[1];

    /* Global options: --help / -h / --version / -V */
    if (!strcmp(sub, "--help") || !strcmp(sub, "-h")) {
        usage_full(argv[0]);
        return 0;
    }
    if (!strcmp(sub, "--version") || !strcmp(sub, "-V")) {
        show_version();
        return 0;
    }

    /* Sub-commands */
    if (!strcmp(sub, "help"))
        return cmd_help(argv[0]);

    if (!strcmp(sub, "version")) {
        show_version();
        return 0;
    }

    if (!strcmp(sub, "info") && argc >= 3) {
        if (argc == 3 && (argc < 4 || !strcmp(argv[2], "--help") || !strcmp(argv[2], "-h")))
            return cmd_help_for("info", argv[0]);
        return cmd_info(argv[2]);
    }

    if (!strcmp(sub, "bench") && argc >= 3)
        return cmd_bench(argv[2]);

    if (!strcmp(sub, "verify") && argc >= 3) {
        const char *path = argv[2];
        size_t path_len = strlen(path);
        if (path_len > 4 && !strcmp(path + path_len - 4, ".qar"))
            return cmd_verify_archive(path);
        return cmd_verify(path);
    }

    if (!strcmp(sub, "list") && argc >= 3)
        return cmd_list(argv[2]);

    if (!strcmp(sub, "pack") && argc >= 3) {
        if (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h")))
            return cmd_help_for("pack", argv[0]);
        return cmd_pack(argc - 2, argv + 2);
    }

    if (!strcmp(sub, "unpack") && argc >= 3) {
        if (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h")))
            return cmd_help_for("unpack", argv[0]);
        return cmd_unpack(argc - 2, argv + 2);
    }

    if (!strcmp(sub, "decode")) {
        if (argc == 2 || (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h"))))
            return cmd_help_for("decode", argv[0]);
        int parallel = 0, num_threads = 0;
        const char *in_path = NULL, *out_path = NULL;
        int pos = 0;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
                return cmd_help_for("decode", argv[0]);
            if (!strncmp(argv[i], "--threads=", 10)) {
                if (argv[i][10] == '\0') { con_error("--threads= requires a value"); return 1; }
                num_threads = atoi(argv[i] + 10);
            } else if (!strcmp(argv[i], "--parallel")) {
                parallel = 1;
            } else if (argv[i][0] != '-') {
                if (pos == 0) in_path = argv[i];
                else if (pos == 1) out_path = argv[i];
                pos++;
            } else {
                con_error("unknown option '%s'", argv[i]);
                return 1;
            }
        }
        if (!in_path || !out_path) { con_error("decode requires <input> <output>"); return 1; }
        return cmd_decode(in_path, out_path, parallel, num_threads);
    }

    if (!strcmp(sub, "encode") || !strcmp(sub, "probe")) {
        if (argc == 2 || (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h"))))
            return cmd_help_for(sub, argv[0]);
        EncodeConfig cfg;
        encode_config_default(&cfg);
        int i = 2;
        for (; i < argc && argv[i][0] == '-'; i++) {
            if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
                return cmd_help_for(sub, argv[0]);
            if (parse_encode_opts(argc, argv, &i, &cfg) != 0)
                return 1;
        }

        if (!strcmp(sub, "encode")) {
            if (argc - i < 2) { con_error("encode requires <input> <output>"); return 1; }
            return cmd_encode(argv[i], argv[i+1], &cfg);
        } else {
            if (argc - i < 1) { con_error("probe requires <file>"); return 1; }
            return cmd_probe(argv[i], &cfg);
        }
    }

    con_error("unknown command '%s'", sub);
    usage_short(argv[0]);
    return 1;
}
