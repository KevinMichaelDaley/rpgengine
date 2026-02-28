/**
 * @file client_state_push.c
 * @brief Push event senders for the client state socket.
 *
 * Non-static functions: push_cursor_moved, push_entity_clicked (2).
 */

#include "ferrum/editor/client/client_state_dispatch.h"
#include "ferrum/editor/client/client_state_socket.h"

#include <stdio.h>
#include <string.h>

void client_state_push_cursor_moved(client_state_dispatch_t *disp,
                                     float x, float y, float z) {
    if (!disp || !disp->socket) return;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"event\":\"cursor_moved\","
        "\"pos\":[%.6g,%.6g,%.6g],\"source\":\"mouse\"}",
        (double)x, (double)y, (double)z);
    if (n > 0) {
        client_state_socket_send(disp->socket, buf, (uint32_t)n);
    }
}

void client_state_push_entity_clicked(client_state_dispatch_t *disp,
                                       uint32_t entity_id,
                                       float x, float y, float z) {
    if (!disp || !disp->socket) return;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"event\":\"entity_clicked\","
        "\"entity\":%u,\"pos\":[%.6g,%.6g,%.6g]}",
        entity_id, (double)x, (double)y, (double)z);
    if (n > 0) {
        client_state_socket_send(disp->socket, buf, (uint32_t)n);
    }
}
