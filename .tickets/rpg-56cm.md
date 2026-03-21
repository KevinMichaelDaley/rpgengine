---
id: rpg-56cm
status: closed
deps: []
links: []
created: 2026-03-18T07:04:12Z
type: bug
priority: 2
assignee: KMD
tags: [prefab, gizmo]
---
# Bone translate gizmo axes inverted and ignore transform space

When dragging bone translate gizmos in prefab mode, the movement is inverted (dragging right moves left) and the axes don't respect the selected transform basis (e.g., world space). The gizmo drag delta computation in scene_gizmo_bone_apply.c or the input routing in scene_input.c likely applies the delta in the wrong coordinate space or with inverted sign.

