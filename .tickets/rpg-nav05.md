---
id: rpg-nav05
status: open
deps: [rpg-nav04]
links: [rpg-nav01, rpg-llm0d-nav]
created: 2026-04-27T00:25:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, integration, aegis, async, goto]
---
# Navigation Integration: Wire Pathfinding into Async Executor + GOTO Tool

Connect the hierarchical A* system to the existing AEGIS async infrastructure and the GOTO tool.

## Requirements

### 1. Async Task Format (AEGIS_TASK_NAV_QUERY)

Extend the existing `AEGIS_TASK_NAV_QUERY` stub to carry pathfinding parameters:

```c
/* Param layout (64 bytes) */
/* bytes  0..11: start position (float[3]) */
/* bytes 12..23: goal position  (float[3]) */
/* bytes 24..27: path strategy (uint32_t) */
/* bytes 28..31: agent radius (float) */
/* bytes 32..35: agent height (float) */
/* bytes 36..39: max waypoints (uint32_t) */
/* bytes 40..43: start section ID (uint32_t, 0xFFFFFFFF = auto) */
/* bytes 44..47: goal section ID (uint32_t, 0xFFFFFFFF = auto) */
```

Result layout (variable size, arena-allocated):
```c
typedef struct npc_nav_result {
    int32_t  status;        /* 0 = success, -1 = unreachable, -2 = partial */
    uint32_t waypoint_count;
    /* Followed by waypoint_count × vec3_t */
} npc_nav_result_t;
```

### 2. Async Executor Integration

In `aegis_async_execute.c`, replace the NAV_QUERY ERROR stub:

```c
static void execute_nav_query_(const aegis_async_task_t *task,
                               const npc_nav_world_t *nav_world) {
    /* Extract params from task->params. */
    /* Call npc_hpath_query(nav_world, &request, &result). */
    /* Pack result into task->result_ptr (up to result_cap). */
    /* If result overflows result_cap, return PARTIAL with truncated waypoints. */
}
```

- The executor receives a `npc_nav_world_t *` (or NULL until navigation is initialized).
- If nav_world is NULL, return ERROR (preserves current stub behavior during rollout).

### 3. GOTO Tool Integration

In `aegis_ops_tool.c`, when `tool_id == AEGIS_TOOL_GOTO`:

1. Parse `target` string from JSON args.
2. Resolve target to a world position:
   - Named entity: lookup by entity name attribute.
   - Landmark: lookup in static location table.
3. Validate mobility (not rooted/stunned).
4. Build `npc_hpath_request_t` with strategy = `NPC_PATH_HIERARCHICAL`.
5. Submit `AEGIS_TASK_NAV_QUERY` via the async buffer.
6. Store async handle in result register.
7. Script polls/waits for completion, then reads waypoint path.

### 4. NPC Navigation World Lifecycle

```c
typedef struct npc_nav_world {
    npc_svo_grid_t      svo;
    npc_nav_hgraph_t    hgraph;
    npc_nav_blocker_t  *dynamic_blockers;
    uint32_t            blocker_count;
    uint32_t            blocker_cap;
} npc_nav_world_t;
```

- One `npc_nav_world_t` per server world instance.
- Initialized on level load: build SVO from static geometry → extract chunk graph → hierarchical reduction.
- Updated each tick: refresh dynamic blocker list from physics/ECS.

## Files to Modify

- `src/aegis/aegis_async_execute.c` — implement `execute_nav_query_`
- `src/aegis/ops/aegis_ops_tool.c` — wire GOTO to submit nav query
- `src/aegis/ops/aegis_ops_nav.c` — GOTO tool handler (uses json_parse)
- `src/npc/nav/npc_nav_world.c` — world init/update/destroy

## Files to Create

- `include/ferrum/npc/npc_nav_world.h` — npc_nav_world_t
- `src/npc/nav/npc_nav_world.c` — lifecycle management
- `tests/npc/npc_nav_integration_tests.c` — end-to-end: GOTO → async → path result

## Acceptance

- [ ] NAV_QUERY async task submits successfully from VM.
- [ ] Executor drains nav query and returns waypoint path.
- [ ] GOTO tool resolves entity target, submits nav query, and returns handle.
- [ ] Script can poll nav query status and read waypoints.
- [ ] Nav world builds from static geometry on level load.
- [ ] Dynamic blocker added via physics event updates path result.
