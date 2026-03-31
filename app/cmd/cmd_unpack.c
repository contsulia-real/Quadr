/* cmd_unpack.c - Unpack command implementation */

#include "cmd_unpack.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_unpack(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_dir = NULL;
    int num_threads = 0;
    uint32_t specific_file = UINT32_MAX;

    int pos_args = 0;
    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "--threads=", 10)) {
            if (argv[i][10] == '\0') { con_error("--threads= requires a value"); return 1; }
            num_threads = atoi(argv[i] + 10);
        } else if (!strncmp(argv[i], "--file=", 7)) {
            if (argv[i][7] == '\0') { con_error("--file= requires a value"); return 1; }
            specific_file = (uint32_t)atoi(argv[i] + 7);
        } else if (argv[i][0] != '-') {
            if (pos_args == 0) in_path = argv[i];
            else if (pos_args == 1) out_dir = argv[i];
            pos_args++;
        } else {
            con_error("unknown option '%s'", argv[i]);
            return 1;
        }
    }

    if (!in_path) {
        con_error("missing <archive.qar> argument");
        return 1;
    }

    double t0 = quadr_now_ms();
    QuadrError e;

    if (specific_file != UINT32_MAX) {
        e = quadr_archive_unpack_file(in_path, specific_file, NULL, out_dir);
    } else {
        e = quadr_archive_unpack(in_path, out_dir, num_threads, NULL, NULL);
    }

    if (e != QUADR_OK) {
        con_error("unpack failed: %s", quadr_strerror(e));
        return 1;
    }

    double ms = quadr_now_ms() - t0;
    QuadrArchiveInfo *info = quadr_archive_info(in_path);
    if (info) {
        if (specific_file != UINT32_MAX) {
            if (specific_file < info->file_count)
                con_ok("extracted " CON_BOLD "'%s'" CON_RESET " -> %s (%.0f ms)",
                    info->entries[specific_file].path,
                    out_dir ? out_dir : ".", ms);
        } else {
            con_ok("extracted " CON_BOLD "%u" CON_RESET " files from " CON_BOLD "%s" CON_RESET " (%.0f ms)",
                info->file_count, in_path, ms);
        }
        quadr_archive_info_free(info);
    } else {
        con_ok("unpacked %s (%.0f ms)", in_path, ms);
    }

    return 0;
}
