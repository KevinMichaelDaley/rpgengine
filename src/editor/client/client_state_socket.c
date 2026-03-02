/**
 * @file client_state_socket.c
 * @brief Client state socket lifecycle: init, listen, destroy.
 *
 * Non-static functions: init, listen, destroy (3).
 */

#include "ferrum/editor/client/client_state_socket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void client_state_socket_init(client_state_socket_t *css) {
    if (!css) return;
    memset(css, 0, sizeof(*css));
    css->listen_fd = -1;
    css->client_fd = -1;
    css->port      = 0;
    css->recv_buf  = (char *)malloc(CLIENT_STATE_RECV_BUF);
    css->recv_cap  = css->recv_buf ? CLIENT_STATE_RECV_BUF : 0;
}

bool client_state_socket_listen(client_state_socket_t *css, uint16_t port) {
    if (!css) return false;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    /* Allow port reuse for quick restarts. */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    if (listen(fd, 1) < 0) {
        close(fd);
        return false;
    }

    /* Set non-blocking so accept() never stalls. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    /* Query actual port (useful when port=0). */
    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    if (getsockname(fd, (struct sockaddr *)&bound, &bound_len) == 0) {
        css->port = ntohs(bound.sin_port);
    }

    css->listen_fd = fd;
    return true;
}

void client_state_socket_destroy(client_state_socket_t *css) {
    if (!css) return;
    if (css->client_fd >= 0) {
        close(css->client_fd);
        css->client_fd = -1;
    }
    if (css->listen_fd >= 0) {
        close(css->listen_fd);
        css->listen_fd = -1;
    }
    free(css->recv_buf);
    css->recv_buf = NULL;
    css->recv_cap = 0;
    css->recv_len = 0;
    css->port     = 0;
}
