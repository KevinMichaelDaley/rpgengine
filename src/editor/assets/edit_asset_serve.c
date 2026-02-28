/**
 * @file edit_asset_serve.c
 * @brief Asset download server — lifecycle and main loop.
 *
 * Non-static functions: edit_asset_server_start, edit_asset_server_stop (2).
 */

#define _GNU_SOURCE
#include "ferrum/editor/assets/edit_asset_serve.h"
#include "ferrum/editor/edit_asset_registry.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
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

/** @brief Find an unused client slot. Returns index or -1. */
static int find_free_client_(edit_asset_server_t *s) {
    for (int i = 0; i < EDIT_ASSET_MAX_CLIENTS; ++i) {
        if (s->clients[i].fd < 0) return i;
    }
    return -1;
}

/** @brief Close a client connection. */
static void close_client_(edit_asset_server_t *s, int idx) {
    if (idx < 0 || idx >= EDIT_ASSET_MAX_CLIENTS) return;
    edit_asset_client_t *c = &s->clients[idx];
    if (c->fd >= 0) {
        epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
        close(c->fd);
        c->fd = -1;
    }
}

/** @brief Read exactly n bytes from a blocking/non-blocking fd. */
static bool read_exact_(int fd, void *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t r = recv(fd, (char *)buf + done, n - done, 0);
        if (r <= 0) return false;
        done += (size_t)r;
    }
    return true;
}

/** @brief Send exactly n bytes. */
static bool send_exact_(int fd, const void *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = send(fd, (const char *)buf + done, n - done, MSG_NOSIGNAL);
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

/** @brief Send an error/not-found response (status + 0 length). */
static void send_status_(int fd, uint8_t status) {
    uint8_t resp[5];
    resp[0] = status;
    memset(resp + 1, 0, 4); /* total_len = 0 */
    send_exact_(fd, resp, 5);
}

/* ----------------------------------------------------------------------- */
/* Request handling                                                          */
/* ----------------------------------------------------------------------- */

/**
 * @brief Handle one binary request on a client connection.
 *
 * Reads the request header, looks up the asset, reads the file,
 * and sends the binary response. Returns false if the connection
 * should be closed (client disconnected or protocol error).
 */
static bool handle_request_(edit_asset_server_t *s, int client_fd) {
    /* Read path_len (u16 LE). */
    uint8_t hdr[2];
    if (!read_exact_(client_fd, hdr, 2)) return false;
    uint16_t path_len = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);

    /* Validate path length. */
    if (path_len == 0 || path_len > EDIT_ASSET_MAX_PATH) {
        send_status_(client_fd, EDIT_ASSET_STATUS_ERROR);
        return path_len == 0; /* Keep connection on empty path, close on overflow. */
    }

    /* Read path. */
    char path[EDIT_ASSET_MAX_PATH + 1];
    if (!read_exact_(client_fd, path, path_len)) return false;
    path[path_len] = '\0';

    /* Look up in registry. */
    const edit_asset_entry_t *entry =
        edit_asset_registry_find(s->registry, path);
    if (!entry) {
        send_status_(client_fd, EDIT_ASSET_STATUS_NOT_FOUND);
        return true; /* Connection stays open. */
    }

    /* Build full filesystem path. */
    char full_path[2048];
    int n = snprintf(full_path, sizeof(full_path), "%s/%s",
                     s->asset_root, path);
    if (n < 0 || (size_t)n >= sizeof(full_path)) {
        send_status_(client_fd, EDIT_ASSET_STATUS_ERROR);
        return true;
    }

    /* Open and read the file. */
    FILE *f = fopen(full_path, "rb");
    if (!f) {
        send_status_(client_fd, EDIT_ASSET_STATUS_ERROR);
        return true;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0 || file_size > 64 * 1024 * 1024) {
        /* Reject files > 64MB. */
        fclose(f);
        send_status_(client_fd, EDIT_ASSET_STATUS_ERROR);
        return true;
    }

    uint32_t total_len = (uint32_t)file_size;
    void *data = malloc(total_len);
    if (!data) {
        fclose(f);
        send_status_(client_fd, EDIT_ASSET_STATUS_ERROR);
        return true;
    }

    size_t read_n = fread(data, 1, total_len, f);
    fclose(f);
    if (read_n != total_len) {
        free(data);
        send_status_(client_fd, EDIT_ASSET_STATUS_ERROR);
        return true;
    }

    /* Send response header: status(1) + total_len(4 LE). */
    uint8_t resp_hdr[5];
    resp_hdr[0] = EDIT_ASSET_STATUS_OK;
    resp_hdr[1] = (uint8_t)(total_len & 0xFF);
    resp_hdr[2] = (uint8_t)((total_len >> 8) & 0xFF);
    resp_hdr[3] = (uint8_t)((total_len >> 16) & 0xFF);
    resp_hdr[4] = (uint8_t)((total_len >> 24) & 0xFF);
    if (!send_exact_(client_fd, resp_hdr, 5)) {
        free(data);
        return false;
    }

    /* Send file data. */
    bool ok = send_exact_(client_fd, data, total_len);
    free(data);
    return ok;
}

/* ----------------------------------------------------------------------- */
/* Accept handling                                                           */
/* ----------------------------------------------------------------------- */

/** @brief Accept a new client connection. */
static void accept_client_(edit_asset_server_t *s) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd = accept(s->listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0) return;

    int idx = find_free_client_(s);
    if (idx < 0) {
        close(fd);
        return;
    }

    /* Asset connections use blocking I/O for simplicity. */
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    s->clients[idx].fd = fd;

    struct epoll_event ev = {.events = EPOLLIN, .data.fd = fd};
    epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

/* ----------------------------------------------------------------------- */
/* Main loop                                                                 */
/* ----------------------------------------------------------------------- */

static void *asset_server_main_(void *arg) {
    edit_asset_server_t *s = (edit_asset_server_t *)arg;
    struct epoll_event events[8];

    while (atomic_load_explicit(&s->running, memory_order_acquire)) {
        int nfds = epoll_wait(s->epoll_fd, events, 8, 50);

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == s->listen_fd) {
                accept_client_(s);
                continue;
            }

            /* Client readable — handle one request. */
            if (events[i].events & EPOLLIN) {
                /* Find client index. */
                int idx = -1;
                for (int j = 0; j < EDIT_ASSET_MAX_CLIENTS; ++j) {
                    if (s->clients[j].fd == fd) { idx = j; break; }
                }
                if (idx >= 0) {
                    if (!handle_request_(s, fd)) {
                        close_client_(s, idx);
                    }
                }
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                for (int j = 0; j < EDIT_ASSET_MAX_CLIENTS; ++j) {
                    if (s->clients[j].fd == fd) {
                        close_client_(s, j);
                        break;
                    }
                }
            }
        }
    }

    return NULL;
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

bool edit_asset_server_start(edit_asset_server_t *server, uint16_t port,
                              const edit_asset_registry_t *registry,
                              const char *asset_root) {
    if (!server || !registry || !asset_root) return false;

    memset(server, 0, sizeof(*server));
    server->registry   = registry;
    server->asset_root = asset_root;
    server->listen_fd  = -1;
    server->epoll_fd   = -1;

    for (int i = 0; i < EDIT_ASSET_MAX_CLIENTS; ++i) {
        server->clients[i].fd = -1;
    }

    /* Create listen socket. */
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) return false;

    int reuse = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));
    set_nonblocking_(server->listen_fd);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port),
    };

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(server->listen_fd);
        return false;
    }

    /* Retrieve actual bound port. */
    socklen_t addr_len = sizeof(addr);
    getsockname(server->listen_fd, (struct sockaddr *)&addr, &addr_len);
    server->port = ntohs(addr.sin_port);

    if (listen(server->listen_fd, 4) != 0) {
        close(server->listen_fd);
        return false;
    }

    /* Create epoll. */
    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd < 0) {
        close(server->listen_fd);
        return false;
    }

    struct epoll_event ev = {.events = EPOLLIN, .data.fd = server->listen_fd};
    epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &ev);

    /* Start thread. */
    atomic_store_explicit(&server->running, true, memory_order_release);
    if (pthread_create(&server->thread, NULL, asset_server_main_, server) != 0) {
        close(server->epoll_fd);
        close(server->listen_fd);
        return false;
    }

    return true;
}

void edit_asset_server_stop(edit_asset_server_t *server) {
    if (!server) return;

    atomic_store_explicit(&server->running, false, memory_order_release);
    pthread_join(server->thread, NULL);

    for (int i = 0; i < EDIT_ASSET_MAX_CLIENTS; ++i) {
        close_client_(server, i);
    }

    if (server->epoll_fd >= 0)  { close(server->epoll_fd);  server->epoll_fd  = -1; }
    if (server->listen_fd >= 0) { close(server->listen_fd); server->listen_fd = -1; }
}
