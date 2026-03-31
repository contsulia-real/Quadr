/* cmd_info.c - Info command implementation */

#include "cmd_info.h"
#include "cmd_common.h"
#include "quadr_version.h"

int cmd_info(const char *path) {
    size_t fl = 0;
    uint8_t *fb = read_file(path, &fl);
    if (!fb) return 1;
    QuadrFileHeader fh = {0};
    if (quadr_file_header_read(fb, fl, &fh) != QUADR_OK) {
        con_error("not a Quadr file: %s", path); free(fb); return 1;
    }

    char sz_orig[32], sz_disk[32];
    print_size_human(fh.total_uncomp_size, sz_orig, sizeof(sz_orig));
    print_size_human((uint64_t)fl, sz_disk, sizeof(sz_disk));

    con_section("Quadr File Info");
    con_kv("file", "%s", path);
    con_kv("version", "0x%02X", fh.version);
    con_kv("original", "%s", sz_orig);
    con_kv("on-disk", "%s (%.2f%%)", sz_disk,
        fh.total_uncomp_size ? (double)fl/(double)fh.total_uncomp_size*100.0 : 0.0);
    con_kv("blocks", "%u", fh.block_count);
    con_kv("data_hint", "0x%02X", fh.data_hint);
    con_kv("simd", "%s", quadr_simd_level_name(quadr_simd_level()));

    uint32_t tc[4] = {0};
    uint32_t backends[6] = {0};
    for (uint32_t i = 0; i < fh.block_count; i++) {
        size_t rp = (size_t)fh.offset_table[i];
        if (rp + 5 + QUADR_BLOCK_HEADER_SIZE > fl) break;
        uint8_t bid = fb[rp];
        if (bid < 6) backends[bid]++;
        QuadrBlockHeader bh;
        if (quadr_block_header_read(fb + rp + 5, &bh) == QUADR_OK)
            tc[bh.type & 3]++;
    }

    con_nl();
    con_kv("block_types", "DELTA=%s%u" CON_RESET " RLE=%s%u" CON_RESET " PASSTHROUGH=%s%u" CON_RESET " RAW=%s%u" CON_RESET,
        CON_BRIGHT_CYAN, tc[0], CON_YELLOW, tc[1], CON_GRAY, tc[2], CON_BRIGHT_RED, tc[3]);
    con_kv("backends", "none=%u zlib=%u zstd=%u lz4=%u lz4hc=%u 7z=%u",
        backends[0], backends[1], backends[2], backends[3], backends[4], backends[5]);

    quadr_file_header_free(&fh); free(fb);
    return 0;
}
