#ifndef QUADR_CONSOLE_H
#define QUADR_CONSOLE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── ANSI color codes (auto-disabled on non-TTY Windows pre-VT) ─── */

int  con_is_tty(FILE *f);
void con_init(void);

/* Text styles */
#define CON_RESET       "\033[0m"
#define CON_BOLD        "\033[1m"
#define CON_DIM         "\033[2m"
#define CON_UNDERLINE   "\033[4m"

/* Foreground colors */
#define CON_BLACK       "\033[30m"
#define CON_RED         "\033[31m"
#define CON_GREEN       "\033[32m"
#define CON_YELLOW      "\033[33m"
#define CON_BLUE        "\033[34m"
#define CON_MAGENTA     "\033[35m"
#define CON_CYAN        "\033[36m"
#define CON_WHITE       "\033[37m"
#define CON_GRAY        "\033[90m"

/* Bright foreground colors */
#define CON_BRIGHT_BLACK   "\033[90m"
#define CON_BRIGHT_RED     "\033[91m"
#define CON_BRIGHT_GREEN   "\033[92m"
#define CON_BRIGHT_YELLOW  "\033[93m"
#define CON_BRIGHT_BLUE    "\033[94m"
#define CON_BRIGHT_MAGENTA "\033[95m"
#define CON_BRIGHT_CYAN    "\033[96m"
#define CON_BRIGHT_WHITE   "\033[97m"

/* Background colors */
#define CON_BG_BLACK    "\033[40m"
#define CON_BG_RED      "\033[41m"
#define CON_BG_GREEN    "\033[42m"
#define CON_BG_YELLOW   "\033[43m"
#define CON_BG_BLUE     "\033[44m"
#define CON_BG_MAGENTA  "\033[45m"
#define CON_BG_CYAN     "\033[46m"
#define CON_BG_WHITE    "\033[47m"

/* ─── Semantic output helpers (all go to stdout) ─── */

/* Error: red bold prefix, resets after */
void con_error(const char *fmt, ...);

/* Warning: yellow bold prefix */
void con_warn(const char *fmt, ...);

/* Success: green bold prefix */
void con_ok(const char *fmt, ...);

/* Info: cyan prefix */
void con_info(const char *fmt, ...);

/* Debug: gray/dim */
void con_debug(const char *fmt, ...);

/* Plain info line with label: value coloring */
void con_kv(const char *key, const char *fmt, ...);

/* Section header (bold + underline) */
void con_section(const char *title);

/* Table header row */
void con_table_header(const char *fmt, ...);

/* Table data row */
void con_table_row(const char *fmt, ...);

/* Progress bar (single-line, overwrites) */
void con_progress(double pct, const char *label);

/* Newline helper */
void con_nl(void);

#ifdef __cplusplus
}
#endif

#endif /* QUADR_CONSOLE_H */
