#ifndef CMD_HELP_H
#define CMD_HELP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Show short usage */
void usage_short(const char *p);

/* Show full help */
void usage_full(const char *p);

/* Show version */
void show_version(void);

/* Check if argument is a known global option */
int is_known_global_option(const char *arg);

#ifdef __cplusplus
}
#endif

#endif /* CMD_HELP_H */
