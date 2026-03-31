#ifndef CMD_PROBE_H
#define CMD_PROBE_H

#include "cmd_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Execute probe command */
int cmd_probe(const char *path, const EncodeConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* CMD_PROBE_H */
