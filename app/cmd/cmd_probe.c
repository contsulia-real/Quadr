/* cmd_probe.c - Probe command implementation */

#include "cmd_probe.h"
#include "cmd_common.h"
#include "quadr_version.h"

int cmd_probe(const char *path, const EncodeConfig *cfg) {
    size_t raw_len = 0;
    uint8_t *raw = read_file(path, &raw_len);
    if (!raw) return 1;

    uint32_t bs = cfg->quadr.block_size;
    uint32_t bc = raw_len ? (uint32_t)((raw_len + bs - 1) / bs) : 0;
    const char *type_str[] = {"DELTA","RLE","PASSTHROUGH","RAW"};
    const char *type_colors[] = {CON_BRIGHT_CYAN, CON_YELLOW, CON_GRAY, CON_BRIGHT_RED};

    con_section("Probe");
    con_kv("file", "%s", path);
    con_kv("size", "%zu bytes (%u blocks x %uKB)", raw_len, bc, bs/1024);
    con_kv("simd", "%s mode=%s", quadr_simd_level_name(quadr_simd_level()),
        cfg->use_fast_probe ? "fast" : "full");

    con_nl();
    con_table_header("%-6s %-12s %-7s %-7s %-6s %s",
        "Block", "Type", "Stride", "Shuffle", "Score", "Size");
    printf("─────────────────────────────────────────────────────────\n");

    uint32_t tc[4] = {0};
    double t_probe = 0.0;

    for (uint32_t bi = 0; bi < bc; bi++) {
        size_t off = (size_t)bi * bs;
        size_t blen = (off + bs <= raw_len) ? bs : raw_len - off;
        const uint8_t *blk = raw + off;

        double t0, t1;
        t0 = quadr_now_ms();
        QuadrProbeResult r = do_probe(blk, blen, cfg);
        t1 = quadr_now_ms();
        t_probe += (t1 - t0);

        tc[r.type]++;
        printf("%-6u %s%-12s" CON_RESET " %-7d %-7d %-6.3f %zu B\n",
            bi, type_colors[r.type], type_str[r.type], r.stride, r.shuffle, r.score, blen);
    }

    printf("─────────────────────────────────────────────────────────\n");
    con_nl();
    con_kv("summary", "DELTA=%s%u" CON_RESET " RLE=%s%u" CON_RESET " PASSTHROUGH=%s%u" CON_RESET " RAW=%s%u" CON_RESET,
        CON_BRIGHT_CYAN, tc[0], CON_YELLOW, tc[1], CON_GRAY, tc[2], CON_BRIGHT_RED, tc[3]);
    con_kv("probe_time", "%.2f ms (%.2f ms/block %.2f GB/s)",
        t_probe,
        bc ? t_probe / bc : 0.0,
        (raw_len && t_probe > 0) ? (double)raw_len / 1e9 / (t_probe * 1e-3) : 0.0);

    free(raw);
    return 0;
}
