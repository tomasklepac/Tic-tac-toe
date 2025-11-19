// ============================================================
//  UTILS MODULE IMPLEMENTATION
//  ------------------------------------------------------------
//  Provides helper utilities for network communication and
//  basic string manipulation used throughout the server.
//
//  Functions:
//   - sendp:     Send formatted protocol message (prefixed with ##)
//   - recv_line: Read single line from socket
//   - trim_newline: Remove trailing newline / carriage return
// ============================================================

#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>

// ============================================================
//  sendp()
//  ------------------------------------------------------------
//  Sends a formatted protocol message to the given socket.
//  The message is automatically prefixed with "##" and ends
//  with a newline ('\n').
//
//  Example: sendp(fd, "HELLO|%s", name)  -->  ##HELLO|John\n
// ============================================================
void sendp(int fd, const char* fmt, ...) {
    char payload[256];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(payload, sizeof(payload), fmt, ap);
    va_end(ap);

    char msg[300];
    snprintf(msg, sizeof(msg), "##%s\n", payload);

    ssize_t ret = send(fd, msg, strlen(msg), 0);
    if (ret < 0) {
        perror("send");
    }
}


// ============================================================
//  recv_line()
//  ------------------------------------------------------------
//  Reads a single line (terminated by '\n') from the socket.
//  Returns the number of bytes read, or 0 if the client
//  disconnected. The result is null-terminated.
//
//  NOTE: Blocking call – waits for data until newline or EOF.
// ============================================================
int recv_line(int fd, char* out, size_t cap) {
    size_t used = 0;

    while (used + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) break;          // disconnected or error
        out[used++] = c;
        if (c == '\n') break;       // line complete
    }

    out[used] = '\0';
    return (int)used;
}


// ============================================================
//  trim_newline()
//  ------------------------------------------------------------
//  Removes any trailing '\n' or '\r' characters from a string.
//  Useful for sanitizing input lines read from sockets.
// ============================================================
void trim_newline(char* s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}
