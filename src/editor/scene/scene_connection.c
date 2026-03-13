/**
 * @file scene_connection.c
 * @brief Scene connection lifecycle — init, destroy, connect, disconnect.
 */

#include "ferrum/editor/scene/scene_connection.h"

#include <stdio.h>
#include <string.h>

bool scene_connection_init(scene_connection_t *conn,
                           const scene_conn_config_t *config) {
    if (!conn) return false;
    if (!config || !config->host) return false;

    memset(conn, 0, sizeof(*conn));

    /* Copy host string to internal buffer */
    size_t host_len = strlen(config->host);
    if (host_len >= sizeof(conn->host_buf)) return false;
    memcpy(conn->host_buf, config->host, host_len + 1);

    /* Store config with internal host pointer */
    conn->config.host     = conn->host_buf;
    conn->config.tcp_port = config->tcp_port;
    conn->config.udp_port = config->udp_port;

    /* Initialize TCP connection (allocates recv buffer) */
    if (!ctrl_conn_init(&conn->tcp)) {
        return false;
    }

    /* UDP socket starts uninitialized until connect */
    memset(&conn->udp, 0, sizeof(conn->udp));
    conn->udp.fd = -1;

    conn->state        = SCENE_CONN_DISCONNECTED;
    conn->next_cmd_id  = 0;
    conn->pending_cmds = 0;
    conn->error_msg[0] = '\0';
    conn->initialized  = true;
    return true;
}

void scene_connection_destroy(scene_connection_t *conn) {
    if (!conn || !conn->initialized) return;
    scene_connection_disconnect(conn);
    ctrl_conn_destroy(&conn->tcp);
    conn->initialized = false;
}

bool scene_connection_connect(scene_connection_t *conn) {
    if (!conn || !conn->initialized) return false;

    /* Connect TCP */
    if (!ctrl_conn_connect(&conn->tcp, conn->config.host,
                           conn->config.tcp_port)) {
        snprintf(conn->error_msg, sizeof(conn->error_msg),
                 "TCP connect failed to %s:%u",
                 conn->config.host, conn->config.tcp_port);
        conn->state = SCENE_CONN_ERROR;
        return false;
    }

    /* Open and configure UDP socket */
    if (net_udp_socket_open(&conn->udp) != NET_UDP_SOCKET_OK) {
        snprintf(conn->error_msg, sizeof(conn->error_msg),
                 "UDP socket open failed");
        ctrl_conn_disconnect(&conn->tcp);
        conn->state = SCENE_CONN_ERROR;
        return false;
    }
    net_udp_socket_set_nonblocking(&conn->udp, 1);

    /* Resolve server UDP address */
    /* Parse dotted-quad into bytes for net_udp_addr_ipv4 */
    uint8_t a, b, c, d;
    if (sscanf(conn->config.host, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) == 4) {
        net_udp_addr_ipv4(&conn->server_addr, a, b, c, d,
                          conn->config.udp_port);
    }

    conn->state = SCENE_CONN_CONNECTED;
    conn->pending_cmds = 0;
    return true;
}

void scene_connection_disconnect(scene_connection_t *conn) {
    if (!conn || !conn->initialized) return;
    ctrl_conn_disconnect(&conn->tcp);

    if (conn->udp.initialized) {
        net_udp_socket_close(&conn->udp);
    }

    conn->state = SCENE_CONN_DISCONNECTED;
}
