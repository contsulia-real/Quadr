#include "quadr_logic_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ql_backend_name(QLBackend b) {
    static const char *names[] = {
        [QL_BACKEND_NONE]  = "none",
        [QL_BACKEND_ZLIB]  = "zlib-ng",
        [QL_BACKEND_ZSTD]  = "zstd",
        [QL_BACKEND_LZ4]   = "lz4",
        [QL_BACKEND_LZ4HC] = "lz4hc",
        [QL_BACKEND_7Z]    = "7z",
    };
    if (b < 6) return names[b];
    return "unknown";
}

int ql_backend_default_level(QLBackend b) {
    switch (b) {
        case QL_BACKEND_LZ4:    return 0;
        case QL_BACKEND_LZ4HC:  return 9;
        case QL_BACKEND_ZLIB:   return 6;
        case QL_BACKEND_ZSTD:   return 3;
        case QL_BACKEND_7Z:     return 6;
        default:                return 0;
    }
}

int ql_effective_level(const QLEncodeConfig *c) {
    return (c->level >= 0) ? c->level : ql_backend_default_level(c->backend);
}

void ql_encode_config_default(QLEncodeConfig *c) {
    memset(c, 0, sizeof(*c));
    quadr_encode_opts_default(&c->quadr);
    c->backend = QL_BACKEND_ZSTD;
    c->level = -1;
    c->use_fast_probe = 1;
}

uint8_t *ql_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); *len = 0; return (uint8_t *)calloc(1, 1); }
    uint8_t *b = (uint8_t *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) {
        free(b); fclose(f); return NULL;
    }
    fclose(f);
    *len = (size_t)sz;
    return b;
}

int ql_write_file(const char *path, const uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = (fwrite(buf, 1, len, f) == len) ? 0 : -1;
    fclose(f);
    return ok;
}

void ql_print_size_human(uint64_t size, char *buf, size_t buf_size) {
    if (size >= 1073741824ULL)
        snprintf(buf, buf_size, "%.2f GB", (double)size / 1073741824.0);
    else if (size >= 1048576ULL)
        snprintf(buf, buf_size, "%.2f MB", (double)size / 1048576.0);
    else if (size >= 1024ULL)
        snprintf(buf, buf_size, "%.2f KB", (double)size / 1024.0);
    else
        snprintf(buf, buf_size, "%llu B", (unsigned long long)size);
}

int ql_parse_hint(const char *s, uint8_t *out) {
    if (!strcmp(s, "generic"))  { *out = QUADR_HINT_GENERIC;  return 0; }
    if (!strcmp(s, "image"))    { *out = QUADR_HINT_IMAGE;    return 0; }
    if (!strcmp(s, "audio"))    { *out = QUADR_HINT_AUDIO_PCM;return 0; }
    if (!strcmp(s, "sensor"))   { *out = QUADR_HINT_SENSOR;   return 0; }
    if (!strcmp(s, "float"))    { *out = QUADR_HINT_FLOAT;    return 0; }
    return -1;
}

int ql_parse_backend(const char *s, QLBackend *out) {
    if (!strcmp(s, "none") || !strcmp(s, "n")) { *out = QL_BACKEND_NONE; return 0; }
    if (!strcmp(s, "zlib") || !strcmp(s, "zlib-ng") || !strcmp(s, "zlibng") || !strcmp(s, "z"))
        { *out = QL_BACKEND_ZLIB; return 0; }
    if (!strcmp(s, "zstd")) { *out = QL_BACKEND_ZSTD; return 0; }
    if (!strcmp(s, "lz4") || !strcmp(s, "l")) { *out = QL_BACKEND_LZ4; return 0; }
    if (!strcmp(s, "lz4hc")) { *out = QL_BACKEND_LZ4HC; return 0; }
    if (!strcmp(s, "7z")) { *out = QL_BACKEND_7Z; return 0; }
    return -1;
}

void ql_probe_result_free(QLProbeResult *r) {
    if (r && r->blocks) {
        free(r->blocks);
        r->blocks = NULL;
    }
}

void ql_list_result_free(QLListResult *r) {
    if (r && r->entries) {
        free(r->entries);
        r->entries = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * File Type Detection
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const uint8_t *magic;
    size_t         magic_len;
    size_t         offset;
    QLFileType     type;
    const char    *mime;
    const char    *description;
} MagicEntry;

static const uint8_t MAGIC_PNG[]    = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t MAGIC_JPEG[]   = {0xFF, 0xD8, 0xFF};
static const uint8_t MAGIC_GIF89[]  = {'G', 'I', 'F', '8', '9', 'a'};
static const uint8_t MAGIC_GIF87[]  = {'G', 'I', 'F', '8', '7', 'a'};
static const uint8_t MAGIC_BMP[]    = {'B', 'M'};
static const uint8_t MAGIC_WEBP[]   = {'R', 'I', 'F', 'F'};
static const uint8_t MAGIC_ZIP[]    = {'P', 'K', 0x03, 0x04};
static const uint8_t MAGIC_GZIP[]   = {0x1F, 0x8B};
static const uint8_t MAGIC_7Z[]     = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
static const uint8_t MAGIC_PDF[]    = {'%', 'P', 'D', 'F'};
static const uint8_t MAGIC_MP3[]    = {0xFF, 0xFB};
static const uint8_t MAGIC_FLAC[]   = {'f', 'L', 'a', 'C'};
static const uint8_t MAGIC_WAV[]    = {'R', 'I', 'F', 'F'};
static const uint8_t MAGIC_MP4[]    = {0, 0, 0, 0, 'f', 't', 'y', 'p'};
static const uint8_t MAGIC_AVIF[]   = {0, 0, 0, 0, 'f', 't', 'y', 'p'};

static const MagicEntry magic_table[] = {
    {MAGIC_PNG,    sizeof(MAGIC_PNG),    0,  QL_FILETYPE_IMAGE,      "image/png",       "PNG image"},
    {MAGIC_JPEG,   sizeof(MAGIC_JPEG),   0,  QL_FILETYPE_IMAGE,      "image/jpeg",      "JPEG image"},
    {MAGIC_GIF89,  sizeof(MAGIC_GIF89),  0,  QL_FILETYPE_IMAGE,      "image/gif",       "GIF 89a image"},
    {MAGIC_GIF87,  sizeof(MAGIC_GIF87),  0,  QL_FILETYPE_IMAGE,      "image/gif",       "GIF 87a image"},
    {MAGIC_BMP,    sizeof(MAGIC_BMP),    0,  QL_FILETYPE_IMAGE,      "image/bmp",       "BMP image"},
    {MAGIC_WEBP,   4,                    0,  QL_FILETYPE_IMAGE,      "image/webp",      "WebP image"},
    {MAGIC_ZIP,    sizeof(MAGIC_ZIP),    0,  QL_FILETYPE_COMPRESSED, "application/zip", "ZIP archive"},
    {MAGIC_GZIP,   sizeof(MAGIC_GZIP),   0,  QL_FILETYPE_COMPRESSED, "application/gzip","Gzip archive"},
    {MAGIC_7Z,     sizeof(MAGIC_7Z),     0,  QL_FILETYPE_COMPRESSED, "application/x-7z-compressed", "7-Zip archive"},
    {MAGIC_PDF,    sizeof(MAGIC_PDF),    0,  QL_FILETYPE_BINARY,     "application/pdf", "PDF document"},
    {MAGIC_MP3,    sizeof(MAGIC_MP3),    0,  QL_FILETYPE_AUDIO,      "audio/mpeg",      "MP3 audio"},
    {MAGIC_FLAC,   sizeof(MAGIC_FLAC),   0,  QL_FILETYPE_AUDIO,      "audio/flac",      "FLAC audio"},
    {MAGIC_WAV,    4,                    0,  QL_FILETYPE_AUDIO,      "audio/wav",       "WAV audio"},
    {MAGIC_MP4,    4,                    4,  QL_FILETYPE_VIDEO,      "video/mp4",       "MP4 video"},
    {MAGIC_AVIF,   4,                    4,  QL_FILETYPE_IMAGE,      "image/avif",      "AVIF image"},
};

#define N_MAGIC (sizeof(magic_table) / sizeof(magic_table[0]))

static double ascii_ratio(const uint8_t *data, size_t len) {
    if (!len) return 0.0;
    size_t ascii = 0;
    size_t check = len < 4096 ? len : 4096;
    for (size_t i = 0; i < check; i++) {
        uint8_t b = data[i];
        if (b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E))
            ascii++;
    }
    return (double)ascii / (double)check;
}

static double newline_ratio(const uint8_t *data, size_t len) {
    if (!len) return 0.0;
    size_t nl = 0;
    size_t check = len < 4096 ? len : 4096;
    for (size_t i = 0; i < check; i++)
        if (data[i] == '\n' || data[i] == '\r') nl++;
    return (double)nl / (double)check;
}

static int is_text_extension(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;
    ext++;
    static const char *text_exts[] = {
        "txt", "html", "htm", "xml", "json", "csv", "md", "rst",
        "c", "h", "cpp", "hpp", "cc", "cxx", "java", "py", "rb",
        "js", "ts", "css", "scss", "less", "sql", "sh", "bash",
        "yaml", "yml", "toml", "ini", "cfg", "conf", "log",
        "tex", "latex", "r", "m", "go", "rs", "swift", "kt",
        "php", "pl", "lua", "vim", "el", "hs", "ml", "fs",
        "diff", "patch", "asm", "s", "S", "Makefile", "CMakeLists",
        "gradle", "properties", "pom", "xsl", "xslt", "svg",
        NULL
    };
    for (int i = 0; text_exts[i]; i++)
        if (strcmp(ext, text_exts[i]) == 0) return 1;
    return 0;
}

QLFileTypeResult ql_detect_file_type_from_data(const uint8_t *data, size_t len) {
    QLFileTypeResult res = {QL_FILETYPE_UNKNOWN, "application/octet-stream", "unknown binary", 0, 0};

    if (!data || len < 4) return res;

    for (size_t i = 0; i < N_MAGIC; i++) {
        const MagicEntry *m = &magic_table[i];
        if (len < m->offset + m->magic_len) continue;
        int match = 1;
        for (size_t j = 0; j < m->magic_len; j++) {
            if (data[m->offset + j] != m->magic[j]) { match = 0; break; }
        }
        if (match) {
            res.type = m->type;
            res.mime = m->mime;
            res.description = m->description;
            res.is_text = 0;
            res.already_compressed = (m->type == QL_FILETYPE_COMPRESSED ||
                                       m->type == QL_FILETYPE_IMAGE ||
                                       m->type == QL_FILETYPE_VIDEO);
            return res;
        }
    }

    double ascii = ascii_ratio(data, len);
    double nl = newline_ratio(data, len);

    if (ascii > 0.90 && nl > 0.01) {
        res.type = QL_FILETYPE_TEXT;
        res.mime = "text/plain";
        res.description = "text file";
        res.is_text = 1;
        res.already_compressed = 0;
    } else if (ascii > 0.80 && nl > 0.005) {
        res.type = QL_FILETYPE_TEXT;
        res.mime = "text/plain";
        res.description = "text file (with some binary)";
        res.is_text = 1;
        res.already_compressed = 0;
    } else if (ascii < 0.30) {
        res.type = QL_FILETYPE_BINARY;
        res.mime = "application/octet-stream";
        res.description = "binary data";
        res.is_text = 0;
        res.already_compressed = 0;
    } else {
        res.type = QL_FILETYPE_BINARY;
        res.mime = "application/octet-stream";
        res.description = "mixed content";
        res.is_text = 0;
        res.already_compressed = 0;
    }

    return res;
}

QLFileTypeResult ql_detect_file_type(const char *path) {
    QLFileTypeResult res = {QL_FILETYPE_UNKNOWN, "application/octet-stream", "unknown", 0, 0};

    if (is_text_extension(path)) {
        res.type = QL_FILETYPE_TEXT;
        res.mime = "text/plain";
        res.description = "text file (by extension)";
        res.is_text = 1;
        res.already_compressed = 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return res;

    uint8_t buf[4096];
    size_t got = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    if (got < 4) return res;

    res = ql_detect_file_type_from_data(buf, got);
    return res;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Auto Configuration — Smart 3-mode engine
 * ═══════════════════════════════════════════════════════════════════════════ */

int ql_parse_auto_mode(const char *s, int *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "ratio") == 0 || strcmp(s, "max") == 0) { *out = QL_AUTO_RATIO; return 0; }
    if (strcmp(s, "speed") == 0 || strcmp(s, "fast") == 0) { *out = QL_AUTO_SPEED; return 0; }
    if (strcmp(s, "balance") == 0 || strcmp(s, "default") == 0) { *out = QL_AUTO_BALANCE; return 0; }
    return -1;
}

void ql_auto_configure(QLEncodeConfig *cfg, const char *in_path) {
    if (!cfg || !in_path) return;

    QLFileTypeResult ft = ql_detect_file_type(in_path);
    int mode = cfg->auto_mode; /* 0=balance, 1=ratio, 2=speed */

    uint64_t file_size = 0;
    FILE *f = fopen(in_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        file_size = (uint64_t)ftell(f);
        fclose(f);
    }

    uint32_t block_small = (mode == QL_AUTO_SPEED) ? 64 * 1024 : 16 * 1024;
    uint32_t block_medium = (mode == QL_AUTO_SPEED) ? 64 * 1024 : 32 * 1024;
    uint32_t block_large = 64 * 1024;

    switch (ft.type) {
    case QL_FILETYPE_TEXT:
        if (mode == QL_AUTO_RATIO) {
            if (file_size < 64 * 1024)        cfg->level = 19;
            else if (file_size < 1024*1024)    cfg->level = 15;
            else if (file_size < 10*1024*1024) cfg->level = 10;
            else                               cfg->level = 6;
        } else if (mode == QL_AUTO_BALANCE) {
            if (file_size < 64 * 1024)        cfg->level = 9;
            else if (file_size < 1024*1024)    cfg->level = 6;
            else if (file_size < 10*1024*1024) cfg->level = 3;
            else                               cfg->level = 1;
        } else {
            cfg->level = 0;
        }
        cfg->backend = (mode == QL_AUTO_SPEED) ? QL_BACKEND_LZ4 : QL_BACKEND_ZSTD;
        cfg->quadr.block_size = (file_size < 1024*1024) ? block_small : block_large;
        cfg->quadr.data_hint = QUADR_HINT_GENERIC;
        break;

    case QL_FILETYPE_IMAGE:
        if (ft.mime && (strcmp(ft.mime, "image/png") == 0 ||
                        strcmp(ft.mime, "image/bmp") == 0)) {
            if (mode == QL_AUTO_RATIO) {
                cfg->backend = QL_BACKEND_ZSTD;
                cfg->level = 9;
            } else if (mode == QL_AUTO_BALANCE) {
                cfg->backend = QL_BACKEND_ZSTD;
                cfg->level = 3;
            } else {
                cfg->backend = QL_BACKEND_LZ4;
                cfg->level = 0;
            }
        } else {
            cfg->backend = QL_BACKEND_LZ4;
            cfg->level = 0;
        }
        cfg->quadr.block_size = block_medium;
        cfg->quadr.data_hint = QUADR_HINT_IMAGE;
        break;

    case QL_FILETYPE_AUDIO:
        if (mode == QL_AUTO_RATIO) {
            cfg->backend = QL_BACKEND_ZSTD;
            cfg->level = 9;
        } else if (mode == QL_AUTO_BALANCE) {
            cfg->backend = QL_BACKEND_ZSTD;
            cfg->level = 6;
        } else {
            cfg->backend = QL_BACKEND_LZ4;
            cfg->level = 0;
        }
        cfg->quadr.block_size = block_medium;
        cfg->quadr.data_hint = QUADR_HINT_AUDIO_PCM;
        break;

    case QL_FILETYPE_VIDEO:
        cfg->backend = (mode == QL_AUTO_RATIO) ? QL_BACKEND_ZSTD : QL_BACKEND_LZ4;
        cfg->level = (mode == QL_AUTO_RATIO) ? 3 : 0;
        cfg->quadr.block_size = block_large;
        cfg->quadr.data_hint = QUADR_HINT_GENERIC;
        break;

    case QL_FILETYPE_COMPRESSED:
        cfg->backend = QL_BACKEND_NONE;
        cfg->level = 0;
        cfg->quadr.block_size = block_large;
        cfg->quadr.data_hint = QUADR_HINT_GENERIC;
        break;

    case QL_FILETYPE_BINARY:
        if (mode == QL_AUTO_RATIO) {
            cfg->backend = QL_BACKEND_ZSTD;
            cfg->level = 9;
        } else if (mode == QL_AUTO_BALANCE) {
            cfg->backend = QL_BACKEND_ZSTD;
            cfg->level = 3;
        } else {
            cfg->backend = QL_BACKEND_LZ4;
            cfg->level = 0;
        }
        cfg->quadr.block_size = block_large;
        cfg->quadr.data_hint = QUADR_HINT_GENERIC;
        break;

    default:
    case QL_FILETYPE_UNKNOWN:
        if (mode == QL_AUTO_RATIO) {
            cfg->backend = QL_BACKEND_ZSTD;
            cfg->level = 6;
        } else if (mode == QL_AUTO_BALANCE) {
            cfg->backend = QL_BACKEND_ZSTD;
            cfg->level = 3;
        } else {
            cfg->backend = QL_BACKEND_LZ4;
            cfg->level = 0;
        }
        cfg->quadr.block_size = block_medium;
        cfg->quadr.data_hint = QUADR_HINT_GENERIC;
        break;
    }
}
