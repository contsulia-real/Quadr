#ifndef CMD_DECODE_H
#define CMD_DECODE_H

#include "cmd_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Execute decode command */
int cmd_decode(const char *in_path, const char *out_path, int parallel, int num_threads);

#ifdef __cplusplus
}
#endif

#endif /* CMD_DECODE_H */
