/**
 * @file client_state_socket_io.c
 * @brief Client state socket I/O: poll, pop_line, send.
 *
 * Non-static functions: poll, pop_line, send (3).
 */

#include "ferrum/editor/client/client_state_socket.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * @brief Try to accept a pending connection on the listen socket.
 *
 * If a controller is already connected, skip. Accepts at most one client.
 * The accepted fd is set to non-blocking.
 *
 * @return true if a new client was accepted.
 */
static bool try_accept_(client_state_socket_t *css) {
    if (css->client_fd >= 0) return false;  /* Already connected. */
    if (css->listen_fd < 0) return false;

    int fd = accept(css->listen_fd, NULL, NULL);
    if (fd < 0) return false;  /* EAGAIN or real error. */

    /* Set non-blocking. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    css->client_fd = fd;
    css->recv_len  = 0;  /* Reset buffer for new connection. */
    return true;
}

/**
 * @brief Read available data from the connected controller.
 *
 * Handles disconnect detection (recv returns 0).
 *
 * @return true if data was read.
 */
static bool try_recv_(client_state_socket_t *css) {
    if (css->client_fd < 0) return false;

    uint32_t space = CLIENT_STATE_RECV_BUF - css->recv_len;
    if (space == 0) return false;  /* Buffer full. */

    ssize_t n = recv(css->client_fd, css->recv_buf + css->recv_len,
                     space, 0);
    if (n > 0) {
        css->recv_len += (uint32_t)n;
        return true;
    }

    if (n == 0) {
        /* Controller disconnected. */
        close(css->client_fd);
        css->client_fd = -1;
        css->recv_len  = 0;
        return false;
    }

    /* EAGAIN/EWOULDBLOCK = no data available (normal for non-blocking). */
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;
    }

    /* Real error — disconnect. */
    close(css->client_fd);
    css->client_fd = -1;
    css->recv_len  = 0;
    return false;
}

bool client_state_socket_poll(client_state_socket_t *css) {
    if (!css) return false;
    if (css->listen_fd < 0) return false;

    /* Build pollfd array: listen_fd always, client_fd if connected. */
    struct pollfd fds[2];
    int nfds = 0;

    fds[0].fd      = css->listen_fd;
    fds[0].events  = POLLIN;
    fds[0].revents = 0;
    nfds = 1;

    if (css->client_fd >= 0) {
        fds[1].fd      = css->client_fd;
        fds[1].events  = POLLIN;
        fds[1].revents = 0;
        nfds = 2;
    }

    /* Non-blocking poll (timeout = 0). */
    int ready = poll(fds, (nfds_t)nfds, 0);
    if (ready <= 0) return false;

    bool activity = false;

    /* Check for pending connection on listen socket. */
    if (fds[0].revents & POLLIN) {
        activity |= try_accept_(css);
    }

    /* Check for incoming data on client socket. */
    if (nfds == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
        activity |= try_recv_(css);
    }

    return activity;
}

uint32_t client_state_socket_pop_line(client_state_socket_t *css,
                                       char *buf, uint32_t buf_cap) {
    if (!css || !buf || buf_cap == 0) return 0;

    /* Find newline in recv_buf. */
    char *nl = memchr(css->recv_buf, '\n', css->recv_len);
    if (!nl) return 0;

    uint32_t line_len = (uint32_t)(nl - css->recv_buf);
    if (line_len >= buf_cap) {
        line_len = buf_cap - 1;
    }

    memcpy(buf, css->recv_buf, line_len);
    buf[line_len] = '\0';

    /* Shift remaining data forward. */
    uint32_t consumed = (uint32_t)(nl - css->recv_buf) + 1;
    uint32_t remaining = css->recv_len - consumed;
    if (remaining > 0) {
        memmove(css->recv_buf, nl + 1, remaining);
    }
    css->recv_len = remaining;

    return line_len;
}

bool client_state_socket_send(client_state_socket_t *css,
                               const char *json, uint32_t len) {
    if (!css || !json || len == 0 || css->client_fd < 0) return false;

    /* Build message with newline appended in a single buffer
     * to avoid split-send issues with non-blocking receivers. */
    bool needs_nl = (json[len - 1] != '\n');
    uint32_t total = len + (needs_nl ? 1 : 0);

    char buf[CLIENT_STATE_RECV_BUF];
    if (total > sizeof(buf)) return false;

    memcpy(buf, json, len);
    if (needs_nl) buf[len] = '\n';

    ssize_t sent = send(css->client_fd, buf, total, MSG_NOSIGNAL);
    if (sent < 0 || (uint32_t)sent != total) {
        close(css->client_fd);
        css->client_fd = -1;
        css->recv_len  = 0;
        return false;
    }

    return true;
}
