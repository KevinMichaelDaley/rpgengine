---
id: rpg-qzl1
status: open
deps: []
links: []
created: 2026-03-14T06:47:58Z
type: feature
priority: 2
assignee: KMD
tags: [editor, viewport, camera]
---
# Per-viewport navigation mode toggle: fly / orbit-selection / orbit-cursor / pan-zoom

Add a per-viewport navigation mode toggle that switches between four camera control styles. Each viewport (see rpg-2uhb for multi-viewport tiling) maintains its own independent nav mode.

## Navigation Modes

1. **Fly (free mouselook + noclip)**: WASD movement with mouselook (hold right-click or toggle). No pivot point — camera moves freely through the scene like a first-person noclip camera. Scroll wheel controls movement speed.

2. **Orbit Selection**: Camera orbits around the centroid of the current selection. This is the current default behavior. Middle-click drag orbits, scroll zooms, shift+middle-click pans.

3. **Orbit Cursor**: Camera orbits around the 3D cursor position instead of the selection centroid. Same orbit/zoom/pan controls as orbit-selection but pivoting on the cursor.

4. **Pan-Zoom Only**: 2D-style navigation — no orbit/rotation. Mouse drag pans, scroll wheel zooms. Useful for top-down or side views where rotation would be disorienting.

## Toggle UX

- A toolbar button or keybinding (e.g. N key or a cycle key) switches between the four modes
- The current nav mode should be displayed in the viewport toolbar (similar to the basis indicator)
- Switching modes should preserve the current camera orientation where possible
- In fly mode, the orbit distance concept doesn't apply — camera position is direct
- Each viewport stores its own nav mode independently

## Implementation Notes

- The editor_camera_t struct needs a nav_mode enum field
- Nav mode is per-viewport — in a multi-viewport layout, one viewport can be in fly mode while another is in orbit-selection
- Fly mode needs velocity-based movement integrated with the frame loop (not just on keypress)
- Orbit-selection reuses existing orbit logic but ensures the focus point tracks selection centroid
- Orbit-cursor reuses existing orbit logic but sets focus to the 3D cursor world position
- Pan-zoom disables orbit callbacks entirely; only pan and zoom (distance) are active
- Camera pan behavior may differ per mode (fly: strafe, orbit: shift focus point, pan-zoom: direct translation)
- Mouse sensitivity and movement speed should be configurable or at least have sensible defaults

