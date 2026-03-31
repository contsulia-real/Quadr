/* cmd_verify.c - Verify command implementation */

#include "cmd_verify.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_verify(const char *path) {
    double t0 = quadr_now_ms();

    QuadrStreamCtx *ctx = quadr_stream_decode_open(path, 0);
    if (!ctx) {
        con_error("not a valid Quadr file: %s", path);
        return 1;
    }

    uint8_t buf[65536];
    uint32_t blk_ok = 0;
    int failed = 0;

    for (;;) {
        size_t got = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, sizeof(buf), &got);
        if (e == QUADR_ERR_TRUNC) { blk_ok++; break; }
        if (e != QUADR_OK) {
            con_error("block %u failed: %s", blk_ok, quadr_strerror(e));
            failed = 1;
            break;
        }
        blk_ok++;
    }

    double ms = quadr_now_ms() - t0;
    quadr_stream_close(ctx);

    if (!failed)
        con_ok(CON_BOLD "%s" CON_RESET " (%u blocks, %.0f ms)", path, blk_ok, ms);
    return failed;
}

int cmd_verify_archive(const char *path) {
    double t0 = quadr_now_ms();
    uint32_t bad = UINT32_MAX;

    QuadrError e = quadr_archive_verify(path, &bad);
    double ms = quadr_now_ms() - t0;

    if (e == QUADR_OK) {
        QuadrArchiveInfo *info = quadr_archive_info(path);
        if (info) {
            con_ok(CON_BOLD "%s" CON_RESET " (%u files, %.0f ms)", path, info->file_count, ms);
            quadr_archive_info_free(info);
        } else {
            con_ok(CON_BOLD "%s" CON_RESET " (%.0f ms)", path, ms);
        }
        return 0;
    } else {
        if (bad != UINT32_MAX)
            con_error(CON_BOLD "%s" CON_RESET " entry %u: %s (%.0f ms)", path, bad, quadr_strerror(e), ms);
        else
            con_error(CON_BOLD "%s" CON_RESET ": %s (%.0f ms)", path, quadr_strerror(e), ms);
        return 1;
    }
}
