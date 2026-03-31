#ifndef CMD_PARSE_H
#define CMD_PARSE_H

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

/* Encode configuration */
typedef struct {
    QuadrEncodeOpts quadr;
    uint8_t backend;
    int level;
    int use_fast_probe;
    int mixed_backend;
    int parallel;
    int num_threads;
} EncodeConfig;

/* Option parsing */
void encode_config_default(EncodeConfig *c);
int parse_hint(const char *s, uint8_t *o);
int parse_backend(const char *s, uint8_t *o);
int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg);
int is_known_global_option(const char *arg);

/* Usage and help */
void usage_short(const char *p);
void usage_full(const char *p);
void show_version(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_PARSE_H */
