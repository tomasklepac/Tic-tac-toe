#pragma once
#include <stdio.h>
#include <stdarg.h>

void log_init(const char* path);
void log_close();
void server_log(const char* fmt, ...);

