/* cmd_decode.c - Decode command implementation */

#include "cmd_decode.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_decode(const char *in_path, const char *out_path, int parallel, int num_threads) {
    QLDecodeResult res = ql_decode(in_path, out_path, parallel, num_threads);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    char sz_b[32];
    print_size_human(res.output_size, sz_b, sizeof(sz_b));

    if (res.parallel) {
        con_ok("decoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET " (parallel)", in_path, out_path);
        con_kv("size", "%s (%.2f GB/s)", sz_b, res.speed_gbps);
        con_kv("time", "%.0f ms threads=%d", res.time_ms,
            res.num_threads <= 0 ? quadr_detect_cpu_cores() : res.num_threads);
    } else {
        con_ok("decoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET, in_path, out_path);
        con_kv("size", "%s (%.2f GB/s)", sz_b, res.speed_gbps);
        con_kv("time", "%.0f ms", res.time_ms);
    }
    return 0;
}
