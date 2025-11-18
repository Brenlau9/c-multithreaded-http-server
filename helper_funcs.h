/**
 * @file helper_funcs.h
 * @brief Helper functions for sockets and robust I/O.
 *
 */

#pragma once

#include <stdint.h>
#include <sys/types.h>

/**
 * @struct Listener_Socket
 * @brief Represents a socket listening for incoming connections.
 */
typedef struct {
    int fd;  // listening socket file descriptor
} Listener_Socket;

/**
 * @brief Initialize a listening TCP socket on the given port.
 *
 * @param sock  Pointer to Listener_Socket to initialize.
 * @param port  Port number to bind and listen on.
 *
 * @return 0 on success, -1 on error (errno is set).
 */
int listener_init(Listener_Socket *sock, int port);

/**
 * @brief Accept a connection on the listening socket.
 *
 * @param sock Pointer to Listener_Socket.
 *
 * @return A newly accepted client socket fd on success, or -1 on error.
 */
int listener_accept(Listener_Socket *sock);

/**
 * @brief Read from fd into buf until:
 *        - the delimiter string appears in buf,
 *        - max_bytes-1 bytes have been read, or
 *        - EOF or error occurs.
 *
 * buf is always NUL-terminated if max_bytes > 0.
 *
 * @param fd         File descriptor to read from.
 * @param buf        Destination buffer.
 * @param max_bytes  Maximum number of bytes to place in buf (including NUL).
 * @param delim      Delimiter string to search for (e.g. "\r\n\r\n").
 *
 * @return Number of bytes read into buf (excluding NUL) on success, or -1 on error.
 */
ssize_t read_until(int fd, char *buf, size_t max_bytes, const char *delim);

/**
 * @brief Read exactly n bytes from fd into buf unless EOF or error occurs.
 *
 * @param fd  File descriptor to read from.
 * @param buf Buffer to store data.
 * @param n   Number of bytes to read.
 *
 * @return Number of bytes actually read (0..n) on EOF, or -1 on error.
 */
ssize_t read_n_bytes(int fd, void *buf, size_t n);

/**
 * @brief Write exactly n bytes from buf to fd, unless an error occurs.
 *
 * @param fd  File descriptor to write to.
 * @param buf Buffer with data.
 * @param n   Number of bytes to write.
 *
 * @return Number of bytes actually written (0..n), or -1 on error.
 */
ssize_t write_n_bytes(int fd, const void *buf, size_t n);

/**
 * @brief Reads bytes from src and writes them to dst until either:
 *        (1) exactly n bytes have been copied,
 *        (2) read() returns 0 (EOF), or
 *        (3) an error occurs.
 *
 * @param src File descriptor to read from.
 * @param dst File descriptor to write to.
 * @param n   Maximum number of bytes to copy.
 *
 * @return Number of bytes written, or -1 on error.
 */
ssize_t pass_n_bytes(int src, int dst, size_t n);
