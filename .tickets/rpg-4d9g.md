---
id: rpg-4d9g
status: closed
deps: [rpg-43h9]
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-1iyt
---
# §0.1.1 Clay UI Backend

See ref/scene_editor_design.md §0.1.1. OpenGL renderer for Clay render commands (clay_backend.c), font loading and glyph atlas (clay_fonts.c), theme constants (clay_theme.c). Handles RECTANGLE, TEXT, IMAGE, BORDER, SCISSOR, CUSTOM command types. Pre-allocated arena from engine allocator.

## Acceptance Criteria

Clay UI renders panel chrome, toolbar, outliner placeholder, inspector placeholder. Font atlas loads. Text is measurable and renderable via Clay backend. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

