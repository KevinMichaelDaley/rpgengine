/**
 * @file scene_connection_status.c
 * @brief Scene connection status formatting.
 */

#include "ferrum/editor/scene/scene_connection.h"

#include <stdio.h>

void scene_connection_format_status(const scene_connection_t *conn,
                                    char *buf, size_t cap) {
    if (!conn || !buf || cap == 0) return;

    switch (conn->state) {
        case SCENE_CONN_DISCONNECTED:
            snprintf(buf, cap, "Offline");
            break;

        case SCENE_CONN_CONNECTED:
            if (conn->pending_cmds > 0) {
                snprintf(buf, cap, "Syncing... (%u)", conn->pending_cmds);
            } else {
                snprintf(buf, cap, "Synced");
            }
            break;

        case SCENE_CONN_ERROR:
            if (conn->error_msg[0] != '\0') {
                snprintf(buf, cap, "Error: %s", conn->error_msg);
            } else {
                snprintf(buf, cap, "Error");
            }
            break;
    }
}
