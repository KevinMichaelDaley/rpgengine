---
id: scene-007
status: open
deps: [scene-001]
links: []
created: 2026-04-02T05:17:00Z
type: task
priority: 3
assignee: kmd
---
# Inspector: Entity Type Field

Add an "Entity Type" dropdown to the inspector panel that shows available entity definitions (.fentity files). Selecting a type applies the definition to the selected entity.

## Features

- Dropdown populated from entity definition cache
- Shows definition name (e.g., "Crate (Wood)", "Barrel", "Prop_Table")
- Selecting a type:
  - Sets mesh path
  - Sets material path
  - Applies attrs
  - Updates physics properties
- "None" option clears definition-specific fields

## Implementation

- Extend `src/editor/panels/inspector_widgets.c`
- Add entity_type_dropdown widget
- On change, apply definition to entity

## Deliverables

- [ ] Entity Type dropdown in inspector
- [ ] Dropdown shows all .fentity definitions
- [ ] Selecting type applies definition
- [ ] "None" option removes definition linkage

## Acceptance Criteria

- [ ] Dropdown appears in inspector for selected entity
- [ ] Selecting a type updates entity mesh/material/attrs
- [ ] Changes sync to server
