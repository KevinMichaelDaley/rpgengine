---
id: rpg-c25c
status: open
deps: [rpg-4e2b]
links: [rpg-c6o7]
created: 2026-03-21T07:00:00Z
type: task
priority: 2
assignee: kmd
parent: rpg-c6o7
---
# §5.2.5 Constraint viewport overlays

Visual rendering of constraints, anchors, and their limits in the 3D viewport.

## Overlay Elements

- **Anchor markers** — small diamonds at anchor world positions
  - Grey = unselected, yellow = selected, red = broken/invalid
  - Render at fixed screen-space size (scale with distance)

- **Connection lines** — colored line between the two anchors of each constraint
  - Color-coded by joint type (hinge=blue, ball=green, distance=orange, etc.)
  - Selected constraint line is brighter/thicker

- **Hinge** — arc showing allowed rotation range around hinge axis
  - Filled sector for allowed range, dashed lines for limits

- **Cone-twist** — wireframe cone showing swing limits
  - Twist range as arc around cone axis
  - Solid cone edges for hard limits, dashed for soft

- **Distance** — dashed line at rest length, solid within min/max range
  - Color gradient from green (at rest) to red (at limit)

- **Limit-rotation** — per-axis colored arcs showing allowed rotation ranges
  - Red=X, Green=Y, Blue=Z (matching gizmo convention)

- **Limit-position** — per-axis colored bars showing allowed translation ranges

- **IK chain** — highlighted bone chain from root to effector, pole target line

- **Broken constraints** — red dashed line, X marker at midpoint

## Visibility Toggle

- `V` key cycles collision overlay modes: off → shapes only → shapes + constraints → constraints only
- Inspector checkbox per-constraint to hide individual overlays

## Key files

- src/editor/scene/scene_viewport_collision_overlay.c — extend with constraint rendering
- src/editor/scene/scene_viewport_overlay.c — anchor marker rendering
- src/editor/viewport/viewport_gizmo.h — may need overlay draw helpers
- include/ferrum/editor/scene/scene_viewport_render.h — overlay pass integration
