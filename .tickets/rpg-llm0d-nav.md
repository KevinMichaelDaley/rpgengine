---
id: rpg-llm0d-nav
status: closed
deps: [rpg-llm02e]
links: [rpg-llm02a]
created: 2026-04-26T23:30:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, npc, ai, navigation, goto, navmesh]
---
# Navigation Tool: GOTO

Implement the GOTO navigation tool via `tool_action` (tool_id = 8). Validates the target, checks mobility state, and submits an async nav query. The actual pathfinding executor is stubbed until the nav mesh system exists.

## Requirements

### GOTO (tool_id = 8)
```json
{"name": "GOTO", "arguments": {"target": "campfire_03"}}
```

- **Target resolution**: map the string `target` to a world position.
  - Named entities: look up by entity name attribute.
  - Arbitrary strings: consult a static location table (spawn points, landmarks).
  - Fails with `"GOTO failed: unknown target 'campfire_03'."` if unresolvable.

- **Mobility check**: actor must not be rooted or stunned.
  - Fails with `"GOTO failed: rooted/stunned."` if immobile.

- **Nav mesh validity**: the resolved position must lie on the walkable nav mesh.
  - Currently stubbed — always returns true if a position was resolved.
  - Fails with `"GOTO failed: target not on nav mesh."` when nav mesh check is implemented.

- **Engine action**: submit an async `AEGIS_TASK_NAV_QUERY` via the MPSC buffer.
  - Params: actor position → target position.
  - Result: path waypoints or failure sentinel.
  - Sets locomotion BT to "moving" state.

- **Success**: `"Moving to campfire_03."`

### Engine Event Format
```c
typedef struct npc_nav_event {
    uint32_t action_type;   /* GOTO */
    uint64_t actor_id;
    vec3_t   target_pos;
    uint32_t target_name_hash;
} npc_nav_event_t;
```

## Files to Create
- `include/ferrum/npc/npc_nav_action.h` — nav event types, target resolution API
- `src/npc/nav/npc_nav_action.c` — target resolution + GOTO validation + event build
- `tests/npc/npc_nav_action_tests.c` — target lookup, rooted state, event emission

## Files to Modify
- `src/aegis/ops/aegis_ops_tool.c` — wire tool_id=8 to real handler
- `src/aegis/ops/aegis_ops_nav.c` — GOTO tool handler (uses json_parse)

## Acceptance
- [ ] GOTO resolves named entity target to position.
- [ ] GOTO resolves landmark string to position.
- [ ] Rooted actor fails GOTO with correct error.
- [ ] Unknown target fails with correct error.
- [ ] Success emits `npc_nav_event_t` and submits async nav query.
- [ ] Nav query executor stub returns ERROR sentinel (acceptable until pathfinding exists).
