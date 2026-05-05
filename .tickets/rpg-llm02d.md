---
id: rpg-llm02d
status: split
deps: [rpg-llm02e]
links: [rpg-llm0d-nav, rpg-llm0d-combat, rpg-llm10]
created: 2026-04-26T01:20:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, npc, ai, combat, navigation, defend, attack, flee, goto]
---
# Combat and Navigation Action Tools: DEFEND, ATTACK, FLEE, GOTO

**SPLIT into two tickets:**
- [`rpg-llm0d-nav`](rpg-llm0d-nav.md) — GOTO navigation tool (implementable now)
- [`rpg-llm0d-combat`](rpg-llm0d-combat.md) — DEFEND/ATTACK/FLEE combat tools (deferred until combat subsystem exists)

Original combined design retained below for reference.

## Requirements

### Tool IDs
| Tool | ID | Arguments | Max Args |
|------|----|-----------|----------|
| DEFEND | 5 | `{"target": "name"}` | 1 |
| ATTACK | 6 | `{"target": "name"}` | 1 |
| FLEE | 7 | `{"direction": "optional"}` | 1 |
| GOTO | 8 | `{"target": "location_name"}` | 1 |

### DEFEND
- Range check: target within 30m
- State check: combat active or threatened
- Engine action: set combat BT to "protect target" mode
- Error: `"DEFEND failed: target out of range (>30m)."`
- Error: `"DEFEND failed: not in combat or threatened."`

### ATTACK
- Range check: target within weapon range
- State check: not already fleeing
- Engine action: set combat BT to "engage target" mode
- Error: `"ATTACK failed: target out of weapon range."`
- Error: `"ATTACK failed: currently fleeing."`

### FLEE
- No range check
- State check: health < 30% or outnumbered
- Engine action: set combat BT to "escape" mode, pick nav destination away from threats
- Error: `"FLEE failed: health too high and not outnumbered."`

### GOTO
- Range check: target valid nav mesh point
- State check: not rooted/stunned
- Engine action: submit nav query, set locomotion BT
- Error: `"GOTO failed: invalid nav mesh target."`
- Error: `"GOTO failed: rooted/stunned."`

### Engine Event Format
All actions build an update and `SIGNAL` to engine event queue:
```c
typedef struct npc_action_event {
    uint32_t action_type;   /* DEFEND, ATTACK, FLEE, GOTO */
    uint64_t actor_id;
    uint64_t target_id;
    uint32_t param_hash;    /* direction for FLEE, location for GOTO */
} npc_action_event_t;
```

## Files to Create
- `include/ferrum/npc/npc_combat_action.h` — combat action types, event struct, error codes
- `include/ferrum/npc/npc_nav_action.h` — GOTO validation types
- `src/npc/combat/npc_combat_action.c` — DEFEND/ATTACK/FLEE validation + event build
- `src/npc/nav/npc_nav_action.c` — GOTO validation + nav mesh check
- `tests/npc/npc_combat_action_tests.c` — range checks, state checks, event emission
- `tests/npc/npc_nav_action_tests.c` — nav mesh validity, rooted state

## Acceptance
- [ ] DEFEND succeeds when target within 30m and combat active.
- [ ] ATTACK fails when target beyond weapon range.
- [ ] FLEE succeeds when health < 30%.
- [ ] GOTO submits nav query for valid target.
- [ ] All failures return specific error text to LLM.
- [ ] Success cases emit `npc_action_event_t` to engine queue.
