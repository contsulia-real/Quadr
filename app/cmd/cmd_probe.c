/* cmd_probe.c - Probe command implementation */

#include "cmd_probe.h"
#include "cmd_common.h"
#include "quadr_version.h"

int cmd_probe(const char *path, const QLEncodeConfig *cfg) {
    QLProbeResult res = ql_probe(path, cfg);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    const char *type_str[] = {"DELTA","RLE","PASSTHROUGH","RAW"};
    const char *type_colors[] = {CON_BRIGHT_CYAN, CON_YELLOW, CON_GRAY, CON_BRIGHT_RED};

    con_section("Probe");
    con_kv("file", "%s", path);
    con_kv("size", "%llu bytes (%u blocks x %uKB)",
        (unsigned long long)res.file_size, res.block_count, res.block_size_kb);
    con_kv("simd", "%s mode=%s", res.simd_level, res.probe_mode);

    con_nl();
    con_table_header("%-6s %-12s %-7s %-7s %-6s %s",
        "Block", "Type", "Stride", "Shuffle", "Score", "Size");
    printf("─────────────────────────────────────────────────────────\n");

    for (uint32_t bi = 0; bi < res.num_blocks; bi++) {
        QLProbeBlockInfo *b = &res.blocks[bi];
        printf("%-6u %s%-12s" CON_RESET " %-7d %-7d %-6.3f %zu B\n",
            b->block_index, type_colors[b->type], type_str[b->type],
            b->stride, b->shuffle, b->score, b->size);
    }

    printf("─────────────────────────────────────────────────────────\n");
    con_nl();
    con_kv("summary", "DELTA=%s%u" CON_RESET " RLE=%s%u" CON_RESET " PASSTHROUGH=%s%u" CON_RESET " RAW=%s%u" CON_RESET,
        CON_BRIGHT_CYAN, res.type_counts[0], CON_YELLOW, res.type_counts[1],
        CON_GRAY, res.type_counts[2], CON_BRIGHT_RED, res.type_counts[3]);
    con_kv("probe_time", "%.2f ms (%.2f ms/block %.2f GB/s)",
        res.total_probe_ms, res.avg_probe_ms, res.probe_gbps);

    ql_probe_result_free(&res);
    return 0;
}
