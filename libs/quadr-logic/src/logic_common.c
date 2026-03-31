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
