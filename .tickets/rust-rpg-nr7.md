---
id: rust-rpg-nr7
status: open
deps: [rust-rpg-it1]
links: []
created: 2026-01-17T18:02:49.822104987-08:00
type: epic
priority: 2
---
# P_008 — Inventory & Modifiers (Networking & Transactions)

## P_008 — Inventory & Modifiers (Networking & Transactions)

### Design Intent
Inventory operations are server-authoritative transactions synchronized to clients via reliable ordered messages, with deterministic modifier aggregation and conflict resolution. Clients may optimistically perform UI actions, but authoritative state arrives via deltas/snapshots with hashes to detect divergence.

### Specification
#### Components
- `item_base_t { weight, max_stack }`
- `container_t { capacity, item_entity_ids[] }`
- `modifier_t { kind(Add/Mult), stat_id, value }`

#### Transactions (Server-Authoritative)
- Operations: `ADD`, `REMOVE`, `MOVE`, `SPLIT`, `MERGE`, `EQUIP`, `UNEQUIP`.
- Each transaction carries: `tx_id`, `actor_id`, source/target container/slot, item entity handle, counts.
- Server validates capacity, stacking rules, ownership, and emits result.

#### Networking Sync
- Reliable ordered channel for transactions and inventory state deltas.
- Client keeps a pending-ops buffer for optimistic UI; reconciles on authoritative results.
- Container snapshots include a compact item list and a `state_hash` (rolling hash over `(item_id, count)` pairs).
- Deltas encode per-slot changes (add/remove/count updates) with varints.

#### Modifier Aggregation
- Deterministic rule: sum additive modifiers, then apply multiplicative modifiers per stat.
- Aggregation runs on server and client; divergence detected via `derived_hash` of aggregated results.

#### Conflict Resolution
- If client pending op conflicts with authoritative state, client rolls back local prediction and applies server delta; UI reflects updated state.

#### Security
- Server clamps counts, validates ownership; rejects malformed or out-of-capacity ops; never trusts client counts.

### Implementation Steps
1. Components and container relations.
2. Transaction executor on server with validation/errors.
3. Client pending-ops buffer + reconciliation.
4. Reliable message encode/decode for transactions and container deltas.
5. `state_hash` and `derived_hash` computation functions.
6. Modifier aggregation pipeline (deterministic order).
7. Integration with ECS and networking layer (P_007).

### Architectural Considerations
- Contiguous container storage (dense arrays); fixed capacities.
- Handles/entity IDs only; no raw pointers.
- Deterministic aggregation order; document stat combination rules.
- Server authoritative; clients reconcile.
- No per-op mallocs in hot path; use arenas or fixed buffers.

### Unit Tests (RED-first)
**Happy Path**
1. **Add/remove item success**
   - add an item entity to a container; remove it; container contents update correctly.
2. **Stacking within max_stack**
   - add two stackable items; verify stacks merge up to `max_stack`.
3. **Modifier stacking**
   - base stat + additive + multiplicative yields expected result.
4. **Deterministic aggregation**
   - multiple modifiers applied in a fixed deterministic order.
5. **Transaction roundtrip**
   - client sends `ADD`; server validates and replies; client applies authoritative delta; final state matches server.
6. **State hash matches authoritative**
   - after applying server delta, `state_hash` equals server-provided value.
7. **Derived hash matches aggregation**
   - aggregated stats produce expected `derived_hash` on client and server.

**Edge Cases**
8. **max_stack == 1**
   - stacking is disabled; adding duplicates creates separate entries or fails (document).
9. **Zero capacity container**
   - any add fails with explicit error.
10. **Remove non-existent item**
   - removing an item not in container returns explicit error.
11. **Stat overflow-adjacent**
   - large additive/multiplicative values remain finite or clamp (document) and never produce NaNs.
12. **Out-of-order transaction delivery**
   - reliable ordered ensures application in order; test buffered application.
13. **Concurrent ops on same slot**
   - server resolves deterministically; client reconciles authoritative result.

**Failure Modes**
14. **Capacity enforcement**
   - adding beyond capacity fails with explicit error.
15. **Invalid handles**
   - invalid item entity IDs/handles must not crash; return error.
16. **Malformed delta**
   - decode error for slot change returns failure; state remains consistent.
17. **Hash mismatch**
   - client detects state hash mismatch and requests snapshot.

### Regression Tests (RED-first)
1. **No double-counting on re-equip**
   - equipping/unequipping the same item repeatedly must not accumulate phantom modifiers.
2. **Add-then-mult order lock-in**
   - aggregation rule locked by tests.
3. **Transaction idempotency**
   - duplicate `tx_id` is ignored; state unchanged.
4. **Deterministic reconciliation**
   - client prediction rollback consistently produces same final state.

### Cumulative Integration Tests (RED-first, cumulative through P_008)
1. **Server→Client inventory sync (P_000..P_008)**
   - server executes transactions; client applies deltas; hashes match; containers identical.
2. **Predicted UI ops**
   - client queues pending ops and reconciles with server; final state matches authoritative.
3. **Bandwidth budget under churn**
   - heavy item churn replicated within reliable budget; client stays consistent.

---



