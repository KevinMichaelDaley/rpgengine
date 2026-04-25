---
id: scene-004
status: open
deps: [scene-002]
links: [scene-006]
created: 2026-04-02T05:17:00Z
type: task
priority: 2
assignee: kmd
---
# Material Apply Command

TUI command to apply a .fmat material definition to an entity. Sets all material slots from the definition.

## Command Syntax

```
:mat_apply <entity_id> <path>
:mat_apply 42 assets/materials/wood_crate.fmat
```

## Behavior

1. Load .fmat file
2. Resolve entity by ID
3. Set each material slot from definition
4. Version stamp for replication
5. Return success

## Implementation

- `src/editor/commands/cmd_mat_apply.c`
- Uses material_def_t from scene-002
- Integrates with existing material slot system

## Deliverables

- [ ] cmd_mat_apply command handler
- [ ] Undo support (restore previous slots)
- [ ] Error handling for missing files/entities

## Acceptance Criteria

- [ ] `:mat_apply 42 assets/materials/wood.fmat` applies material
- [ ] All slots set from definition
- [ ] Undo restores previous material slots
- [ ] Missing file prints error
- [ ] Invalid entity ID prints error
