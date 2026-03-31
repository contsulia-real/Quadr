/* cmd_encode.c - Encode command implementation */

#include "cmd_encode.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_encode(const char *in_path, const char *out_path, const EncodeConfig *cfg) {
    QLEncodeConfig ql_cfg;
    ql_encode_config_default(&ql_cfg);
    ql_cfg.backend = (QLBackend)cfg->backend;
    ql_cfg.level = cfg->level;
    ql_cfg.use_fast_probe = cfg->use_fast_probe;
    ql_cfg.mixed_backend = cfg->mixed_backend;
    ql_cfg.parallel = cfg->parallel;
    ql_cfg.num_threads = cfg->num_threads;
    ql_cfg.auto_configure = cfg->auto_configure;
    ql_cfg.auto_mode = cfg->auto_mode;
    ql_cfg.quadr = cfg->quadr;

    if (ql_cfg.auto_configure) {
        ql_auto_configure(&ql_cfg, in_path);
    }

    QLEncodeResult res = ql_encode(in_path, out_path, &ql_cfg);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    char sz_a[32], sz_b[32];
    print_size_human(res.input_size, sz_a, sizeof(sz_a));
    print_size_human(res.output_size, sz_b, sizeof(sz_b));

    if (res.parallel) {
        con_ok("encoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET " (parallel)", in_path, out_path);
        con_kv("size", "%s -> %s (%s%.2f%%" CON_RESET ")", sz_a, sz_b, CON_BRIGHT_GREEN, res.ratio_pct);
        con_kv("speed", "%.2f GB/s (%.0f ms)", res.speed_gbps, res.time_ms);
        con_kv("config", "threads=%d backend=%s(L%d) SIMD=%s",
            res.num_threads <= 0 ? quadr_detect_cpu_cores() : res.num_threads,
            backend_name(res.backend), res.level,
            res.simd_level);
    } else {
        con_ok("encoded " CON_BOLD "%s" CON_RESET " -> " CON_BOLD "%s" CON_RESET, in_path, out_path);
        con_kv("size", "%s -> %s (%s%.2f%%" CON_RESET ")", sz_a, sz_b, CON_BRIGHT_GREEN, res.ratio_pct);
        con_kv("speed", "%.2f GB/s (%.0f ms)", res.speed_gbps, res.time_ms);
        con_kv("config", "block=%uKB backend=%s(L%d) SIMD=%s probe=%s",
            res.block_size/1024, backend_name(res.backend), res.level,
            res.simd_level, res.probe_mode);
    }
    return 0;
}
