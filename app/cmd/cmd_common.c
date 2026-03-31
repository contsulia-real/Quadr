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

/* ─── Option Parsing ────────────────────────────────────────────────────── */

int parse_hint(const char *s, uint8_t *o) {
    return ql_parse_hint(s, o);
}

int parse_backend(const char *s, Backend *o) {
    return ql_parse_backend(s, o);
}

int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg) {
    (void)argc;
    char *arg = argv[*i];
    char *eq = strchr(arg, '=');
    const char *val = eq ? eq+1 : "";

    if (!strncmp(arg,"--hint=",7)) { if (ql_parse_hint(val, &cfg->quadr.data_hint) != 0) { con_error("invalid hint '%s'", val); return -1; } return 0; }
    else if (!strncmp(arg,"--block=",8)) { if (val[0]=='\0') { con_error("--block= requires a value"); return -1; } cfg->quadr.block_size=(uint32_t)(atoi(val)*1024); return 0; }
    else if (!strncmp(arg,"--xbit=",7)) { if (val[0]=='\0') { con_error("--xbit= requires a value"); return -1; } cfg->quadr.x_bit=(uint8_t)atoi(val); return 0; }
    else if (!strncmp(arg,"--stride=",9)) { if (val[0]=='\0') { con_error("--stride= requires a value"); return -1; } cfg->quadr.hint_stride=(uint8_t)atoi(val); return 0; }
    else if (!strncmp(arg,"--backend=",10)) { Backend bk; if (ql_parse_backend(val, &bk) != 0) { con_error("unknown backend '%s'", val); return -1; } cfg->backend = bk; return 0; }
    else if (!strncmp(arg,"--level=",8)) { if (val[0]=='\0') { con_error("--level= requires a value"); return -1; } cfg->level=atoi(val); return 0; }
    else if (!strcmp(arg,"--fast")) { cfg->use_fast_probe=1; return 0; }
    else if (!strcmp(arg,"--no-fast")) { cfg->use_fast_probe=0; return 0; }
    else if (!strcmp(arg,"--mixed-backend")) { cfg->mixed_backend=1; return 0; }
    else if (!strcmp(arg,"--adaptive-block")) { cfg->quadr.adaptive_block=1; return 0; }
    else if (!strcmp(arg,"--parallel")) { cfg->parallel=1; return 0; }
    else if (!strncmp(arg,"--threads=",10)) { if (val[0]=='\0') { con_error("--threads= requires a value"); return -1; } cfg->num_threads=atoi(val); return 0; }
    else if (!strcmp(arg,"--auto")) { cfg->auto_configure=1; cfg->auto_mode=0; return 0; }
    else if (!strncmp(arg,"--auto=",7)) { cfg->auto_configure=1; if (ql_parse_auto_mode(val, &cfg->auto_mode) != 0) { con_error("invalid auto mode '%s' (use ratio, balance, or speed)", val); return -1; } return 0; }


    con_error("unknown option '%s'", arg);
    return -1;

}
