---
id: rpg-2uhb
status: open
deps: []
links: []
created: 2026-03-14T06:44:36Z
type: feature
priority: 2
assignee: KMD
tags: [editor, viewport, ui]
---
# BSP-based multi-viewport tiling with split/drag

Add support for multiple independent tiled viewports in a single editor window using a BSP (binary space partition) tree.

## Splitting Controls

- **Alt+Left**: Split vertically, current viewport goes left, new viewport on right
- **Alt+Right**: Split vertically, current viewport goes right, new viewport on left
- **Alt+Up**: Split horizontally, current viewport goes top, new viewport on bottom
- **Alt+Down**: Split horizontally, current viewport goes bottom, new viewport on top

## Draggable Boundaries

Split boundaries (dividers) between viewports must be draggable to resize the panes interactively. The dividers should have a small grab zone (e.g. 4-6px) and show a resize cursor on hover.

## BSP Architecture

Use a binary space partition tree to manage the viewport layout:

- Each leaf node is a viewport with its own independent camera, gizmo state, and FBO
- Each internal node is a split (horizontal or vertical) with a configurable ratio (0.0-1.0)
- Splitting a viewport replaces its leaf with an internal node containing two new leaves
- Closing a viewport collapses the internal node, promoting the sibling leaf
- The BSP tree is walked to compute panel_rect_t for each viewport during layout
- Each viewport gets its own FBO sized to its panel rect, re-created on resize

## Key Design Considerations

- The focused viewport receives input; other viewports still render
- Each viewport maintains independent orbit camera state (yaw, pitch, distance, focus)
- Gizmo interaction only occurs in the focused viewport
- Grid, entity rendering, and overlays render independently per viewport
- The existing single-viewport code in scene_viewport_render should generalize to N viewports
- Consider a max viewport count (e.g. 8) to bound resource usage
- Panel layout system needs to support dynamic viewport rects from the BSP tree

