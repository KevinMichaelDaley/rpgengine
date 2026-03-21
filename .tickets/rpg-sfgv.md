---
id: rpg-sfgv
status: closed
deps: []
links: []
created: 2026-03-18T07:04:22Z
type: bug
priority: 2
assignee: KMD
tags: [prefab, renderer]
---
# Shaded/matcap toggle has no visual effect on skinned meshes in prefab mode

Switching between shaded and matcap rendering modes (via the viewport mode toggle) has no visible effect on skinned meshes in prefab mode — they always appear shaded. The skinned mesh draw path in scene_viewport_draw.c may not be reading the viewport's shade_mode or may always use the Blinn-Phong shader regardless of the current mode setting.

