#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

static FILE* g_log = NULL;

void log_init(const char* path) {
    if (g_log) return;
    g_log = fopen(path, "w");
}

void log_close() {
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void logf(const char* fmt, ...) {
    if (!g_log) return;
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(g_log, "[%s] ", tbuf);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fprintf(g_log, "\n");
    fflush(g_log);
}
