---
id: rpg-g08l
status: closed
deps: [rpg-o8pq]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-8hc1
tags: [aegis, vm, entity]
---
# Aegis entity query instructions

Implement entity query bytecode instructions per ref/aegis_bytecode_spec.md §3.3.

These instructions read from the script_entity_view_t snapshot (provided by the engine via edit_script_env.h). All reads go through entity_attrs_get() for dynamic attributes, or read fixed fields (pos, rot, scale) for well-known keys.

Instructions:
- query_entity r_dst, r_entity_id: find entity in snapshot by ID; r_dst = snapshot index (handle), or -1 if not found
- get_attr r_dst, r_handle, key: read attribute from snapshot entity; key is a SCRIPT_KEY_* constant (immediate); copies value into register (vec3 → register.vec3, f32 → register.f32, etc.)
- entity_count r_dst: number of active entities in snapshot
- entity_at r_dst, r_index: entity handle at snapshot index (for iteration, 0..entity_count-1)

get_attr behavior for well-known keys:
- KEY_POS (0): reads snapshot.pos[3] as vec3
- KEY_ROT (1): reads snapshot.rot[3] as vec3
- KEY_SCALE (2): reads snapshot.scale[3] as vec3
- KEY_TYPE (4): reads snapshot.type as u32
- KEY_BODY_IDX (5): reads snapshot.body_index as u32
- All other keys: delegates to entity_attrs_get(&snapshot.attrs, key, ...)

All query_entity and get_attr calls are logged to the execution trace (read set tracking).

Files:
- src/aegis/ops/aegis_ops_entity.c (query_entity, get_attr)
- src/aegis/ops/aegis_ops_entity_iter.c (entity_count, entity_at)
- tests/aegis/aegis_ops_entity_tests.c

Acceptance criteria:
- [ ] query_entity finds entity by ID in snapshot, returns valid handle
- [ ] query_entity returns -1 for missing entity
- [ ] get_attr reads well-known keys (pos, rot, scale) directly from snapshot fields
- [ ] get_attr reads dynamic keys via entity_attrs_get
- [ ] entity_count returns correct count; entity_at returns valid handles
- [ ] entity_at with out-of-range index returns error
- [ ] Tests: query existing entity, query missing, read each well-known key, read user key, iterate all entities

## Acceptance Criteria

Entity queries read from snapshot correctly, well-known keys optimized, bounds-checked

