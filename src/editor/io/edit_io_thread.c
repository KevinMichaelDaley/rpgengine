/**
 * @file edit_io_thread.c
 * @brief Editor I/O thread — lifecycle (start/stop) and main loop.
 *
 * Runs an epoll-based event loop on a dedicated pthread. Accepts
 * edit protocol connections, reads newline-delimited JSON, and
 * bridges commands/responses via SPSC rings.
 */

#define _GNU_SOURCE
#include "ferrum/editor/edit_io_thread.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/* ----------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ----------------------------------------------------------------------- */

/** @brief Set a socket to non-blocking mode. */
static bool set_nonblocking_(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/** @brief Find an unused client slot. Returns index or -1 if full. */
static int find_free_client_(edit_io_thread_t *io) {
    for (int i = 0; i < EDIT_IO_MAX_CLIENTS; ++i) {
        if (io->clients[i].fd < 0) return i;
    }
    return -1;
}

/** @brief Find the client slot for a given fd. Returns index or -1. */
static int find_client_by_fd_(edit_io_thread_t *io, int fd) {
    for (int i = 0; i < EDIT_IO_MAX_CLIENTS; ++i) {
        if (io->clients[i].fd == fd) return i;
    }
    return -1;
}

/** @brief Close a client connection and clean up its slot. */
static void close_client_(edit_io_thread_t *io, int idx) {
    if (idx < 0 || idx >= EDIT_IO_MAX_CLIENTS) return;
    edit_io_client_t *c = &io->clients[idx];
    if (c->fd >= 0) {
        epoll_ctl(io->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        c->fd = -1;
    }
    free(c->read_buf);
    c->read_buf = NULL;
    c->read_len = 0;
    c->read_cap = 0;
}

/* ----------------------------------------------------------------------- */
/* Accept handling                                                           */
/* ----------------------------------------------------------------------- */

/** @brief Accept a new client and add to epoll. */
static void accept_client_(edit_io_thread_t *io) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd = accept(io->listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0) return;

    int idx = find_free_client_(io);
    if (idx < 0) {
        close(fd); /* No room. */
        return;
    }

    set_nonblocking_(fd);

    /* Disable Nagle for low-latency responses. */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    edit_io_client_t *c = &io->clients[idx];
    c->fd       = fd;
    c->read_buf = (char *)malloc(EDIT_IO_READ_BUF_SIZE);
    c->read_len = 0;
    c->read_cap = EDIT_IO_READ_BUF_SIZE;

    struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
    epoll_ctl(io->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

/* ----------------------------------------------------------------------- */
/* Read handling                                                             */
/* ----------------------------------------------------------------------- */

/**
 * @brief Process a complete JSON line: push it into the command ring.
 */
static void process_line_(edit_io_thread_t *io, const char *line,
                          uint32_t len) {
    (void)io;
    /* Push the raw JSON line into the command ring. */
    edit_cmd_ring_push(io->cmd_ring, 0, line, len);
}

/**
 * @brief Read available data from a client and process complete lines.
 */
static void read_client_(edit_io_thread_t *io, int client_idx) {
    edit_io_client_t *c = &io->clients[client_idx];

    for (;;) {
        /* Ensure read buffer has room. */
        if (c->read_len >= c->read_cap) {
            if (c->read_cap >= EDIT_IO_MAX_LINE) {
                /* Line too long — drop client. */
                close_client_(io, client_idx);
                return;
            }
            uint32_t new_cap = c->read_cap * 2;
            if (new_cap > EDIT_IO_MAX_LINE) new_cap = EDIT_IO_MAX_LINE;
            char *new_buf = (char *)realloc(c->read_buf, new_cap);
            if (!new_buf) {
                close_client_(io, client_idx);
                return;
            }
            c->read_buf = new_buf;
            c->read_cap = new_cap;
        }

        ssize_t n = read(c->fd, c->read_buf + c->read_len,
                         c->read_cap - c->read_len);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                close_client_(io, client_idx);
            }
            break;
        }
        c->read_len += (uint32_t)n;

        /* Scan for newlines and process complete lines. */
        uint32_t scan_start = 0;
        for (uint32_t i = 0; i < c->read_len; ++i) {
            if (c->read_buf[i] == '\n') {
                uint32_t line_len = i - scan_start;
                if (line_len > 0) {
                    process_line_(io, c->read_buf + scan_start, line_len);
                }
                scan_start = i + 1;
            }
        }

        /* Shift remaining partial data to beginning. */
        if (scan_start > 0) {
            uint32_t remaining = c->read_len - scan_start;
            if (remaining > 0) {
                memmove(c->read_buf, c->read_buf + scan_start, remaining);
            }
            c->read_len = remaining;
        }
    }
}

/* ----------------------------------------------------------------------- */
/* Response drain                                                            */
/* ----------------------------------------------------------------------- */

/**
 * @brief Drain the response ring and send responses to the first connected
 *        client (simplified: broadcasts to all connected clients).
 */
static void drain_responses_(edit_io_thread_t *io) {
    const edit_cmd_slot_t *slot;
    while ((slot = edit_cmd_ring_peek(io->resp_ring)) != NULL) {
        /* Send to all connected clients. */
        for (int i = 0; i < EDIT_IO_MAX_CLIENTS; ++i) {
            if (io->clients[i].fd >= 0) {
                /* Best-effort write; ignore partial sends for now. */
                write(io->clients[i].fd, slot->payload, slot->payload_len);
            }
        }
        edit_cmd_ring_advance(io->resp_ring);
    }
}

/* ----------------------------------------------------------------------- */
/* Main event loop (runs on I/O thread)                                      */
/* ----------------------------------------------------------------------- */

static void *io_thread_main_(void *arg) {
    edit_io_thread_t *io = (edit_io_thread_t *)arg;
    struct epoll_event events[16];

    while (atomic_load_explicit(&io->running, memory_order_acquire)) {
        int nfds = epoll_wait(io->epoll_fd, events, 16, 50 /* 50ms timeout */);

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == io->listen_fd) {
                accept_client_(io);
                continue;
            }

            /* Client event — data available to read. */
            if (events[i].events & EPOLLIN) {
                int idx = find_client_by_fd_(io, fd);
                if (idx >= 0) {
                    read_client_(io, idx);
                }
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                int idx = find_client_by_fd_(io, fd);
                if (idx >= 0) {
                    close_client_(io, idx);
                }
            }
        }

        /* Drain response ring each iteration. */
        drain_responses_(io);
    }

    return NULL;
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

bool edit_io_start(edit_io_thread_t *io, uint16_t port,
                   edit_cmd_ring_t *cmd_ring, edit_cmd_ring_t *resp_ring) {
    if (!io || !cmd_ring || !resp_ring) return false;

    memset(io, 0, sizeof(*io));
    io->cmd_ring  = cmd_ring;
    io->resp_ring = resp_ring;
    io->listen_fd = -1;
    io->epoll_fd  = -1;

    /* Init client slots. */
    for (int i = 0; i < EDIT_IO_MAX_CLIENTS; ++i) {
        io->clients[i].fd = -1;
    }

    /* Create listen socket. */
    io->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (io->listen_fd < 0) return false;

    int reuse = 1;
    setsockopt(io->listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    set_nonblocking_(io->listen_fd);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port == 0 ? EDIT_IO_DEFAULT_PORT : port),
    };

    if (bind(io->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(io->listen_fd);
        return false;
    }

    /* Retrieve the actual port. */
    socklen_t addr_len = sizeof(addr);
    getsockname(io->listen_fd, (struct sockaddr *)&addr, &addr_len);
    io->port = ntohs(addr.sin_port);

    if (listen(io->listen_fd, 8) != 0) {
        close(io->listen_fd);
        return false;
    }

    /* Create epoll. */
    io->epoll_fd = epoll_create1(0);
    if (io->epoll_fd < 0) {
        close(io->listen_fd);
        return false;
    }

    struct epoll_event ev = {.events = EPOLLIN, .data.fd = io->listen_fd};
    epoll_ctl(io->epoll_fd, EPOLL_CTL_ADD, io->listen_fd, &ev);

    /* Start thread. */
    atomic_store_explicit(&io->running, true, memory_order_release);
    if (pthread_create(&io->thread, NULL, io_thread_main_, io) != 0) {
        close(io->epoll_fd);
        close(io->listen_fd);
        return false;
    }

    return true;
}

void edit_io_stop(edit_io_thread_t *io) {
    if (!io) return;

    atomic_store_explicit(&io->running, false, memory_order_release);
    pthread_join(io->thread, NULL);

    /* Close all clients. */
    for (int i = 0; i < EDIT_IO_MAX_CLIENTS; ++i) {
        close_client_(io, i);
    }

    if (io->epoll_fd >= 0) { close(io->epoll_fd); io->epoll_fd = -1; }
    if (io->listen_fd >= 0) { close(io->listen_fd); io->listen_fd = -1; }
}
