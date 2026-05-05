/**
 * @file npc_state_compact.c
 * @brief Context compaction: truncates oldest text when over token budget.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_state_compact
 */

#include "ferrum/npc/npc_state_manager.h"
#include <string.h>

static uint32_t token_estimate_(const char *buf, uint32_t len) {
    (void)buf;
    if (len == 0) return 0;
    if (len <= 3) return 1;
    return (len / 4) + 1;
}

bool npc_state_compact(npc_state_t *npc) {
    if (!npc || !npc->context_buffer || npc->context_len == 0) {
        if (npc) npc->context_dirty = false;
        return false;
    }
    if (npc->context_token_estimate <= npc->context_max_tokens) {
        npc->context_dirty = false;
        return true;
    }

    uint32_t remove_len = npc->context_len / 2;
    if (remove_len == 0) remove_len = npc->context_len;

    uint32_t keep_len = npc->context_len - remove_len;
    if (keep_len > 0) {
        memmove(npc->context_buffer,
                npc->context_buffer + remove_len, keep_len);
    }
    npc->context_len = keep_len;
    npc->context_buffer[keep_len] = '\0';

    npc->context_token_estimate = token_estimate_(npc->context_buffer,
                                                   npc->context_len);
    npc->context_dirty = (npc->context_token_estimate >
                          npc->context_max_tokens);
    return true;
}
