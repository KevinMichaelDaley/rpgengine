/**
 * @file npc_trade_init.c
 * @brief Barter state initialization and npc_trade_init state transition.
 *
 * ≤4 non-static functions:
 *   1. npc_barter_state_init
 *   2. npc_trade_init
 *   3. npc_trade_error_str
 */

#include "ferrum/npc/npc_trade.h"

#include <string.h>

/**
 * @brief Initialize a barter state to BARTER_NONE.
 * @param state Pointer to the barter state to initialize.
 */
void npc_barter_state_init(npc_barter_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->phase = BARTER_NONE;
}

/**
 * @brief Attempt to initiate a trade between two NPCs.
 *
 * Both must be BARTER_NONE. Sets both to BARTER_PROPOSED and records
 * counter-party IDs and timeout deadline.
 *
 * @param a      Barter state of the initiator (must be BARTER_NONE).
 * @param b      Barter state of the target (must be BARTER_NONE).
 * @param a_id   Entity ID of the initiator.
 * @param b_id   Entity ID of the target.
 * @param now_us Current time in microseconds.
 * @return NPC_TRADE_OK on success, or a negative error code.
 */
int npc_trade_init(npc_barter_state_t *a, npc_barter_state_t *b,
                   uint64_t a_id, uint64_t b_id, uint64_t now_us) {
    if (!a || !b) return NPC_TRADE_ERR_NO_TARGET;

    if (a->phase != BARTER_NONE) return NPC_TRADE_ERR_ALREADY;
    if (b->phase != BARTER_NONE) return NPC_TRADE_ERR_TARGET_BUSY;

    uint64_t deadline = now_us + (uint64_t)NPC_TRADE_TIMEOUT_SECONDS * 1000000ULL;

    a->phase              = BARTER_PROPOSED;
    a->counter_party_id   = b_id;
    a->timeout_deadline_us = deadline;
    a->my_offer_item_id    = 0;
    a->my_ask_item_id      = 0;
    a->their_offer_item_id = 0;
    a->their_ask_item_id   = 0;

    b->phase              = BARTER_PROPOSED;
    b->counter_party_id   = a_id;
    b->timeout_deadline_us = deadline;
    b->my_offer_item_id    = 0;
    b->my_ask_item_id      = 0;
    b->their_offer_item_id = 0;
    b->their_ask_item_id   = 0;

    return NPC_TRADE_OK;
}

/**
 * @brief Get a human-readable error string for a trade error code.
 *
 * @param err Error code (NPC_TRADE_OK, NPC_TRADE_ERR_*, or NPC_TRADE_BROADCAST).
 * @return Pointer to a static string. Never returns NULL.
 */
const char *npc_trade_error_str(int err) {
    switch (err) {
    case NPC_TRADE_OK:                return "ok";
    case NPC_TRADE_ERR_ALREADY:       return "already in a trade";
    case NPC_TRADE_ERR_NO_TARGET:     return "no friendly entity within 2m";
    case NPC_TRADE_ERR_TARGET_BUSY:   return "target is already trading";
    case NPC_TRADE_ERR_LANGUAGE:      return "language barrier";
    case NPC_TRADE_ERR_COMBAT:        return "in combat";
    case NPC_TRADE_ERR_INVENTORY:     return "item not in inventory";
    case NPC_TRADE_ERR_INVALID_ITEM:  return "invalid item name";
    case NPC_TRADE_ERR_NO_TRADE:      return "not in a trade";
    case NPC_TRADE_ERR_INCOMPLETE:    return "offer or ask not set";
    case NPC_TRADE_ERR_STATE:         return "invalid state for operation";
    case NPC_TRADE_BROADCAST:         return "broadcast";
    default:                          return "unknown trade error";
    }
}
