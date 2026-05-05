/**
 * @file npc_trade_buy.c
 * @brief TRADE_BUY: set ask item in trade loop, or broadcast outside.
 *
 * ≤4 non-static functions:
 *   1. npc_trade_buy
 */

#include "ferrum/npc/npc_trade.h"

/**
 * @brief Set the ask item for a trade.
 *
 * If their_state is non-NULL (in a trade loop), sets my_ask_item_id
 * and updates their_ask_item_id on the counter-party. Auto-transitions
 * both to BARTER_ACTIVE if both parties have set items.
 *
 * If their_state is NULL (not in a trade), returns NPC_TRADE_BROADCAST.
 *
 * @param my_state    This NPC's barter state (must be BARTER_PROPOSED or BARTER_ACTIVE).
 * @param their_state Counter-party's barter state, or NULL if not in a trade.
 * @param item_id     Item ID to request.
 * @return NPC_TRADE_OK, NPC_TRADE_BROADCAST, or a negative error code.
 */
int npc_trade_buy(npc_barter_state_t *my_state, npc_barter_state_t *their_state,
                  uint32_t item_id) {
    if (!my_state) return NPC_TRADE_ERR_NO_TRADE;
    if (my_state->phase != BARTER_PROPOSED && my_state->phase != BARTER_ACTIVE) {
        return NPC_TRADE_BROADCAST;
    }
    if (!their_state) return NPC_TRADE_BROADCAST;

    my_state->my_ask_item_id        = item_id;
    their_state->their_ask_item_id  = item_id;

    /* Auto-transition to ACTIVE if all slots are filled. */
    if (my_state->phase == BARTER_PROPOSED &&
        (my_state->my_offer_item_id != 0 || my_state->my_ask_item_id != 0) &&
        (their_state->my_offer_item_id != 0 || their_state->my_ask_item_id != 0)) {
        my_state->phase    = BARTER_ACTIVE;
        their_state->phase = BARTER_ACTIVE;
    }

    return NPC_TRADE_OK;
}
