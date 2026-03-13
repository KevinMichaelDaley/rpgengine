/**
 * @file scene_connection_io.c
 * @brief Scene connection I/O — send, pump, pop, status.
 */

#include "ferrum/editor/scene/scene_connection.h"

#include <stdio.h>
#include <string.h>

bool scene_connection_send_cmd(scene_connection_t *conn, const char *cmd) {
    if (!conn || !conn->initialized || !cmd) return false;
    if (conn->state != SCENE_CONN_CONNECTED) return false;

    if (!ctrl_conn_send_cmd(&conn->tcp, cmd)) {
        return false;
    }
    conn->pending_cmds++;
    return true;
}

int scene_connection_pump(scene_connection_t *conn) {
    if (!conn || !conn->initialized) return 0;
    if (conn->state != SCENE_CONN_CONNECTED) return 0;

    int events = 0;

    /* Poll TCP for response data */
    if (ctrl_conn_recv(&conn->tcp)) {
        events++;
    }

    /* Poll UDP for snapshot data */
    if (conn->udp.initialized) {
        uint8_t pkt[2048];
        size_t pkt_len = 0;
        int rc = net_udp_socket_recv(&conn->udp, pkt, sizeof(pkt), &pkt_len);
        if (rc == NET_UDP_SOCKET_OK && pkt_len > 0) {
            /* TODO: feed to snapshot reassembler */
            events++;
        }
    }

    return events;
}

uint32_t scene_connection_pop_response(scene_connection_t *conn,
                                       char *buf, uint32_t buf_cap) {
    if (!conn || !conn->initialized || !buf || buf_cap == 0) return 0;
    if (conn->state != SCENE_CONN_CONNECTED) return 0;

    uint32_t len = ctrl_conn_pop_line(&conn->tcp, buf, buf_cap);
    if (len > 0 && conn->pending_cmds > 0) {
        conn->pending_cmds--;
    }
    return len;
}

uint32_t scene_connection_next_id(scene_connection_t *conn) {
    if (!conn || !conn->initialized) return 0;
    conn->next_cmd_id++;
    return conn->next_cmd_id;
}
