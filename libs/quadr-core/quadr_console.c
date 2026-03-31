#include "quadr_console.h"
#include "quadr_platform.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#if defined(QUADR_OS_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static int g_colors_enabled = 1;

int con_is_tty(FILE *f) {
#if defined(QUADR_OS_WINDOWS)
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD mode = 0;
    return GetConsoleMode(h, &mode) != 0;
#else
    return isatty(fileno(f));
#endif
}

void con_init(void) {
#if defined(QUADR_OS_WINDOWS)
    /* Enable VT processing on Windows 10+ */
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hout, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
            SetConsoleMode(hout, mode);
        }
    }
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    if (hin != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hin, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
            SetConsoleMode(hin, mode);
        }
    }
#endif
    g_colors_enabled = con_is_tty(stdout);
}

#define C(s) (g_colors_enabled ? (s) : "")

void con_error(const char *fmt, ...) {
    fprintf(stdout, "%s%serror%s: ", C(CON_BOLD), C(CON_BRIGHT_RED), C(CON_RESET));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_warn(const char *fmt, ...) {
    fprintf(stdout, "%s%swarning%s: ", C(CON_BOLD), C(CON_BRIGHT_YELLOW), C(CON_RESET));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_ok(const char *fmt, ...) {
    fprintf(stdout, "%s%sok%s: ", C(CON_BOLD), C(CON_BRIGHT_GREEN), C(CON_RESET));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_info(const char *fmt, ...) {
    fprintf(stdout, "%s%sinfo%s: ", C(CON_BOLD), C(CON_BRIGHT_CYAN), C(CON_RESET));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_debug(const char *fmt, ...) {
    if (!g_colors_enabled) {
        fprintf(stdout, "[debug] ");
    } else {
        fprintf(stdout, "%s%sdebug%s: ", C(CON_DIM), C(CON_GRAY), C(CON_RESET));
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_kv(const char *key, const char *fmt, ...) {
    fprintf(stdout, "  %s%s%-16s%s  ", C(CON_BRIGHT_CYAN), C(CON_BOLD), key, C(CON_RESET));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_section(const char *title) {
    fprintf(stdout, "\n%s%s%s%s\n", C(CON_BOLD), C(CON_UNDERLINE), title, C(CON_RESET));
}

void con_table_header(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "%s", C(CON_BOLD));
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "%s\n", C(CON_RESET));
}

void con_table_row(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

void con_progress(double pct, const char *label) {
    const int bar_w = 40;
    int filled = (int)(pct * bar_w);
    if (filled > bar_w) filled = bar_w;
    if (!g_colors_enabled) {
        fprintf(stdout, "\r[%-40s] %5.1f%%  %s",
                "========================================" + (bar_w - filled),
                pct * 100.0, label ? label : "");
    } else {
        fprintf(stdout, "\r%s[", C(CON_DIM));
        for (int i = 0; i < bar_w; i++) {
            if (i < filled)
                fprintf(stdout, "%s█%s", C(CON_BRIGHT_GREEN), C(CON_DIM));
            else
                fprintf(stdout, "░");
        }
        fprintf(stdout, "]%s %5.1f%%  %s", C(CON_RESET), pct * 100.0, label ? label : "");
    }
    fflush(stdout);
}

void con_nl(void) {
    fprintf(stdout, "\n");
}
