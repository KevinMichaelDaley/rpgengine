/**
 * @file editor_tick_drain.c
 * @brief Per-tick drain — processes pending commands from the ring.
 *
 * Called once per server tick. Peeks commands from the SPSC ring,
 * dispatches via the handler table, and pushes responses to the
 * response ring for the I/O thread to send back to clients.
 */

#include "ferrum/editor/editor_ctx.h"
#include <string.h>

uint32_t editor_tick_drain(editor_ctx_t *ctx) {
    if (!ctx || !ctx->initialized) return 0;

    uint32_t processed = 0;
    const edit_cmd_slot_t *slot;

    while ((slot = edit_cmd_ring_peek(&ctx->cmd_ring)) != NULL) {
        /* Dispatch the JSON command. */
        char resp[4096];
        uint32_t resp_len = edit_dispatch_exec(
            &ctx->dispatch,
            slot->payload, slot->payload_len,
            resp, sizeof(resp));

        /* Push response to the response ring. */
        if (resp_len > 0) {
            edit_cmd_ring_push(&ctx->resp_ring, slot->id,
                               resp, resp_len);
        }

        edit_cmd_ring_advance(&ctx->cmd_ring);
        processed++;
    }

    return processed;
}
