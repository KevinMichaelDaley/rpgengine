---
id: rpg-wjcg
status: open
deps: []
links: []
created: 2026-03-12T06:48:54Z
type: task
priority: 2
assignee: KMD
parent: rpg-1n66
---
# §8.1 Terrain System

See ref/scene_editor_design.md §8.1. Page grid (256x256 vertex pages), page streaming (load/unload by camera distance), terrain tools (raise/lower/smooth/flatten/stamp/erode), erosion (hydraulic and thermal), splatmap via texture layer stack (dynamic layer count), terrain rendering (per-page mesh, LOD with geomorphing), brush engine integration, outliner terrain pages, hide/swap-to-disk per-page.

## Acceptance Criteria

Terrain sculpting works with all tools. Erosion simulation. Material layers painted. Pages stream. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

