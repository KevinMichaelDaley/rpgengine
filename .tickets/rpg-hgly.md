---
id: rpg-hgly
status: closed
deps: []
links: []
created: 2026-03-13T08:02:25Z
type: feature
priority: 2
assignee: KMD
tags: [ui, editor, scene]
---
# Asset Browser Panel: scrollable tree view for spawning entities and assets

Replace the removed Box/Sphere/Capsule spawn buttons in the outliner with a dedicated Asset Browser panel. This panel should be a scrollable frame containing two vertically stacked tree views:

1. Built-in Entities Tree
A collapsible tree section at the top listing built-in entity types that can be spawned directly:
- Primitives: Box, Sphere, Capsule, Halfspace
- Lights: Point light, Spot light, Directional light
- Cameras: Perspective camera, Orthographic camera
- Markers: Waypoint, Spawn point, Trigger volume

Each entry should be clickable to spawn an instance at the default position (or at the camera target). Double-click or drag-to-viewport for placement.

2. Asset Directory Tree
Below the built-in entities, a file-system-backed tree view that mirrors the project assets directory. This tree should:
- Recursively scan the assets directory for meshes (.fvma, .obj), prefabs, textures, and other loadable assets
- Display folders as collapsible nodes with file icons by type
- Allow clicking a mesh/prefab to spawn it as a MESH entity
- Show a tooltip or preview on hover (future enhancement)
- Support search/filter via a text input at the top of the panel

Panel Integration:
- The asset browser should occupy its own panel region in the editor layout (likely replacing or adjacent to the outliner)
- Uses Clay UI layout like existing panels
- Scrollable with the same scrollbar system as the outliner/inspector
- Tree expand/collapse state should persist across frames

Spawn Behavior:
- Spawning from either tree should use the existing TUI command system (e.g. spawn box, spawn mesh path/to/asset.fvma)
- Command echo and pending status should work identically to TUI-typed commands
- Selected entity after spawn should be the newly created entity

