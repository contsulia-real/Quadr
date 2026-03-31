#ifndef CMD_HELP_H
#define CMD_HELP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Show short usage (for --help or unknown command) */
void usage_short(const char *p);

/* Show full help (for 'help' command) - no version/backends */
void usage_full(const char *p);

/* Show version + backends (for 'version' command and --version) */
void show_version(void);

/* Check if argument is a known global option */
int is_known_global_option(const char *arg);

/* 'help' command handler */
int cmd_help(const char *prog);

/* Per-command --help dispatch: cmd_help_for("encode", prog) etc. */
int cmd_help_for(const char *cmd, const char *prog);

#ifdef __cplusplus
}
#endif

#endif /* CMD_HELP_H */
