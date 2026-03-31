/* cmd_list.c - List command implementation */

#include "cmd_list.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_list(const char *path) {
    QuadrArchiveInfo *info = quadr_archive_info(path);
    if (!info) {
        con_error("not a valid Quadr archive: %s", path);
        return 1;
    }

    char sz_orig[32], sz_comp[32];
    print_size_human(info->total_uncomp_size, sz_orig, sizeof(sz_orig));
    print_size_human(info->total_comp_size, sz_comp, sizeof(sz_comp));

    con_section("Archive Contents");
    con_kv("archive", "%s", path);
    con_kv("files", "%u", info->file_count);
    con_kv("total", "%s (original) %s (compressed)", sz_orig, sz_comp);
    if (info->total_uncomp_size > 0)
        con_kv("ratio", "%.2f%%",
            (double)info->total_comp_size / (double)info->total_uncomp_size * 100.0);

    con_nl();
    con_table_header("%-6s %-12s %-14s %-7s %s",
        "Index", "Size", "Compressed", "Ratio", "Path");
    printf("─────────────────────────────────────────────────────────────────\n");

    for (uint32_t i = 0; i < info->file_count; i++) {
        const QuadrArchiveEntry *e = &info->entries[i];
        double ratio = e->orig_size > 0 ?
            (double)e->comp_size / (double)e->orig_size * 100.0 : 0.0;

        char sz_o[32], sz_c[32];
        print_size_human(e->orig_size, sz_o, sizeof(sz_o));
        print_size_human(e->comp_size, sz_c, sizeof(sz_c));

        const char *rc = ratio < 100.0 ? CON_BRIGHT_GREEN : CON_GRAY;
        printf("%-6u %-12s %-14s %s%5.1f%%" CON_RESET " %s\n",
            i, sz_o, sz_c, rc, ratio, e->path);
    }

    quadr_archive_info_free(info);
    return 0;
}
