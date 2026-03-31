#ifndef CMD_COMMON_H
#define CMD_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "quadr.h"
#include "quadr_console.h"
#include "quadr_logic_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Backend Types (alias to QLBackend for CLI convenience) ──────────── */

typedef QLBackend Backend;

#define BACKEND_NONE   QL_BACKEND_NONE
#define BACKEND_ZLIB   QL_BACKEND_ZLIB
#define BACKEND_ZSTD   QL_BACKEND_ZSTD
#define BACKEND_LZ4    QL_BACKEND_LZ4
#define BACKEND_LZ4HC  QL_BACKEND_LZ4HC
#define BACKEND_7Z     QL_BACKEND_7Z

/* ─── Encode Configuration (alias to QLEncodeConfig) ──────────────────── */

typedef QLEncodeConfig EncodeConfig;

/* ─── Helpers ──────────────────────────────────────────────────────────── */

#define backend_name(b)            ql_backend_name(b)
#define backend_default_level(b)   ql_backend_default_level(b)
#define effective_level(c)         ql_effective_level(c)
#define encode_config_default(c)   ql_encode_config_default(c)
#define read_file(p, len)          ql_read_file(p, len)
#define write_file(p, b, n)        ql_write_file(p, b, n)
#define print_size_human(s, b, sz) ql_print_size_human(s, b, sz)

/* ─── Option Parsing ──────────────────────────────────────────────────── */

int parse_hint(const char *s, uint8_t *o);
int parse_backend(const char *s, Backend *o);
int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CMD_COMMON_H */
