/**
 * @file ctrl_conn_io.c
 * @brief Controller TCP connection I/O: send, recv, pop_line.
 *
 * Non-static functions: send_cmd, send_raw, recv, pop_line (4).
 */

#include "ferrum/editor/ctrl_conn.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

bool ctrl_conn_send_cmd(ctrl_conn_t *conn, const char *cmd) {
    if (!conn || !cmd || conn->state != CTRL_CONN_CONNECTED) return false;

    /* Format: {"id":N,"cmd":"<text>","args":{}}\n */
    char buf[CTRL_CONN_SEND_BUF];
    int n = snprintf(buf, sizeof(buf),
                     "{\"id\":%u,\"cmd\":\"%s\",\"args\":{}}\n",
                     conn->next_id, cmd);
    if (n < 0 || (uint32_t)n >= sizeof(buf)) return false;

    ssize_t sent = send(conn->fd, buf, (size_t)n, MSG_NOSIGNAL);
    if (sent != n) {
        conn->state = CTRL_CONN_ERROR;
        return false;
    }

    conn->next_id++;
    return true;
}

bool ctrl_conn_send_raw(ctrl_conn_t *conn, const char *json, uint32_t len) {
    if (!conn || !json || len == 0 || conn->state != CTRL_CONN_CONNECTED)
        return false;

    /* Send the JSON payload. */
    ssize_t sent = send(conn->fd, json, len, MSG_NOSIGNAL);
    if (sent < 0 || (uint32_t)sent != len) {
        conn->state = CTRL_CONN_ERROR;
        return false;
    }

    /* Append newline if not already present. */
    if (json[len - 1] != '\n') {
        char nl = '\n';
        if (send(conn->fd, &nl, 1, MSG_NOSIGNAL) != 1) {
            conn->state = CTRL_CONN_ERROR;
            return false;
        }
    }

    return true;
}

bool ctrl_conn_recv(ctrl_conn_t *conn) {
    if (!conn || conn->state != CTRL_CONN_CONNECTED) return false;

    uint32_t space = conn->recv_cap - conn->recv_len;
    if (space == 0) return false;  /* Buffer full. */

    ssize_t n = recv(conn->fd, conn->recv_buf + conn->recv_len,
                     space, 0);
    if (n > 0) {
        conn->recv_len += (uint32_t)n;
        return true;
    }

    if (n == 0) {
        /* Server closed connection. */
        conn->state = CTRL_CONN_ERROR;
        return false;
    }

    /* EAGAIN/EWOULDBLOCK means no data available (non-blocking). */
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return false;  /* No data, not an error. */
    }

    conn->state = CTRL_CONN_ERROR;
    return false;
}

uint32_t ctrl_conn_pop_line(ctrl_conn_t *conn, char *buf, uint32_t buf_cap) {
    if (!conn || !buf || buf_cap == 0) return 0;

    /* Find newline in recv_buf. */
    char *nl = memchr(conn->recv_buf, '\n', conn->recv_len);
    if (!nl) return 0;

    uint32_t line_len = (uint32_t)(nl - conn->recv_buf);
    if (line_len >= buf_cap) {
        line_len = buf_cap - 1;
    }

    memcpy(buf, conn->recv_buf, line_len);
    buf[line_len] = '\0';

    /* Shift remaining data forward. */
    uint32_t consumed = (uint32_t)(nl - conn->recv_buf) + 1;
    uint32_t remaining = conn->recv_len - consumed;
    if (remaining > 0) {
        memmove(conn->recv_buf, nl + 1, remaining);
    }
    conn->recv_len = remaining;

    return line_len;
}
