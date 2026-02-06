---
id: phys-111
status: open
deps: [phys-110]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.11: Island Build Stage (Stage 10)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 10: Island Build. Uses union-find to group connected bodies
into islands for parallel solving.

## Files to create

- `include/ferrum/physics/island_build.h`
- `src/physics/stages/island_build.c`
- `tests/physics/island_build_tests.c`

## API

```c
typedef struct phys_island_build_args_t {
    const phys_constraint_t *constraints;
    uint32_t constraint_count;
    uint32_t body_count;
    phys_island_list_t *islands_out;
    phys_frame_arena_t *arena;
} phys_island_build_args_t;

void phys_stage_island_build(const phys_island_build_args_t *args);
```

## Implementation

Uses the `phys_island_list_build` function from Step 0.11.

## Acceptance Criteria

- [ ] Connected bodies grouped into same island
- [ ] Separate components form separate islands
- [ ] Constraints assigned to correct island
- [ ] Static bodies handled correctly (don't merge islands)

## Test Cases

```c
// test_island_build_single
// All bodies connected → 1 island

// test_island_build_multiple
// Two separate groups → 2 islands

// test_island_build_static_break
// Two dynamic bodies connected through static → 2 islands
```
