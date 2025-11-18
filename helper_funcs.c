#include "helper_funcs.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------------- Listener_Socket helpers ---------------- */

int listener_init(Listener_Socket *sock, int port) {
    if (!sock) {
        errno = EINVAL;
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t) port);

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        close(fd);
        return -1;
    }

    sock->fd = fd;
    return 0;
}

int listener_accept(Listener_Socket *sock) {
    if (!sock) {
        errno = EINVAL;
        return -1;
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(sock->fd, (struct sockaddr *) &addr, &addrlen);
    return client_fd; // -1 on error, caller can check errno
}

/* ---------------- Robust I/O helpers ---------------- */

ssize_t read_until(int fd, char *buf, size_t max_bytes, const char *delim) {
    if (!buf || max_bytes == 0) {
        errno = EINVAL;
        return -1;
    }

    size_t total = 0;
    buf[0] = '\0';

    size_t delim_len = strlen(delim);

    while (total < max_bytes - 1) {
        ssize_t n = read(fd, buf + total, (max_bytes - 1) - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            // EOF
            break;
        }

        total += (size_t) n;
        buf[total] = '\0';

        if (delim_len > 0 && strstr(buf, delim) != NULL) {
            break;
        }
    }

    return (ssize_t) total;
}

ssize_t read_n_bytes(int fd, void *vbuf, size_t n) {
    unsigned char *buf = vbuf;
    size_t total = 0;

    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            // EOF
            break;
        }
        total += (size_t) r;
    }

    return (ssize_t) total;
}

ssize_t write_n_bytes(int fd, const void *vbuf, size_t n) {
    const unsigned char *buf = vbuf;
    size_t total = 0;

    while (total < n) {
        ssize_t w = write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            // Shouldn’t normally happen for write; treat as error-ish.
            break;
        }
        total += (size_t) w;
    }

    return (ssize_t) total;
}

ssize_t pass_n_bytes(int src, int dst, size_t n) {
    char buffer[4096];
    size_t total_written = 0;

    while (total_written < n) {
        size_t to_read = n - total_written;
        if (to_read > sizeof(buffer)) {
            to_read = sizeof(buffer);
        }

        ssize_t r = read(src, buffer, to_read);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            // EOF on src
            break;
        }

        size_t written_for_chunk = 0;
        while ((ssize_t) written_for_chunk < r) {
            ssize_t w = write(dst, buffer + written_for_chunk, (size_t) r - written_for_chunk);
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
            if (w == 0) {
                // Unusual; treat as "can't write more"
                break;
            }
            written_for_chunk += (size_t) w;
        }

        total_written += written_for_chunk;

        if ((ssize_t) written_for_chunk < r) {
            // Couldn't write entire chunk; bail out.
            break;
        }
    }

    return (ssize_t) total_written;
}
