---
id: rpg-llm02c
status: open
deps: [rpg-llm02e]
links: []
created: 2026-04-26T01:20:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, npc, ai, trade, barter, economy]
---
# Trade Tools: TRADE_INIT, TRADE_SELL, TRADE_BUY, TRADE_ACCEPT, TRADE_REJECT

Implement all 5 trade tools via the `tool_action` opcode. When an NPC enters a trade loop, it stays in that state until resolved, rejected, timed out, or combat interrupts.

## Requirements

### Barter State Machine
```c
typedef enum {
    BARTER_NONE,      /* not in any trade */
    BARTER_PROPOSED,  /* waiting for counter-party response */
    BARTER_ACTIVE,    /* both parties confirmed, exchanging offers */
    BARTER_RESOLVED,  /* deal accepted or rejected, auto-exit next tick */
} npc_barter_phase_t;

typedef struct npc_barter_state {
    npc_barter_phase_t phase;
    uint64_t           counter_party_id;
    uint64_t           timeout_deadline_us;
    uint32_t           my_offer_item_id;
    uint32_t           my_ask_item_id;
    uint32_t           their_offer_item_id;
    uint32_t           their_ask_item_id;
} npc_barter_state_t;
```

### TRADE_INIT (tool_id = 0)
- 0 arguments
- Validation: `phase == BARTER_NONE`; nearest friendly within 2m also `BARTER_NONE`; `language_similarity >= 0.3`; neither in combat
- Success: both parties → `BARTER_PROPOSED`
- Errors: `"already in a trade"`, `"no friendly entity within 2m"`, `"target is already trading"`, `"language barrier"`, `"in combat"`

### TRADE_SELL (tool_id = 1)
- 0–1 argument (`{"item": "name"}`)
- In trade loop: sets `my_offer_item_id`, updates counter-party state
- Not in trade loop: broadcasts `TRADE_OFFER_BROADCAST` to friendly entities within 10m
- Errors: `"item not in inventory"`, `"invalid item name"`

### TRADE_BUY (tool_id = 2)
- 0–1 argument (`{"item": "name"}`)
- In trade loop: sets `my_ask_item_id`, updates counter-party state
- Not in trade loop: broadcasts `WANT_TO_BUY_BROADCAST` within 10m
- Errors: `"invalid item name"`

### TRADE_ACCEPT (tool_id = 3)
- 0 arguments
- Validation: offer/ask pair set; counter-party is player (UI) or also called `TRADE_ACCEPT`
- Success: transfer items, both → `BARTER_RESOLVED`, auto-exit next tick

### TRADE_REJECT (tool_id = 4)
- 0 arguments
- Success: both → `BARTER_RESOLVED`, auto-exit next tick

### Combat Auto-Exit
Taking damage or being targeted by ATTACK → `BARTER_NONE` for both parties.

### Timeout
120 seconds from `TRADE_INIT`. Auto-exit on expiry.

### Engine-Generated Prompt
```
Trade state with <name>:
  You offer: <item_or_none>
  You want:  <item_or_none>
  They offer: <item_or_none>
  They want:  <item_or_none>
```

## Files to Create
- `include/ferrum/npc/npc_trade.h` — barter state struct, phase enum, error codes
- `src/npc/trade/npc_trade_init.c` — TRADE_INIT validation + state transition
- `src/npc/trade/npc_trade_sell.c` — TRADE_SELL in-loop + broadcast
- `src/npc/trade/npc_trade_buy.c` — TRADE_BUY in-loop + broadcast
- `src/npc/trade/npc_trade_resolve.c` — TRADE_ACCEPT, TRADE_REJECT, timeout, combat exit
- `src/npc/trade/npc_trade_prompt.c` — engine-generated barter prompt text
- `tests/npc/npc_trade_state_tests.c` — state machine transitions, combat exit, timeout, broadcast

## Acceptance
- [ ] Two NPCs enter trade via `TRADE_INIT`, exchange `TRADE_SELL`/`TRADE_BUY`, resolve with `TRADE_ACCEPT`.
- [ ] NPC auto-leaves trade when damaged.
- [ ] Trade times out after 120s.
- [ ] `TRADE_SELL` outside trade broadcasts to nearby friendly NPCs.
- [ ] Player-NPC trade uses same state machine.
- [ ] All error messages returned to LLM as tool result text.
