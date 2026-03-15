---
id: rpg-1htd
status: closed
deps: []
links: []
created: 2026-03-14T11:03:10Z
type: feature
priority: 2
assignee: KMD
tags: [ui, editor, viewport]
---
# Viewport visual polish: shading modes, gizmo scaling, selection outline

Bundle of viewport visual improvements for the scene editor.

## Shading mode cycle (/ key)

Add a viewport shading mode that cycles through display modes when pressing "/":

1. **Shaded** (default) — half-lambert matcap with higher ambient than current
2. **Unlit textured** — shows the current material channel (albedo) with no lighting
3. **Matcap** — pure matcap shading
4. **Wireframe** — wireframe overlay on solid or wireframe-only
5. **Texel density** — all textured objects rendered with a checkerboard texture to visualize UV density
6. Additional modes to be added later (normal map view, AO, etc.)

The current default matcap needs to be changed to half-lambert with higher ambient so objects are more visible and less dark on the unlit side.

## Gizmo minimum thickness

When zoomed in close, the gizmo rings and axis lines should have a larger minimum pixel thickness so they remain easy to see and click. Currently they can become too thin at close zoom levels.

## Selection outline always unlit

The selection outline (highlight around selected objects) is currently affected by scene lighting/shading. It should always be rendered as a flat unlit color so it remains consistently visible regardless of lighting conditions.

