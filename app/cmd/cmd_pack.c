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
    const char **files = (const char **)&argv[file_start];

    QLPackResult res = ql_pack(files, file_count, out_path, backend,
                               level, block_size, num_threads, store_paths, base_dir);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    char sz_orig[32], sz_comp[32];
    print_size_human(res.total_uncomp_size, sz_orig, sizeof(sz_orig));
    print_size_human(res.total_comp_size, sz_comp, sizeof(sz_comp));

    con_ok("packed " CON_BOLD "%u" CON_RESET " files -> " CON_BOLD "%s" CON_RESET, res.file_count, out_path);
    con_kv("size", "%s -> %s (%.2f%%)", sz_orig, sz_comp, res.ratio_pct);
    con_kv("time", "%.0f ms backend=%s", res.time_ms, backend_name(backend));

    return 0;
}
