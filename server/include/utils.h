#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

// ============================================================
//  UTILS MODULE HEADER
//  ------------------------------------------------------------
//  Provides helper functions for socket I/O and string handling.
//  - sendp: formatted protocol message sending
//  - recv_line: reads one line from socket
//  - trim_newline: removes trailing newline characters
// ============================================================


/**
 * @brief Sends a formatted protocol message to a client.
 *
 * Automatically prefixes message with "##" and appends a newline ('\n').
 *
 * @param fd  File descriptor (socket) of the client.
 * @param fmt Format string (printf-like).
 * @param ... Variable arguments for formatting.
 */
void sendp(int fd, const char* fmt, ...);


/**
 * @brief Reads a single line (ending with '\n') from a socket.
 *
 * @param fd   File descriptor (socket) to read from.
 * @param out  Output buffer.
 * @param cap  Maximum capacity of the buffer.
 * @return Number of bytes read (0 if disconnected).
 */
int recv_line(int fd, char* out, size_t cap);


/**
 * @brief Removes trailing newline and carriage return characters.
 *
 * @param s  Input string to be trimmed (modified in place).
 */
void trim_newline(char* s);


#endif
