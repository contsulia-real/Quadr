#ifndef CMD_VERIFY_H
#define CMD_VERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Execute verify command (single file) */
int cmd_verify(const char *path);

/* Execute archive verify command */
int cmd_verify_archive(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CMD_VERIFY_H */
