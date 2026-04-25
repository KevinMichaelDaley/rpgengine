---
id: scene-003
status: open
deps: [scene-001]
links: [scene-005]
created: 2026-04-02T05:17:00Z
type: task
priority: 2
assignee: kmd
---
# Entity Definition Spawn Command

TUI command to spawn an entity from a .fentity definition file. Creates an entity with all properties from the definition applied atomically.

## Command Syntax

```
:spawn_def <path>
:spawn_def assets/entities/crate_wood.fentity
```

## Behavior

1. Load .fentity file (via cache)
2. Create entity with specified type
3. Set mesh path (SCRIPT_KEY_MESH_PATH)
4. Set material path (SCRIPT_KEY_MATERIAL)
5. Apply physics properties (static, mass, friction, restitution)
6. Apply custom attrs
7. Queue script paths for later binding (if script system exists)
8. Return entity ID

## Implementation

- `src/editor/commands/cmd_spawn_def.c`
- Uses entity_def_t from scene-001
- Integrates with existing cmd_entity_def pattern

## Deliverables

- [ ] cmd_spawn_def command handler
- [ ] Undo support (delete on undo)
- [ ] Error handling for missing files

## Acceptance Criteria

- [ ] `:spawn_def assets/entities/crate.fentity` spawns entity
- [ ] Entity has mesh, material, attrs from definition
- [ ] Undo removes the spawned entity
- [ ] Missing file prints error, doesn't crash
