/**
 * @file npc_trade.h
 * @brief NPC barter state machine: types, error codes, and trade tool functions.
 *
 * Public types: npc_barter_phase_t, npc_barter_state_t
 */

#ifndef FERRUM_NPC_TRADE_H
#define FERRUM_NPC_TRADE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ======================================================================= */
/* Barter phase                                                            */
/* ======================================================================= */

/**
 * @brief Phase of the barter state machine.
 */
typedef enum {
    BARTER_NONE     = 0, /**< Not in any trade. */
    BARTER_PROPOSED = 1, /**< Waiting for counter-party response. */
    BARTER_ACTIVE   = 2, /**< Both parties confirmed, exchanging offers. */
    BARTER_RESOLVED = 3, /**< Deal accepted or rejected, auto-exit next tick. */
} npc_barter_phase_t;

/* ======================================================================= */
/* Barter state                                                            */
/* ======================================================================= */

/**
 * @brief Per-NPC barter state for trade negotiations.
 */
typedef struct npc_barter_state {
    npc_barter_phase_t phase;              /**< Current state machine phase. */
    uint64_t           counter_party_id;    /**< Entity ID of the other party. */
    uint64_t           timeout_deadline_us; /**< Deadline for auto-timeout. */
    uint32_t           my_offer_item_id;    /**< Item this NPC is offering. */
    uint32_t           my_ask_item_id;      /**< Item this NPC wants. */
    uint32_t           their_offer_item_id; /**< Item the other party is offering. */
    uint32_t           their_ask_item_id;   /**< Item the other party wants. */
} npc_barter_state_t;

/* ======================================================================= */
/* Error codes                                                             */
/* ======================================================================= */

#define NPC_TRADE_OK              0  /**< Success. */
#define NPC_TRADE_ERR_ALREADY     -1 /**< Already in a trade. */
#define NPC_TRADE_ERR_NO_TARGET   -2 /**< No friendly entity within range. */
#define NPC_TRADE_ERR_TARGET_BUSY -3 /**< Target is already trading. */
#define NPC_TRADE_ERR_LANGUAGE    -4 /**< Language barrier. */
#define NPC_TRADE_ERR_COMBAT      -5 /**< In combat. */
#define NPC_TRADE_ERR_INVENTORY   -6 /**< Item not in inventory. */
#define NPC_TRADE_ERR_INVALID_ITEM -7 /**< Invalid item name. */
#define NPC_TRADE_ERR_NO_TRADE    -8 /**< Not in a trade. */
#define NPC_TRADE_ERR_INCOMPLETE  -9 /**< Offer/ask not set. */
#define NPC_TRADE_ERR_STATE       -10 /**< Invalid state for operation. */
#define NPC_TRADE_BROADCAST       1  /**< Special: not in trade, broadcast intent. */

/* ======================================================================= */
/* Constants                                                               */
/* ======================================================================= */

#define NPC_TRADE_TIMEOUT_SECONDS 120

/* ======================================================================= */
/* Init / lifecycle                                                        */
/* ======================================================================= */

/**
 * @brief Initialize a barter state to BARTER_NONE.
 * @param state Pointer to the barter state to initialize.
 */
void npc_barter_state_init(npc_barter_state_t *state);

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
                   uint64_t a_id, uint64_t b_id, uint64_t now_us);

/* ======================================================================= */
/* Offer / ask                                                             */
/* ======================================================================= */

/**
 * @brief Set the offer item for a trade.
 *
 * If their_state is non-NULL (in a trade loop), sets my_offer_item_id
 * and updates their_offer_item_id on the counter-party. Auto-transitions
 * both to BARTER_ACTIVE if both parties have set items.
 *
 * If their_state is NULL (not in a trade), returns NPC_TRADE_BROADCAST.
 *
 * @param my_state    This NPC's barter state (must be BARTER_PROPOSED or BARTER_ACTIVE).
 * @param their_state Counter-party's barter state, or NULL if not in a trade.
 * @param item_id     Item ID to offer.
 * @return NPC_TRADE_OK, NPC_TRADE_BROADCAST, or a negative error code.
 */
int npc_trade_sell(npc_barter_state_t *my_state, npc_barter_state_t *their_state,
                   uint32_t item_id);

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
                  uint32_t item_id);

/* ======================================================================= */
/* Resolve                                                                 */
/* ======================================================================= */

/**
 * @brief Accept the current trade.
 *
 * Both parties must be in BARTER_ACTIVE phase. Sets both to BARTER_RESOLVED.
 *
 * @param a Barter state of the accepting NPC.
 * @param b Barter state of the counter-party.
 * @return NPC_TRADE_OK or a negative error code.
 */
int npc_trade_accept(npc_barter_state_t *a, npc_barter_state_t *b);

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
int npc_trade_reject(npc_barter_state_t *a, npc_barter_state_t *b);

/* ======================================================================= */
/* Combat / timeout                                                        */
/* ======================================================================= */

/**
 * @brief Force-exit both parties from trade due to combat.
 *
 * Resets both states to BARTER_NONE regardless of current phase.
 *
 * @param a Barter state of one party.
 * @param b Barter state of the other party.
 */
void npc_trade_combat_exit(npc_barter_state_t *a, npc_barter_state_t *b);

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
bool npc_trade_is_timed_out(const npc_barter_state_t *state, uint64_t now_us);

/* ======================================================================= */
/* Prompt                                                                  */
/* ======================================================================= */

/**
 * @brief Generate a barter prompt for the LLM.
 *
 * Writes a human-readable trade status string to the output buffer.
 * Uses "none" for unset item slots when no item table is provided.
 *
 * @param state            Barter state.
 * @param buf              Output buffer.
 * @param cap              Capacity of output buffer.
 * @param counter_party_name Name of the other party.
 * @return Number of characters written (excluding null terminator), or -1 on error.
 */
int npc_trade_prompt(const npc_barter_state_t *state,
                     char *buf, size_t cap,
                     const char *counter_party_name);

/**
 * @brief Generate a barter prompt with item name lookup.
 *
 * Like npc_trade_prompt() but resolves item IDs to names using
 * the provided item_count/names arrays. Item ID 0 is "none".
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
                         const char *const *item_names);

/* ======================================================================= */
/* Error strings                                                           */
/* ======================================================================= */

/**
 * @brief Get a human-readable error string for a trade error code.
 *
 * @param err Error code (NPC_TRADE_OK, NPC_TRADE_ERR_*, or NPC_TRADE_BROADCAST).
 * @return Pointer to a static string. Never returns NULL.
 */
const char *npc_trade_error_str(int err);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_TRADE_H */
