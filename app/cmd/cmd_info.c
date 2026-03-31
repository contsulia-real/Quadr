/* cmd_info.c - Info command implementation */

#include "cmd_info.h"
#include "cmd_common.h"
#include "quadr_version.h"

int cmd_info(const char *path) {
    QLInfoResult res = ql_info(path);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    char sz_orig[32], sz_disk[32];
    print_size_human(res.original_size, sz_orig, sizeof(sz_orig));
    print_size_human(res.on_disk_size, sz_disk, sizeof(sz_disk));

    con_section("Quadr File Info");
    con_kv("file", "%s", path);
    con_kv("version", "0x%02X", res.version);
    con_kv("original", "%s", sz_orig);
    con_kv("on-disk", "%s (%.2f%%)", sz_disk,
        res.original_size ? (double)res.on_disk_size/(double)res.original_size*100.0 : 0.0);
    con_kv("blocks", "%u", res.block_count);
    con_kv("data_hint", "0x%02X", res.data_hint);
    con_kv("simd", "%s", res.simd_level);

    con_nl();
    con_kv("block_types", "DELTA=%s%u" CON_RESET " RLE=%s%u" CON_RESET " PASSTHROUGH=%s%u" CON_RESET " RAW=%s%u" CON_RESET,
        CON_BRIGHT_CYAN, res.block_stats.block_types[0], CON_YELLOW, res.block_stats.block_types[1],
        CON_GRAY, res.block_stats.block_types[2], CON_BRIGHT_RED, res.block_stats.block_types[3]);
    con_kv("backends", "none=%u zlib=%u zstd=%u lz4=%u lz4hc=%u 7z=%u",
        res.block_stats.backends[0], res.block_stats.backends[1], res.block_stats.backends[2],
        res.block_stats.backends[3], res.block_stats.backends[4], res.block_stats.backends[5]);

    return 0;
}
