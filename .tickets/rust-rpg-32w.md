---
id: rust-rpg-32w
status: open
deps: [rust-rpg-k75]
links: []
created: 2026-01-17T18:04:54.33773435-08:00
type: epic
priority: 2
---
# P_010 — Dialogue Graph System

## P_010 — Dialogue Graph System

### Design Intent
Implement node-based dialogue as a pure data graph traversal decoupled from UI. Conditions enable branching based on inventory/quest state.

### Specification
- Node: text, speaker_id
- Edge: target_node_id, condition
- Condition types:
  - `HAS_ITEM(item_id)`
  - `QUEST_STATE(quest_id, state)`
  - `ALWAYS`
- Runner:
  - holds current node
  - exposes options list and `select_option(i)` with validation

### Implementation Steps
1. Graph data structures.
2. Condition evaluation against world state snapshot.
3. Runner stepping and option filtering.

### Architectural Considerations
- UI should never mutate graph; runner owns traversal state.
- Conditions evaluated against read-only views for determinism.

### Unit Tests (RED-first)
**Happy Path**
1. **Graph traversal basic**
   - start node exposes options; selecting option advances to correct node.
2. **Conditional branches**
   - options differ based on having item.
3. **Quest-conditioned branch**
   - options differ based on quest state.

**Edge Cases**
4. **No available options**
   - runner reports “end of dialogue” state deterministically.
5. **Stable option ordering**
   - options list ordering is deterministic across runs.
6. **Self-loop / cycle handling**
   - graph cycles do not crash; runner can traverse repeatedly (or validator rejects cycles; document).

**Failure Modes**
7. **Invalid option handling**
   - selecting out-of-range returns error; node unchanged.
8. **Invalid node/edge references**
   - graph with missing target node fails validation.

### Regression Tests (RED-first)
1. **Option filtering after state change**
   - when inventory/quest state changes, the runner recomputes options correctly and does not expose stale ones.
2. **Condition evaluation purity**
   - evaluating conditions must not mutate world state; enforce via read-only views / const correctness.

### Cumulative Integration Tests (RED-first, cumulative through P_010)
1. **Dialogue driven by quest/inventory (P_000..P_010)**
   - run inventory and quest updates, then evaluate dialogue options; verify expected branches and determinism.

---



