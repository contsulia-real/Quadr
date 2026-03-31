/*
 * cmd_help.c - Help, usage, and version display
 */

#include "cmd_help.h"
#include "cmd_common.h"
#include "quadr_version.h"

#ifndef QUADR_PROJECT_NAME
#define QUADR_PROJECT_NAME "quadr"
#endif

/* ──────────────────────────────────────────────────────────────────────────
 * version command / --version  →  version + backends
 * ────────────────────────────────────────────────────────────────────────── */

void show_version(void) {
    con_section(QUADR_PROJECT_NAME);
    con_kv("version", "%s (%s)", QUADR_VERSION_STRING,
           quadr_simd_level_name(quadr_simd_level()));

    con_nl();
    con_section("Built-in Backends");

    size_t n = quadr_backend_count();
    for (size_t i = 0; i < n; i++) {
        const QuadrBackend *bk = quadr_backend_get(i);
        if (!bk) continue;
        con_kv(bk->name, "id=%u  default_level=%d", bk->id, bk->default_level);
    }
    con_nl();
}

/* ──────────────────────────────────────────────────────────────────────────
 * No-args short usage  →  command list only  (no version / backends)
 * ────────────────────────────────────────────────────────────────────────── */

void usage_short(const char *p) {
    const char *prog = p;
    for (const char *s = p; *s; s++)
        if (*s == '/' || *s == '\\') prog = s + 1;

    con_section("Usage");
    con_table_row("  %s <command> [arguments...]", prog);

    con_nl();
    con_section("Commands");
    con_table_row("  " CON_BOLD "encode" CON_RESET " [opts] <in> <out>   Compress a file");
    con_table_row("  " CON_BOLD "decode" CON_RESET " [opts] <in> <out>   Decompress a Quadr file");
    con_table_row("  " CON_BOLD "info" CON_RESET " <file>                Show Quadr file metadata");
    con_table_row("  " CON_BOLD "bench" CON_RESET " <file>               Benchmark probe/encode/decode");
    con_table_row("  " CON_BOLD "verify" CON_RESET " <file>              Verify a Quadr file or archive");
    con_table_row("  " CON_BOLD "probe" CON_RESET " [opts] <file>        Show per-block classification");
    con_table_row("  " CON_BOLD "pack" CON_RESET " [opts] -o <out> <..>  Pack files into a .qar archive");
    con_table_row("  " CON_BOLD "unpack" CON_RESET " [opts] <archive>    Extract a .qar archive");
    con_table_row("  " CON_BOLD "list" CON_RESET " <archive>             List archive contents");
    con_table_row("  " CON_BOLD "version" CON_RESET "                    Show version and backends");
    con_table_row("  " CON_BOLD "help" CON_RESET "                       Show this help message");

    con_nl();
    con_table_row("Run '%s <command> --help' for more information.", prog);
}

/* ──────────────────────────────────────────────────────────────────────────
 * help command / --help  →  full help  (no version / backends)
 * ────────────────────────────────────────────────────────────────────────── */

void usage_full(const char *p) {
    const char *prog = p;
    for (const char *s = p; *s; s++)
        if (*s == '/' || *s == '\\') prog = s + 1;

    con_section("Quadr - Lossless Data Compression Tool");

    con_nl();
    con_section("Commands");
    con_table_row("  " CON_BOLD "encode" CON_RESET " [opts] <in> <out>    Compress a file with configurable backend");
    con_table_row("  " CON_BOLD "decode" CON_RESET " [opts] <in> <out>    Decompress a Quadr file");
    con_table_row("  " CON_BOLD "info" CON_RESET " <file>                 Show Quadr file metadata");
    con_table_row("  " CON_BOLD "bench" CON_RESET " <file>                Benchmark probe/encode/decode on one block");
    con_table_row("  " CON_BOLD "verify" CON_RESET " <file>               Verify integrity of a Quadr file or archive");
    con_table_row("  " CON_BOLD "probe" CON_RESET " [opts] <file>         Show per-block type classification");
    con_table_row("  " CON_BOLD "pack" CON_RESET " [opts] -o <out> <..>  Pack files into a .qar archive");
    con_table_row("  " CON_BOLD "unpack" CON_RESET " [opts] <archive>     Extract a .qar archive");
    con_table_row("  " CON_BOLD "list" CON_RESET " <archive>              List contents of a .qar archive");
    con_table_row("  " CON_BOLD "version" CON_RESET "                     Show version and built-in backends");
    con_table_row("  " CON_BOLD "help" CON_RESET "                        Show this help message");

    con_nl();
    con_section("Encode Options");
    con_table_row("  " CON_BOLD "--hint" CON_RESET "=<type>            Data type hint: generic, image, audio, sensor, float");
    con_table_row("  " CON_BOLD "--block" CON_RESET "=<KB>             Block size in KB (default: 64)");
    con_table_row("  " CON_BOLD "--xbit" CON_RESET "=<8|16|32|64>      Sample bit width (default: 8)");
    con_table_row("  " CON_BOLD "--stride" CON_RESET "=<N>              Extra stride hint (default: auto)");
    con_table_row("  " CON_BOLD "--backend" CON_RESET "=<name>           Compression backend: none, zlib-ng, zstd, lz4, lz4hc, 7z");
    con_table_row("  " CON_BOLD "--level" CON_RESET "=<N>               Backend compression level (default: per-backend)");
    con_table_row("  " CON_BOLD "--fast" CON_RESET " / " CON_BOLD "--no-fast" CON_RESET "         Use fast or full probe (default: fast)");
    con_table_row("  " CON_BOLD "--mixed-backend" CON_RESET "            DELTA blocks use default backend, PASSTHROUGH uses lz4");
    con_table_row("  " CON_BOLD "--adaptive-block" CON_RESET "           Auto-select block size from data characteristics");
    con_table_row("  " CON_BOLD "--parallel" CON_RESET "                 Enable parallel encoding");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Number of threads for parallel mode (default: auto)");
    con_table_row("  " CON_BOLD "--auto" CON_RESET "[=ratio|balance|speed]  Auto-detect file type and choose optimal config");

    con_nl();
    con_section("Decode Options");
    con_table_row("  " CON_BOLD "--parallel" CON_RESET "                 Enable parallel decoding (future)");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Thread count (future)");

    con_nl();
    con_section("Pack Options");
    con_table_row("  " CON_BOLD "-o" CON_RESET " <output.qar>          Output archive path (required)");
    con_table_row("  " CON_BOLD "--backend" CON_RESET "=<name>           Archive compression backend");
    con_table_row("  " CON_BOLD "--level" CON_RESET "=<N>               Compression level");
    con_table_row("  " CON_BOLD "--block" CON_RESET "=<KB>             Block size");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Thread count");
    con_table_row("  " CON_BOLD "--no-paths" CON_RESET "                 Don't store directory paths in archive");
    con_table_row("  " CON_BOLD "--base-dir" CON_RESET "=<dir>            Base directory for stored paths");

    con_nl();
    con_section("Unpack Options");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Thread count for parallel extraction");
    con_table_row("  " CON_BOLD "--file" CON_RESET "=<index>             Extract only one file by index");

    con_nl();
    con_section("Global Options");
    con_table_row("  " CON_BOLD "--help" CON_RESET " / " CON_BOLD "-h" CON_RESET "             Show this help message");
    con_table_row("  " CON_BOLD "--version" CON_RESET " / " CON_BOLD "-V" CON_RESET "         Show version and backends");

    con_nl();
    con_section("Examples");
    con_table_row("  %s encode --backend=zstd --level=9 input.bin output.qdr", prog);
    con_table_row("  %s decode output.qdr restored.bin", prog);
    con_table_row("  %s info output.qdr", prog);
    con_table_row("  %s probe --hint=image photo.raw", prog);
    con_table_row("  %s pack -o archive.qar file1.bin file2.bin", prog);
    con_table_row("  %s unpack archive.qar ./output/", prog);
    con_table_row("  %s list archive.qar", prog);

    con_nl();
}

/* ──────────────────────────────────────────────────────────────────────────
 * Per-command --help
 * ────────────────────────────────────────────────────────────────────────── */

void help_encode(const char *prog) {
    con_section("encode - Compress a file");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s encode [options] <input> <output>", prog);

    con_nl();
    con_section("Options");
    con_table_row("  " CON_BOLD "--hint" CON_RESET "=<type>            Data type hint: generic, image, audio, sensor, float");
    con_table_row("  " CON_BOLD "--block" CON_RESET "=<KB>             Block size in KB (default: 64)");
    con_table_row("  " CON_BOLD "--xbit" CON_RESET "=<8|16|32|64>      Sample bit width (default: 8)");
    con_table_row("  " CON_BOLD "--stride" CON_RESET "=<N>              Extra stride hint (default: auto)");
    con_table_row("  " CON_BOLD "--backend" CON_RESET "=<name>           Compression backend: none, zlib-ng, zstd, lz4, lz4hc, 7z");
    con_table_row("  " CON_BOLD "--level" CON_RESET "=<N>               Backend compression level (default: per-backend)");
    con_table_row("  " CON_BOLD "--fast" CON_RESET " / " CON_BOLD "--no-fast" CON_RESET "         Use fast or full probe (default: fast)");
    con_table_row("  " CON_BOLD "--mixed-backend" CON_RESET "            DELTA→default backend, PASSTHROUGH→lz4");
    con_table_row("  " CON_BOLD "--adaptive-block" CON_RESET "           Auto-select block size from data characteristics");
    con_table_row("  " CON_BOLD "--parallel" CON_RESET "                 Enable parallel encoding");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Number of threads (default: auto)");
    con_table_row("  " CON_BOLD "--auto" CON_RESET "[=ratio|balance|speed]  Auto-detect file type and choose optimal config");

    con_nl();
    con_section("Examples");
    con_table_row("  %s encode --backend=zstd --level=9 input.bin output.qdr", prog);
    con_table_row("  %s encode --hint=float --xbit=32 sensor.raw output.qdr", prog);
    con_table_row("  %s encode --parallel --threads=4 bigfile.bin output.qdr", prog);
    con_nl();
}

void help_decode(const char *prog) {
    con_section("decode - Decompress a Quadr file");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s decode [options] <input> <output>", prog);

    con_nl();
    con_section("Options");
    con_table_row("  " CON_BOLD "--parallel" CON_RESET "                 Enable parallel decoding (future)");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Thread count (future)");

    con_nl();
    con_section("Examples");
    con_table_row("  %s decode output.qdr restored.bin", prog);
    con_nl();
}

void help_info(const char *prog) {
    con_section("info - Show Quadr file metadata");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s info <file>", prog);

    con_nl();
    con_section("Description");
    con_table_row("  Displays file version, original size, compressed size,");
    con_table_row("  block count, block type distribution, and backend usage.");

    con_nl();
    con_section("Examples");
    con_table_row("  %s info output.qdr", prog);
    con_nl();
}

void help_bench(const char *prog) {
    con_section("bench - Benchmark probe/encode/decode");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s bench <file>", prog);

    con_nl();
    con_section("Description");
    con_table_row("  Runs 5 iterations of fast probe, full probe, encode,");
    con_table_row("  and decode on the first block of the file. Reports");
    con_table_row("  best times and throughput for each operation.");

    con_nl();
    con_section("Examples");
    con_table_row("  %s bench input.bin", prog);
    con_nl();
}

void help_verify(const char *prog) {
    con_section("verify - Verify file or archive integrity");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s verify <file>", prog);
    con_table_row("    %s verify <archive.qar>", prog);

    con_nl();
    con_section("Description");
    con_table_row("  Decodes every block and checks XXH3-64 hashes.");
    con_table_row("  For .qar archives, verifies every contained file.");

    con_nl();
    con_section("Examples");
    con_table_row("  %s verify output.qdr", prog);
    con_table_row("  %s verify archive.qar", prog);
    con_nl();
}

void help_probe(const char *prog) {
    con_section("probe - Show per-block type classification");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s probe [options] <file>", prog);

    con_nl();
    con_section("Options");
    con_table_row("  " CON_BOLD "--hint" CON_RESET "=<type>            Data type hint: generic, image, audio, sensor, float");
    con_table_row("  " CON_BOLD "--block" CON_RESET "=<KB>             Block size in KB (default: 64)");
    con_table_row("  " CON_BOLD "--xbit" CON_RESET "=<8|16|32|64>      Sample bit width (default: 8)");
    con_table_row("  " CON_BOLD "--stride" CON_RESET "=<N>              Extra stride hint (default: auto)");
    con_table_row("  " CON_BOLD "--fast" CON_RESET " / " CON_BOLD "--no-fast" CON_RESET "         Use fast or full probe (default: fast)");

    con_nl();
    con_section("Examples");
    con_table_row("  %s probe --hint=image photo.raw", prog);
    con_table_row("  %s probe --xbit=32 --hint=float audio.pcm", prog);
    con_nl();
}

void help_pack(const char *prog) {
    con_section("pack - Pack files into a .qar archive");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s pack [options] -o <output.qar> <files...>", prog);

    con_nl();
    con_section("Options");
    con_table_row("  " CON_BOLD "-o" CON_RESET " <output.qar>          Output archive path (required)");
    con_table_row("  " CON_BOLD "--backend" CON_RESET "=<name>           Archive compression backend");
    con_table_row("  " CON_BOLD "--level" CON_RESET "=<N>               Compression level");
    con_table_row("  " CON_BOLD "--block" CON_RESET "=<KB>             Block size");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Thread count");
    con_table_row("  " CON_BOLD "--no-paths" CON_RESET "                 Don't store directory paths");
    con_table_row("  " CON_BOLD "--base-dir" CON_RESET "=<dir>            Base directory for stored paths");

    con_nl();
    con_section("Examples");
    con_table_row("  %s pack -o archive.qar file1.bin file2.bin", prog);
    con_table_row("  %s pack --backend=zstd --level=9 -o archive.qar dir/", prog);
    con_table_row("  %s pack --no-paths -o archive.qar *.bin", prog);
    con_nl();
}

void help_unpack(const char *prog) {
    con_section("unpack - Extract a .qar archive");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s unpack [options] <archive.qar> [output_dir]", prog);

    con_nl();
    con_section("Options");
    con_table_row("  " CON_BOLD "--threads" CON_RESET "=<N>              Thread count for parallel extraction");
    con_table_row("  " CON_BOLD "--file" CON_RESET "=<index>             Extract only one file by index");

    con_nl();
    con_section("Examples");
    con_table_row("  %s unpack archive.qar", prog);
    con_table_row("  %s unpack archive.qar ./output/", prog);
    con_table_row("  %s unpack --file=0 archive.qar", prog);
    con_nl();
}

void help_list(const char *prog) {
    con_section("list - List archive contents");
    con_nl();
    con_table_row("  " CON_BOLD "Usage:" CON_RESET);
    con_table_row("    %s list <archive.qar>", prog);

    con_nl();
    con_section("Description");
    con_table_row("  Shows file index, original size, compressed size,");
    con_table_row("  compression ratio, and path for each entry.");

    con_nl();
    con_section("Examples");
    con_table_row("  %s list archive.qar", prog);
    con_nl();
}

void help_version(const char *prog) {
    (void)prog;
    show_version();
}

void help_help(const char *prog) {
    usage_full(prog);
}

/* ──────────────────────────────────────────────────────────────────────────
 * Dispatch table
 * ────────────────────────────────────────────────────────────────────────── */

int cmd_help_for(const char *cmd, const char *prog) {
    if (!cmd) { usage_full(prog); return 0; }

    if (!strcmp(cmd, "encode"))  { help_encode(prog);  return 0; }
    if (!strcmp(cmd, "decode"))  { help_decode(prog);  return 0; }
    if (!strcmp(cmd, "info"))    { help_info(prog);    return 0; }
    if (!strcmp(cmd, "bench"))   { help_bench(prog);   return 0; }
    if (!strcmp(cmd, "verify"))  { help_verify(prog);  return 0; }
    if (!strcmp(cmd, "probe"))   { help_probe(prog);   return 0; }
    if (!strcmp(cmd, "pack"))    { help_pack(prog);    return 0; }
    if (!strcmp(cmd, "unpack"))  { help_unpack(prog);  return 0; }
    if (!strcmp(cmd, "list"))    { help_list(prog);    return 0; }
    if (!strcmp(cmd, "version")) { help_version(prog); return 0; }
    if (!strcmp(cmd, "help"))    { help_help(prog);    return 0; }

    con_error("unknown command '%s'", cmd);
    usage_short(prog);
    return 1;
}

int cmd_help(const char *prog) {
    usage_full(prog);
    return 0;
}

int is_known_global_option(const char *arg) {
    return (!strcmp(arg, "--help") || !strcmp(arg, "-h") ||
            !strcmp(arg, "--version") || !strcmp(arg, "-V"));
}
