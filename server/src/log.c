#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#include <pthread.h>

static FILE* g_log = NULL;
static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;

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

void server_log(const char* fmt, ...) {
    if (!g_log) return;
    
    pthread_mutex_lock(&g_log_mtx);
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
    pthread_mutex_unlock(&g_log_mtx);
}
