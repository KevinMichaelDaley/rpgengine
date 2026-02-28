/**
 * @file client_editor_input.c
 * @brief Editor input processor: mouse events → push events.
 *
 * Non-static functions: init, click, right_click, box_select (4).
 */

#include "ferrum/editor/client/client_editor_input.h"
#include "ferrum/editor/client/client_state_dispatch.h"
#include "ferrum/editor/client/client_state_socket.h"

#include <stdio.h>
#include <string.h>

void editor_input_init(editor_input_t *input,
                       client_state_dispatch_t *dispatch) {
    if (!input) return;
    memset(input, 0, sizeof(*input));
    input->dispatch = dispatch;
}

void editor_input_click(editor_input_t *input, uint32_t entity_id,
                        float wx, float wy, float wz) {
    if (!input || !input->dispatch) return;

    if (entity_id != 0) {
        /* Clicked on an entity. */
        client_state_push_entity_clicked(input->dispatch,
                                          entity_id, wx, wy, wz);
    } else {
        /* Clicked empty space — move cursor. */
        client_state_push_cursor_moved(input->dispatch, wx, wy, wz);
    }
}

void editor_input_right_click(editor_input_t *input, uint32_t entity_id,
                              float wx, float wy, float wz) {
    if (!input || !input->dispatch || !input->dispatch->socket) return;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "{\"event\":\"context_menu\","
        "\"entity\":%u,\"pos\":[%.6g,%.6g,%.6g]}",
        entity_id, (double)wx, (double)wy, (double)wz);
    if (n > 0) {
        client_state_socket_send(input->dispatch->socket,
                                  buf, (uint32_t)n);
    }
}

void editor_input_box_select(editor_input_t *input,
                             const uint32_t *entity_ids, uint32_t count) {
    if (!input || !input->dispatch || !input->dispatch->socket) return;
    if (!entity_ids || count == 0) return;

    /* Build JSON: {"event":"box_select","entities":[1,2,3]} */
    char buf[4096];
    int written = snprintf(buf, sizeof(buf),
        "{\"event\":\"box_select\",\"entities\":[");
    if (written < 0) return;

    for (uint32_t i = 0; i < count && (size_t)written < sizeof(buf) - 16; i++) {
        if (i > 0) {
            written += snprintf(buf + written, sizeof(buf) - (size_t)written,
                                ",");
        }
        written += snprintf(buf + written, sizeof(buf) - (size_t)written,
                            "%u", entity_ids[i]);
    }
    written += snprintf(buf + written, sizeof(buf) - (size_t)written, "]}");

    if (written > 0 && (size_t)written < sizeof(buf)) {
        client_state_socket_send(input->dispatch->socket,
                                  buf, (uint32_t)written);
    }
}
