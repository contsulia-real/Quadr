/* cmd_pack.c - Pack command implementation */

#include "cmd_pack.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_pack(int argc, char **argv) {
    const char *out_path = NULL;
    Backend backend = BACKEND_ZSTD;
    int level = -1;
    uint32_t block_size = 0;
    int num_threads = 0;
    int store_paths = 1;
    const char *base_dir = NULL;

    int file_start = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strncmp(argv[i], "--backend=", 10)) {
            if (parse_backend(argv[i] + 10, &backend) != 0) {
                con_error("unknown backend '%s'", argv[i] + 10);
                return 1;
            }
        } else if (!strncmp(argv[i], "--level=", 8)) {
            if (argv[i][8] == '\0') { con_error("--level= requires a value"); return 1; }
            level = atoi(argv[i] + 8);
        } else if (!strncmp(argv[i], "--block=", 8)) {
            if (argv[i][8] == '\0') { con_error("--block= requires a value"); return 1; }
            block_size = (uint32_t)(atoi(argv[i] + 8) * 1024);
        } else if (!strncmp(argv[i], "--threads=", 10)) {
            if (argv[i][10] == '\0') { con_error("--threads= requires a value"); return 1; }
            num_threads = atoi(argv[i] + 10);
        } else if (!strcmp(argv[i], "--no-paths")) {
            store_paths = 0;
        } else if (!strncmp(argv[i], "--base-dir=", 11)) {
            base_dir = argv[i] + 11;
        } else if (argv[i][0] != '-') {
            file_start = i;
            break;
        } else {
            con_error("unknown option '%s'", argv[i]);
            return 1;
        }
    }

    if (!out_path) {
        con_error("missing -o <output.qar>");
        return 1;
    }
    if (file_start == 0 || file_start >= argc) {
        con_error("no input files specified");
        return 1;
    }

    uint32_t file_count = (uint32_t)(argc - file_start);
    if (file_count > QUADR_ARCHIVE_MAX_FILES) {
        con_error("too many files (max %u)", QUADR_ARCHIVE_MAX_FILES);
        return 1;
    }

    const char **files = (const char **)&argv[file_start];

    QuadrArchiveOpts aopts;
    quadr_archive_opts_default(&aopts);
    aopts.backend_id = (uint8_t)backend;
    aopts.backend_level = (level >= 0) ? level : backend_default_level(backend);
    aopts.block_size = block_size;
    aopts.num_threads = num_threads;
    aopts.store_paths = store_paths;
    aopts.base_dir = base_dir;

    double t0 = quadr_now_ms();

    QuadrError e = quadr_archive_pack(files, file_count, out_path, &aopts, NULL, NULL);
    if (e != QUADR_OK) {
        con_error("pack failed: %s", quadr_strerror(e));
        for (uint32_t fi = 0; fi < file_count; fi++) {
            FILE *tf = fopen(files[fi], "rb");
            if (tf) {
                fseek(tf, 0, SEEK_END);
                long sz = ftell(tf);
                fclose(tf);
                char sz_s[32];
                print_size_human((uint64_t)sz, sz_s, sizeof(sz_s));
                con_info(" [%u] %s (%s) ok", fi, files[fi], sz_s);
            } else {
                con_error(" [%u] %s cannot open", fi, files[fi]);
            }
        }
        return 1;
    }

    double ms = quadr_now_ms() - t0;

    QuadrArchiveInfo *info = quadr_archive_info(out_path);
    if (info) {
        char sz_orig[32], sz_comp[32];
        print_size_human(info->total_uncomp_size, sz_orig, sizeof(sz_orig));
        print_size_human(info->total_comp_size, sz_comp, sizeof(sz_comp));

        con_ok("packed " CON_BOLD "%u" CON_RESET " files -> " CON_BOLD "%s" CON_RESET, info->file_count, out_path);
        con_kv("size", "%s -> %s (%.2f%%)", sz_orig, sz_comp,
            info->total_uncomp_size ?
            (double)info->total_comp_size / (double)info->total_uncomp_size * 100.0 : 0.0);
        con_kv("time", "%.0f ms backend=%s", ms, backend_name(backend));
        quadr_archive_info_free(info);
    } else {
        con_ok("packed %u files -> %s (%.0f ms)", file_count, out_path, ms);
    }

    return 0;
}
