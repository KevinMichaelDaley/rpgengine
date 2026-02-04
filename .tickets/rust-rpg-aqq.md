---
id: rust-rpg-aqq
status: closed
deps: [rust-rpg-31g]
links: []
created: 2026-01-17T18:02:49.091558547-08:00
type: task
priority: 2
---
# P_003 — ECS Core (Sparse Set)

## P_003 — ECS Core (Sparse Set)

### Design Intent
Provide cache-friendly component storage with O(1) insertion/removal and stable entity identity using generation counters.

### Specification
#### Entity
- `entity_t { uint32_t index; uint32_t generation; }`

#### SparseSet<T>
- `T* dense`
- `entity_t* dense_entities`
- `uint32_t* sparse` (entity_index → dense_index or UINT32_MAX)
- swap-remove on deletion

#### World
- Manages entity allocation/free lists and per-component stores.
- C11 has no templates; use one of:
  - **Macro-generated sparse sets** per component type, or
  - **Type-erased store** with size/stride + function pointers (more complex)
- For this project: **macro-generated** stores for performance and simplicity.

### Implementation Steps
1. Entity allocator with free list + generation array.
2. Sparse set insert/get/remove.
3. Macro to define `sparse_set_<name>` per component.
4. World registry for known component sets.

### Architectural Considerations
- Keep components POD; avoid pointers where possible.
- Swap-remove must update moved entity’s sparse entry.
- Queries return dense arrays for tight iteration.

### Unit Tests (RED-first)
**Happy Path**
1. **Entity create/destroy lifecycle**
   - create N entities; destroy a subset; create again; verify reuse with generation bump.
2. **Sparse-set insert/get/remove basic**
   - insert component for entity E; `get(E)` returns pointer; remove; `get(E)` returns NULL.
3. **Swap-removal integrity**
   - Insert A,B,C; remove B
   - Verify dense packed and C moved; sparse[C] updated.
4. **Remove last element**
   - remove the last dense entry; verify no swap needed and sparse cleared.
5. **Iterate dense arrays**
   - iteration visits all live components exactly once.

**Edge Cases**
6. **Duplicate insert**
   - inserting a component twice for the same entity must return explicit error or replace deterministically (document).
7. **Remove non-existent**
   - removing a component that isn’t present returns explicit “not found” and makes no changes.
8. **Sparse sentinel correctness**
   - sparse entries for missing components are `UINT32_MAX` (or chosen sentinel) and remain stable.
9. **Capacity growth behavior (if resizable)**
   - growth preserves all mappings; dense order either preserved or documented.

**Failure Modes**
10. **Generation mismatch access**
   - Create entity E; destroy; create new entity reusing index with new generation
   - Old handle must not access components.
11. **Invalid entity index**
   - `get/remove` for out-of-range entity index returns error and does not crash.
12. **World entity exhaustion**
   - creating beyond max entity capacity returns error.

### Regression Tests (RED-first)
1. **Sparse update on swap**
   - after removal swaps last element into hole, moved entity’s sparse index must update.
2. **Stale pointers invalidation**
   - pointers returned by `get` must not remain valid after remove/swap (document), and tests should ensure users don’t rely on them.
3. **Free-list reuse without generation bump bug**
   - ensure every reuse increments generation exactly once.

### Cumulative Integration Tests (RED-first, cumulative through P_003)
1. **ECS system loop determinism (P_000..P_003)**
   - dispatch jobs that run a deterministic “system” over dense component arrays (single-threaded or explicitly synchronized).
   - verify entity/component counts and computed math outputs are stable across runs.

---



