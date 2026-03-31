#ifndef CMD_COMMON_H
#define CMD_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quadr_platform.h"
#include "quadr.h"
#include "quadr_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Backend Types ───────────────────────────────────────────────────────── */

typedef enum {
    BACKEND_NONE = 0,
    BACKEND_ZLIB = 1,
    BACKEND_ZSTD = 2,
    BACKEND_LZ4 = 3,
    BACKEND_LZ4HC = 4,
    BACKEND_7Z = 5,
} Backend;

/* ─── Encode Configuration ───────────────────────────────────────────────── */

typedef struct {
    QuadrEncodeOpts quadr;
    uint8_t backend;
    int level;
    int use_fast_probe;
    int mixed_backend;
    int parallel;
    int num_threads;
} EncodeConfig;

/* ─── Helpers ─────────────────────────────────────────────────────────────── */

const char *backend_name(uint8_t b);
int backend_default_level(uint8_t b);
int effective_level(const EncodeConfig *c);
void encode_config_default(EncodeConfig *c);
uint8_t *read_file(const char *p, size_t *len);
int write_file(const char *p, const uint8_t *b, size_t n);
void print_size_human(uint64_t size, char *buf, size_t buf_size);
QuadrProbeResult do_probe(const uint8_t *data, size_t len, const EncodeConfig *cfg);

/* ─── Option Parsing ──────────────────────────────────────────────────────── */

int parse_hint(const char *s, uint8_t *o);
int parse_backend(const char *s, Backend *o);
int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CMD_COMMON_H */
