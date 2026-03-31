/* cmd_bench.c - Bench command implementation */

#include "cmd_bench.h"
#include "cmd_common.h"
#include "quadr_version.h"

int cmd_bench(const char *path) {
    QLBenchResult res = ql_bench(path);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    con_section("Benchmark");
    con_kv("file", "%s", path);
    con_kv("size", "%llu bytes", (unsigned long long)res.file_size);
    con_kv("simd", "%s", res.simd_level);

    con_nl();
    con_kv("block_type", "%s (stride=%d shuffle=%d)", res.block_type, res.stride, res.shuffle);
    con_kv("probe_fast", "%.3f ms %.2f GB/s",
        res.probe_fast_ms, res.probe_fast_gbps);
    con_kv("probe_full", "%.3f ms %.2f GB/s (fast %.1fx)",
        res.probe_full_ms, res.probe_full_gbps, res.probe_speedup);
    con_kv("encode", "%.3f ms %.2f GB/s",
        res.encode_ms, res.encode_gbps);
    con_kv("decode", "%.3f ms %.2f GB/s",
        res.decode_ms, res.decode_gbps);

    return 0;
}
