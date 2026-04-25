---
id: scene-008
status: open
deps: [scene-002]
links: []
created: 2026-04-02T05:17:00Z
type: task
priority: 3
assignee: kmd
---
# Inspector: Material Slots

Add material slot fields to the inspector panel with file picker for .fmat files. Shows current material assignments and allows picking new materials.

## Features

- Section header: "Material"
- 5 slot rows: Albedo, Normal, Roughness, Metallic, Emissive
- Each row shows:
  - Current texture path (or "None")
  - File picker button (opens to .fmat files)
- "Apply Material" button to select .fmat file
- Quick: click slot → file dialog → select .fmat → apply

## Implementation

- Extend `src/editor/panels/inspector_widgets.c`
- Add material_slot_widget
- File picker uses existing asset browser or native dialog

## Deliverables

- [ ] Material section in inspector
- [ ] Each slot shows current texture
- [ ] File picker for .fmat files
- [ ] Apply updates entity materials

## Acceptance Criteria

- [ ] Material slots visible in inspector
- [ ] Can select .fmat file for each slot
- [ ] Changes reflect in viewport
