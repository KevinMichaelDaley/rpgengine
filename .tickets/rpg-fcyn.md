---
id: rpg-fcyn
status: closed
deps: []
links: []
created: 2026-03-18T07:04:15Z
type: bug
priority: 2
assignee: KMD
tags: [prefab, gizmo]
---
# Multiple bones always show per-object gizmos even when toggled off

When multiple bones are selected in prefab mode, per-object gizmos appear on each bone even when per-object gizmo mode is toggled off (T key). The bone gizmo build/draw code in scene_gizmo_bone.c and scene_viewport_draw.c should check the per_object_gizmo flag on the viewport state and only render individual bone gizmos when it's enabled. When disabled, show a single gizmo at the selection centroid.

