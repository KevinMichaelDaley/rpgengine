---
id: rust-rpg-k75
status: open
deps: [rust-rpg-it1, rust-rpg-nr7]
links: []
created: 2026-01-17T18:02:49.96751964-08:00
type: epic
priority: 2
---
# P_009 — Quest System (Event-driven DAG, Networked Event Log)

## P_009 — Quest System (Event-driven DAG, Networked Event Log)

### Design Intent
Implement server-authoritative quest progression using a networked event log, with deterministic DAG transitions, idempotent event handling, and replay for debugging. Clients receive reliable ordered updates and can display local UI while awaiting authoritative state.

### Specification
#### Quest Definition
- Static DAG: nodes (steps) with requirements and transitions; optional rewards.
- Validation: acyclic (or explicitly mark cycles); valid references.

#### Runtime Quest Log
- Map `quest_id → {current_step, progress, flags}` with timestamp.
- Local client ghost mirrors server state; authoritative updates apply via reliable ordered channel.

#### Networked Event Log
- Server records `event { id, type, payload, timestamp }` and applies to quest log.
- Client receives event stream and/or direct quest state updates (deltas/snapshots).
- Idempotent: duplicate events are ignored on server and client.

#### Sync Policies
- Reliable ordered messages for quest state changes and events.
- Snapshots: periodic quest-log snapshots with `state_hash` allow recovery if events were missed.
- Deltas: per-quest updates (current_step/progress) with varints.

#### Determinism & Replay
- Debug mode allows replaying the event log to rebuild quest state.
- Hash of quest log (`state_hash`) computed after each application.

### Implementation Steps
1. Quest definition tables + validator.
2. Runtime quest log storage with hashing.
3. Server event handler that updates log deterministically.
4. Reliable encode/decode for quest events and state deltas.
5. Client ghost quest log, idempotent event processing, snapshot recovery.
6. Debug replay tool to rebuild quest log from event stream.

### Architectural Considerations
- No scripting runtime required initially.
- Replayable/deterministic: event application is pure and order-dependent; tests cover ordering.
- Reliable ordered transport; avoid malloc in hot path.
- Clear separation: networking delivers events; quest system applies and validates.

### Unit Tests (RED-first)
**Happy Path**
1. **Single quest progression**
   - kill events increment; completion advances step.
2. **Multiple quests in log**
   - events route to the correct quest(s) and update only matching ones.
3. **Completion state stability**
   - completed quests remain completed on unrelated events.
4. **Event log roundtrip**
   - encode/decode events reliably; client applies and reaches identical state.
5. **Snapshot recovery**
   - client missing events applies snapshot and continues processing deltas correctly.

**Edge Cases**
6. **Out-of-order events**
   - reliable ordered ensures application in order; test buffering and correct application.
7. **Idempotent event handling**
   - duplicate IDs ignored; state unchanged.
8. **Large progress counts**
   - high kill counts do not overflow internal counters (or clamp with explicit rule).

**Failure Modes**
9. **Invalid transition**
   - events not matching current step do not advance.
10. **Invalid quest definition**
   - cycles or invalid step references rejected by validator.
11. **Unknown quest_id**
   - updating a missing quest returns explicit error.
12. **Malformed event payload**
   - decode failure returns error; state unchanged.

### Regression Tests (RED-first)
1. **No double-advance on boundary**
   - when progress hits the completion threshold, step advances once.
2. **No cross-quest contamination**
   - events for quest A must never advance quest B.
3. **Event ordering lock-in**
   - canonical ordering yields identical state across runs.
4. **State hash determinism**
   - identical event streams produce identical `state_hash` values.

### Cumulative Integration Tests (RED-first, cumulative through P_009)
1. **Server→Client quest sync (P_000..P_009)**
   - server generates event stream; client applies events and snapshots; quest logs match.
2. **Dialogue gating via quest state**
   - after syncing quest state, dialogue options match expected gating.

---



