/**
 * @file ctrl_conn.c
 * @brief Controller TCP connection lifecycle: init, connect, disconnect.
 *
 * Non-static functions: init, connect, disconnect (3).
 */

#include "ferrum/editor/ctrl_conn.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool ctrl_conn_init(ctrl_conn_t *conn) {
    if (!conn) return false;
    memset(conn, 0, sizeof(*conn));
    conn->fd    = -1;
    conn->state = CTRL_CONN_DISCONNECTED;
    conn->recv_buf = (char *)malloc(CTRL_CONN_RECV_BUF);
    if (!conn->recv_buf) return false;
    conn->recv_cap = CTRL_CONN_RECV_BUF;
    return true;
}

void ctrl_conn_destroy(ctrl_conn_t *conn) {
    if (!conn) return;
    ctrl_conn_disconnect(conn);
    free(conn->recv_buf);
    conn->recv_buf = NULL;
    conn->recv_cap = 0;
}

bool ctrl_conn_connect(ctrl_conn_t *conn, const char *host, uint16_t port) {
    if (!conn || !host) return false;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        return false;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    /* Set non-blocking for future recv calls. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    conn->fd       = fd;
    conn->state    = CTRL_CONN_CONNECTED;
    conn->recv_len = 0;
    conn->next_id  = 1;
    return true;
}

void ctrl_conn_disconnect(ctrl_conn_t *conn) {
    if (!conn) return;
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    conn->state    = CTRL_CONN_DISCONNECTED;
    conn->recv_len = 0;
}
