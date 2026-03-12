---
id: rpg-kjmx
status: open
deps: [rpg-5kty]
links: []
created: 2026-03-12T06:48:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-o25i
---
# §6.3 Physics-Coupled Playback

See ref/scene_editor_design.md §6.3. Velocity derivation (v = (pos[n]-pos[n-1])/dt for kinematic bones). Animation damping (multiply by per-joint factor). Physics channel injection (evaluate velocity/force/mass/muscle keyframes, inject into local physics runner). Keyframe inspector (show derived velocity, propagated impulse, damping).

## Acceptance Criteria

Derived velocity shown in inspector. Damping tunes child bone response. Physics channels inject correctly. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

