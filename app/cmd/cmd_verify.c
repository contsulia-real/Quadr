/* cmd_verify.c - Verify command implementation */

#include "cmd_verify.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_verify(const char *path) {
    QLVerifyResult res = ql_verify(path);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    con_ok(CON_BOLD "%s" CON_RESET " (%u blocks, %.0f ms)", path, res.blocks_verified, res.time_ms);
    return 0;
}

int cmd_verify_archive(const char *path) {
    QLVerifyArchiveResult res = ql_verify_archive(path);

    if (!res.ok) {
        con_error(CON_BOLD "%s" CON_RESET ": %s (%.0f ms)", path, res.error, res.time_ms);
        return 1;
    }

    con_ok(CON_BOLD "%s" CON_RESET " (%u files, %.0f ms)", path, res.file_count, res.time_ms);
    return 0;
}
