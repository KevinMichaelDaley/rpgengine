---
id: rpg-894b
status: open
deps: [rpg-ar4y]
links: [rpg-b51c]
created: 2026-03-21T03:47:02Z
type: task
priority: 3
assignee: kmd
---
# Undo conflict detection for vertex/texel/keyframe operations

Extend undo_conflict_key_t and undo_conflict_check() to support compound conflict keys beyond entity_id. Currently all undo conflict detection uses entity_id only (sufficient for transform/spawn/delete). Per ref/scene_editor_spec.md §3.2, the following operation types need richer keys:

| Operation type | Conflict key |
|---|---|
| Vertex edit | (Mesh ID, vertex index range) |
| Texel paint | (Texture ID, UV region) |
| Keyframe set/delete | (Entity ID, channel, frame) |
| Constraint swap | (Entity ID, bone index, frame) |
| Anim event | (Entity ID, event name, frame) |

## Deliverables

- Extend `undo_conflict_key_t` in `include/ferrum/editor/undo_conflict.h` with fields for mesh_id, vertex range, texture_id, UV region, channel, frame
- Update `undo_conflict_key_extract()` to populate compound keys based on forward_type
- Update `undo_conflict_check()` to compare compound keys (overlapping ranges, etc.)
- Add new `edit_cmd_type_t` values for vertex/texel/keyframe operations
- Tests for range overlap detection, cross-type non-conflict, etc.

## Dependencies

- rpg-ar4y (§3.1 Mesh Mode) — vertex editing must exist before vertex conflict keys are meaningful

## Links

- rpg-b51c — Undo orphan branch recovery (related undo infrastructure)
