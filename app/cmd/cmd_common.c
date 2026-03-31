/*
 * cmd_common.c - Shared utility functions for CLI commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quadr.h"
#include "quadr_console.h"
#include "cmd_common.h"

/* ─── Backend Names ─────────────────────────────────────────────────────── */

static const char *backend_names[] = {
    [BACKEND_NONE]   = "none",
    [BACKEND_ZLIB]   = "zlib-ng",
    [BACKEND_ZSTD]   = "zstd",
    [BACKEND_LZ4]    = "lz4",
    [BACKEND_LZ4HC]  = "lz4hc",
    [BACKEND_7Z]     = "7z",
};

const char *backend_name(uint8_t b) {
    if (b < 6) return backend_names[b];
    return "unknown";
}

/* ─── Backend Default Levels ────────────────────────────────────────────── */

int backend_default_level(uint8_t b) {
    switch (b) {
        case BACKEND_LZ4:    return 0;
        case BACKEND_LZ4HC:  return 9;
        case BACKEND_ZLIB:   return 6;
        case BACKEND_ZSTD:   return 3;
        case BACKEND_7Z:     return 6;
        default:             return 0;
    }
}

/* ─── Effective Level ───────────────────────────────────────────────────── */

int effective_level(const EncodeConfig *c) {
    return (c->level >= 0) ? c->level : backend_default_level(c->backend);
}

/* ─── Encode Config Defaults ────────────────────────────────────────────── */

void encode_config_default(EncodeConfig *c) {
    quadr_encode_opts_default(&c->quadr);
    c->backend        = BACKEND_NONE;
    c->level          = -1;
    c->use_fast_probe = 1;
    c->mixed_backend  = 0;
    c->parallel       = 0;
    c->num_threads    = 0;
}

/* ─── File I/O ──────────────────────────────────────────────────────────── */

uint8_t *read_file(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); *len = 0; return calloc(1,1); }
    uint8_t *b = malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz)
        { free(b); fclose(f); return NULL; }
    fclose(f); *len = (size_t)sz; return b;
}

int write_file(const char *p, const uint8_t *b, size_t n) {
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); return -1; }
    int ok = (fwrite(b, 1, n, f) == n);
    fclose(f); return ok ? 0 : -1;
}

/* ─── Human-Readable Size ───────────────────────────────────────────────── */

void print_size_human(uint64_t size, char *buf, size_t buf_size) {
    if (size >= 1073741824ULL)
        snprintf(buf, buf_size, "%.2f GB", (double)size / 1073741824.0);
    else if (size >= 1048576ULL)
        snprintf(buf, buf_size, "%.2f MB", (double)size / 1048576.0);
    else if (size >= 1024ULL)
        snprintf(buf, buf_size, "%.2f KB", (double)size / 1024.0);
    else
        snprintf(buf, buf_size, "%llu B", (unsigned long long)size);
}

/* ─── Probe Shim ────────────────────────────────────────────────────────── */

QuadrProbeResult do_probe(const uint8_t *data, size_t len, const EncodeConfig *cfg) {
    return cfg->use_fast_probe
           ? quadr_probe_fast(data, len, &cfg->quadr)
           : quadr_probe(data, len, &cfg->quadr);
}

/* ─── Option Parsing ────────────────────────────────────────────────────── */

int parse_hint(const char *s, uint8_t *o) {
    switch (s[0]) {
        case 'g': if (!strcmp(s, "generic"))  { *o = QUADR_HINT_GENERIC;  return 0; } break;
        case 'i': if (!strcmp(s, "image"))    { *o = QUADR_HINT_IMAGE;    return 0; } break;
        case 'a': if (!strcmp(s, "audio"))    { *o = QUADR_HINT_AUDIO_PCM;return 0; } break;
        case 's': if (!strcmp(s, "sensor"))   { *o = QUADR_HINT_SENSOR;   return 0; } break;
        case 'f': if (!strcmp(s, "float"))    { *o = QUADR_HINT_FLOAT;    return 0; } break;
    }
    return -1;
}

int parse_backend(const char *s, Backend *o) {
    switch (s[0]) {
        case 'n': if (!strcmp(s, "none")) { *o = BACKEND_NONE; return 0; } break;
        case 'z':
            if (!strcmp(s, "zlib") || !strcmp(s, "zlib-ng") || !strcmp(s, "zlibng"))
                { *o = BACKEND_ZLIB; return 0; }
            if (!strcmp(s, "zstd")) { *o = BACKEND_ZSTD; return 0; }
            break;
        case 'l':
            if (!strcmp(s, "lz4"))  { *o = BACKEND_LZ4;  return 0; }
            if (!strcmp(s, "lz4hc")){ *o = BACKEND_LZ4HC;return 0; }
            break;
        case '7': if (!strcmp(s, "7z")) { *o = BACKEND_7Z; return 0; } break;
    }
    return -1;
}
