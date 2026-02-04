---
id: rust-rpg-31g
status: closed
deps: [rust-rpg-8eb]
links: []
created: 2026-01-17T18:02:48.948577384-08:00
type: task
priority: 2
---
# P_002 — Memory Systems (Arena + Pool)

## P_002 — Memory Systems (Arena + Pool)

### Design Intent
Eliminate unpredictable allocator behavior in the hot loop by using arena allocation for frame-temporary data and a typed pool for long-lived objects/components.

### Specification
#### Arena
- Backed by a byte buffer.
- `arena_alloc(alignment, size)` returns aligned pointer or NULL.
- `arena_reset()` resets offset to 0.
- Optional: `arena_mark/arena_pop_to_mark` for nested lifetimes.

#### Pool
- Fixed-type pool storing `T` in contiguous memory with free-list.
- Handles use `(index, generation)` or `(index)` + external gen array.
- `pool_alloc` returns handle; `pool_free(handle)` returns slot to free-list.
- `pool_get(handle)` validates generation.

### Implementation Steps
1. Arena struct with buffer, capacity, offset.
2. Alignment padding.
3. Pool arrays: dense storage + free list + generations.
4. Handle struct and validation helpers.

### Architectural Considerations
- Arena pointers become invalid after reset (document).
- Pool must avoid ABA bugs via generation.
- No per-alloc mallocs inside pool operations.

### Unit Tests (RED-first)
**Happy Path**
1. **Arena reuse address stability**
   - allocate 1000 ints; record first ptr
   - reset; allocate again; first ptr matches
2. **Arena alignment (common)**
   - alloc u8 then u64; u64 pointer aligned to 8
3. **Arena sequential allocations do not overlap**
   - allocate N fixed-size blocks; verify each range is disjoint and within buffer.
4. **Pool alloc/free reuse**
   - allocate handles A,B; free A; allocate C; C reuses A’s slot.
5. **Pool get returns stable pointer while alive**
   - `pool_get(handle)` returns same address until freed.

**Edge Cases**
6. **Arena out-of-memory returns NULL**
   - request `size > remaining` returns NULL and does not advance offset.
7. **Arena supports large alignment**
   - allocate with alignment 16/32/64; pointer must be aligned.
8. **Zero-size allocation semantics**
   - `arena_alloc(alignment, 0)` must return a consistent pointer or NULL (document), and must not corrupt state.
9. **Mark/pop nested lifetimes (if supported)**
   - take two marks; allocate; pop to inner mark; verify offset restored; pop to outer mark; restored again.
10. **Pool capacity boundary**
   - allocate exactly capacity; next alloc returns explicit failure.

**Failure Modes**
11. **Pool generation mismatch**
   - alloc handle A; free; alloc reuses index with incremented generation; old handle must fail validation.
12. **Invalid free**
   - freeing an invalid handle (bad index/generation) returns error and does not corrupt freelist.
13. **Double free**
   - freeing the same valid handle twice returns error and does not corrupt freelist.

### Regression Tests (RED-first)
1. **Alignment padding off-by-one**
   - sequences of mixed-size allocations must keep every returned pointer aligned.
2. **Freelist corruption guard**
   - randomized alloc/free sequences must not produce cycles or duplicates in freelist.
3. **Generation wrap behavior**
   - if generation is small (e.g., u16), define and test wrap semantics (error, saturate, or allow wrap with global epoch).

### Cumulative Integration Tests (RED-first, cumulative through P_002)
1. **Arena usage inside jobs (P_000 + P_001 + P_002)**
   - each job allocates scratch from a per-worker arena, computes math results, and resets arena at “frame end”.
   - verify no cross-job aliasing and outputs match reference.

---



