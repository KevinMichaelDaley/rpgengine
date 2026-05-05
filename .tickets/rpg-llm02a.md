---
id: rpg-llm02a
status: closed
deps: [rpg-llm01]
links: [rpg-sense01]
created: 2026-04-26T01:20:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm02
tags: [aegis, llm, npc, ai, sense, query, async]
---
# SENSE_QUERY Async Opcode + Executor

Implement the `AEGIS_OP_SENSE_QUERY = 0x4B` async opcode and its dedicated executor. This is the LLM's window into the physical world.

## Requirements

### Opcode
```
sense_query r_handle, r_mode_flags, r_target
```
- `r_mode_flags`: lower 16 bits = mode (0=full sweep, 1=targeted), upper 16 bits = sense flags bitmask (LOS=1, PROXIMITY=2, AUDIO=4, SMELL=8, SHADOW=16)
- `r_target`: entity_id register (0 for full sweep)
- Fuel cost: 300

### Async Task Type
`AEGIS_TASK_SENSE_QUERY = 3` added to [`aegis_async.h`](engine/include/ferrum/aegis/aegis_async.h)

### Param Layout (24 bytes)
```
params[0..1]   = query_mode    (uint16_t: 0=full, 1=targeted)
params[2..3]   = sense_flags   (uint16_t bitmask)
params[4..7]   = target_entity (uint32_t entity_id, 0 = none)
params[8..19]  = npc_position  (float[3])
params[20..23] = max_range     (float, default 50.0f)
```

### Result Layout (heap arena)
```c
typedef struct aegis_sense_result {
    int32_t  status;          /* 0=ok, -1=error */
    uint32_t entity_count;
    uint32_t event_count;
    /* Followed by entity_count × aegis_sense_entity_t */
    /* Followed by event_count  × aegis_sense_event_t  */
} aegis_sense_result_t;

typedef struct aegis_sense_entity {
    uint32_t entity_id;
    float    distance;
    float    salience;        /* 0.0-1.0 composite score */
    uint16_t flags;           /* bitmask: visible, audible, smelled */
    char     name[];          /* null-terminated display name */
} aegis_sense_entity_t;

typedef struct aegis_sense_event {
    uint32_t event_type_hash;
    float    distance;
    float    salience;
    char     description[];   /* null-terminated short text */
} aegis_sense_event_t;
```

### Detection Types (approximated by distance)
| Type | Near (< 10m) | Medium (10-50m) | Far (> 50m) |
|------|-------------|-----------------|-------------|
| **Line-of-sight** | Exact raycast per entity | Raycast against BVH only | No LOS data |
| **Shadow coverage** | Full shadow map query | Approximate cell coverage | None |
| **Proximity** | `phys_overlap_sphere` exact | Spatial grid query | None |
| **Auditory** | Full audio propagation graph placeholder | Simplified (doors open/closed) | None |
| **Smell** | Exact wind + scent simulation placeholder | Downwind cone approximation | None |

### Auto-Sense Pipeline
Before every LLM prompt, the engine calls `npc_sense_auto_update(npc)`:
1. Submit implicit full sweep with all sense flags, max_range=50m
2. Wait for completion
3. Compare to previous awareness list
4. New entities → auto-insert into knowledge graph
5. Updated entities → refresh position/distance/salience
6. Lost entities → mark `last_seen_at`, fade salience

## Files to Create
- `include/ferrum/npc/npc_sense.h` — sense result types
- `src/aegis/ops/aegis_ops_sense.c` — opcode submit (≤4 non-static functions)
- `src/npc/sense/npc_sense_execute.c` — executor (raycast, overlap, spatial grid)
- `src/npc/sense/npc_sense_auto.c` — per-turn auto update
- `tests/npc/npc_sense_tests.c` — mock physics world, verify result layout

## Files to Modify
- `include/ferrum/aegis/aegis_types.h` — add `AEGIS_OP_SENSE_QUERY`
- `include/ferrum/aegis/aegis_async.h` — add `AEGIS_TASK_SENSE_QUERY = 3`
- `src/aegis/aegis_vm_run.c` — add case to interpreter switch
- `src/aegis/aegis_async_execute.c` — add `execute_sense_query_()` to drain loop

## Acceptance
- [ ] Opcode submits async task to MPSC buffer.
- [ ] Executor performs raycast + overlap against mock physics world.
- [ ] Result buffer contains correct entity_count + salience scores.
- [ ] Auto-sense detects new entity and inserts into knowledge graph.
- [ ] Full sweep returns all entities within 50m.
- [ ] Targeted mode returns data for single entity only.
