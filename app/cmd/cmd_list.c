/* cmd_list.c - List command implementation */

#include "cmd_list.h"
#include "cmd_common.h"
#include "quadr_archive.h"
#include "quadr_version.h"

int cmd_list(const char *path) {
    QLListResult res = ql_list(path);

    if (!res.ok) {
        con_error("%s", res.error);
        return 1;
    }

    char sz_orig[32], sz_comp[32];
    print_size_human(res.total_uncomp_size, sz_orig, sizeof(sz_orig));
    print_size_human(res.total_comp_size, sz_comp, sizeof(sz_comp));

    con_section("Archive Contents");
    con_kv("archive", "%s", path);
    con_kv("files", "%u", res.file_count);
    con_kv("total", "%s (original) %s (compressed)", sz_orig, sz_comp);
    if (res.total_uncomp_size > 0)
        con_kv("ratio", "%.2f%%", res.total_ratio_pct);

    con_nl();
    con_table_header("%-6s %-12s %-14s %-7s %s",
        "Index", "Size", "Compressed", "Ratio", "Path");
    printf("─────────────────────────────────────────────────────────────────\n");

    for (uint32_t i = 0; i < res.num_entries; i++) {
        QLArchiveEntryInfo *e = &res.entries[i];
        const char *rc = e->ratio_pct < 100.0 ? CON_BRIGHT_GREEN : CON_GRAY;

        char sz_o[32], sz_c[32];
        print_size_human(e->orig_size, sz_o, sizeof(sz_o));
        print_size_human(e->comp_size, sz_c, sizeof(sz_c));

        printf("%-6u %-12s %-14s %s%5.1f%%" CON_RESET " %s\n",
            i, sz_o, sz_c, rc, e->ratio_pct, e->path);
    }

    ql_list_result_free(&res);
    return 0;
}
