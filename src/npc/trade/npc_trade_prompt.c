/**
 * @file npc_trade_prompt.c
 * @brief Engine-generated barter prompt text for LLM consumption.
 *
 * ≤4 non-static functions:
 *   1. npc_trade_prompt
 *   2. npc_trade_prompt_fmt
 */

#include "ferrum/npc/npc_trade.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Look up an item name by ID, or return "none" for 0.
 *
 * @param item_id    Item identifier.
 * @param item_count Number of item names.
 * @param item_names Array of item name strings (index item_id - 1).
 * @return Item name string.
 */
static const char *resolve_item_name(uint32_t item_id, uint32_t item_count,
                                     const char *const *item_names) {
    if (item_id == 0) return "none";
    if (!item_names || item_count == 0) return "none";
    if (item_id - 1 < item_count && item_names[item_id - 1]) {
        return item_names[item_id - 1];
    }
    return "unknown";
}

/**
 * @brief Generate a barter prompt for the LLM.
 *
 * Writes a human-readable trade status string to the output buffer.
 * Uses "none" for unset item slots.
 *
 * @param state            Barter state.
 * @param buf              Output buffer.
 * @param cap              Capacity of output buffer.
 * @param counter_party_name Name of the other party.
 * @return Number of characters written (excluding null terminator), or -1 on error.
 */
int npc_trade_prompt(const npc_barter_state_t *state,
                     char *buf, size_t cap,
                     const char *counter_party_name) {
    return npc_trade_prompt_fmt(state, buf, cap, counter_party_name, 0, NULL);
}

/**
 * @brief Generate a barter prompt with item name lookup.
 *
 * Like npc_trade_prompt() but resolves item IDs to names using
 * the provided item_count/names arrays.
 *
 * @param state            Barter state.
 * @param buf              Output buffer.
 * @param cap              Capacity of output buffer.
 * @param counter_party_name Name of the other party.
 * @param item_count       Number of known item names.
 * @param item_names       Array of item name strings indexed by item_id - 1.
 * @return Number of characters written (excluding null terminator), or -1 on error.
 */
int npc_trade_prompt_fmt(const npc_barter_state_t *state,
                         char *buf, size_t cap,
                         const char *counter_party_name,
                         uint32_t item_count,
                         const char *const *item_names) {
    if (!buf || cap == 0) return -1;
    if (!counter_party_name) counter_party_name = "unknown";

    const char *offer = "none";
    const char *want  = "none";
    const char *their_offer = "none";
    const char *their_want  = "none";

    if (state) {
        if (state->my_offer_item_id != 0 || state->their_offer_item_id != 0 ||
            state->my_ask_item_id != 0 || state->their_ask_item_id != 0) {
            offer = resolve_item_name(state->my_offer_item_id, item_count, item_names);
            want  = resolve_item_name(state->my_ask_item_id, item_count, item_names);
            their_offer = resolve_item_name(state->their_offer_item_id, item_count, item_names);
            their_want  = resolve_item_name(state->their_ask_item_id, item_count, item_names);
        }
    }

    return snprintf(buf, cap,
                    "Trade state with %s:\n"
                    "  You offer: %s\n"
                    "  You want:  %s\n"
                    "  They offer: %s\n"
                    "  They want:  %s",
                    counter_party_name,
                    offer, want, their_offer, their_want);
}
