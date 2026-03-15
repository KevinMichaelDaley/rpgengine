---
id: rpg-w0iv
status: closed
deps: []
links: []
created: 2026-03-14T11:06:48Z
type: feature
priority: 2
assignee: KMD
tags: [ui, editor, viewport]
---
# Translate/scale gizmo visual + hit region improvements

The rotate gizmo got visual and hit-testing improvements (thicker lines, active axis highlight, screen-space hit regions). The translate and scale gizmos need the same treatment:

## Larger hit radius when zoomed in

The translate/scale gizmo axes use 3D ray-segment distance for hit testing with a fixed world-space threshold. At close zoom this makes them hard to click. Apply the same screen-space-aware approach used for rotation rings so the clickable region scales naturally with zoom.

## Thicker rendered lines

The translate/scale axis lines are currently 1px thin. They should be rendered thicker (matching the rotation ring thickness) so they are clearly visible at all zoom levels.

## Active axis highlight

When an axis is hovered/active, it should render brighter and thicker (same two-pass approach as rotation rings: inactive axes at base thickness + dimmer color, active axis at larger thickness + brighter color).

