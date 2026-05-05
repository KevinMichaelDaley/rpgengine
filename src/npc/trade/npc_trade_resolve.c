/**
 * @file npc_trade_resolve.c
 * @brief TRADE_ACCEPT, TRADE_REJECT, timeout check, and combat exit.
 *
 * ≤4 non-static functions:
 *   1. npc_trade_accept
 *   2. npc_trade_reject
 *   3. npc_trade_combat_exit
 *   4. npc_trade_is_timed_out
 */

#include "ferrum/npc/npc_trade.h"

#include <string.h>

/**
 * @brief Accept the current trade.
 *
 * Both parties must be in BARTER_ACTIVE phase. Sets both to BARTER_RESOLVED.
 *
 * @param a Barter state of the accepting NPC.
 * @param b Barter state of the counter-party.
 * @return NPC_TRADE_OK or a negative error code.
 */
int npc_trade_accept(npc_barter_state_t *a, npc_barter_state_t *b) {
    if (!a || !b) return NPC_TRADE_ERR_NO_TARGET;
    if (a->phase != BARTER_ACTIVE || b->phase != BARTER_ACTIVE) {
        return NPC_TRADE_ERR_STATE;
    }
    a->phase = BARTER_RESOLVED;
    b->phase = BARTER_RESOLVED;
    return NPC_TRADE_OK;
}

/**
 * @brief Reject the current trade.
 *
 * Both parties must be in BARTER_PROPOSED or BARTER_ACTIVE.
 * Sets both to BARTER_RESOLVED.
 *
 * @param a Barter state of the rejecting NPC.
 * @param b Barter state of the counter-party.
 * @return NPC_TRADE_OK or a negative error code.
 */
int npc_trade_reject(npc_barter_state_t *a, npc_barter_state_t *b) {
    if (!a || !b) return NPC_TRADE_ERR_NO_TARGET;
    if (a->phase != BARTER_PROPOSED && a->phase != BARTER_ACTIVE) {
        return NPC_TRADE_ERR_STATE;
    }
    a->phase = BARTER_RESOLVED;
    b->phase = BARTER_RESOLVED;
    return NPC_TRADE_OK;
}

/**
 * @brief Force-exit both parties from trade due to combat.
 *
 * Resets both states to BARTER_NONE regardless of current phase.
 *
 * @param a Barter state of one party.
 * @param b Barter state of the other party.
 */
void npc_trade_combat_exit(npc_barter_state_t *a, npc_barter_state_t *b) {
    if (a) {
        memset(a, 0, sizeof(*a));
        a->phase = BARTER_NONE;
    }
    if (b) {
        memset(b, 0, sizeof(*b));
        b->phase = BARTER_NONE;
    }
}

/**
 * @brief Check if the trade has timed out.
 *
 * Only relevant in BARTER_PROPOSED or BARTER_ACTIVE. Returns false
 * for BARTER_NONE and BARTER_RESOLVED.
 *
 * @param state  Barter state to check.
 * @param now_us Current time in microseconds.
 * @return true if the trade has exceeded its deadline.
 */
bool npc_trade_is_timed_out(const npc_barter_state_t *state, uint64_t now_us) {
    if (!state) return false;
    if (state->phase != BARTER_PROPOSED && state->phase != BARTER_ACTIVE) {
        return false;
    }
    return now_us >= state->timeout_deadline_us;
}
