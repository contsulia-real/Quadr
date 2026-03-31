#ifndef CMD_ENCODE_H
#define CMD_ENCODE_H

#include "cmd_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse encode options from command line */
int parse_encode_opts(int argc, char **argv, int *i, EncodeConfig *cfg);

/* Execute encode command */
int cmd_encode(const char *in_path, const char *out_path, const EncodeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CMD_ENCODE_H */
