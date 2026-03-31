#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QLVerifyResult ql_verify(const char *path) {
    QLVerifyResult res = {0};
    res.ok = 0;

    double t0 = quadr_now_ms();

    QuadrStreamCtx *ctx = quadr_stream_decode_open(path, 0);
    if (!ctx) {
        snprintf(res.error, sizeof(res.error), "not a valid Quadr file: %s", path);
        return res;
    }

    uint8_t buf[65536];
    uint32_t blk_ok = 0;

    for (;;) {
        size_t got = 0;
        QuadrError e = quadr_stream_pull(ctx, buf, sizeof(buf), &got);
        if (e == QUADR_ERR_TRUNC) { blk_ok++; break; }
        if (e != QUADR_OK) {
            snprintf(res.error, sizeof(res.error), "block %u failed: %s", blk_ok, quadr_strerror(e));
            break;
        }
        blk_ok++;
    }

    double ms = quadr_now_ms() - t0;
    quadr_stream_close(ctx);

    res.blocks_verified = blk_ok;
    res.time_ms = ms;
    res.ok = (res.error[0] == '\0');
    return res;
}

QLVerifyArchiveResult ql_verify_archive(const char *path) {
    QLVerifyArchiveResult res = {0};
    res.ok = 0;

    double t0 = quadr_now_ms();
    uint32_t bad = UINT32_MAX;

    QuadrError e = quadr_archive_verify(path, &bad);
    double ms = quadr_now_ms() - t0;

    if (e == QUADR_OK) {
        QuadrArchiveInfo *info = quadr_archive_info(path);
        if (info) {
            res.file_count = info->file_count;
            quadr_archive_info_free(info);
        }
        res.time_ms = ms;
        res.ok = 1;
    } else {
        if (bad != UINT32_MAX)
            snprintf(res.error, sizeof(res.error), "entry %u: %s", bad, quadr_strerror(e));
        else
            snprintf(res.error, sizeof(res.error), "%s", quadr_strerror(e));
        res.time_ms = ms;
    }
    return res;
}
