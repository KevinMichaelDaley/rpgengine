---
id: rpg-njnm
status: closed
deps: []
links: []
created: 2026-03-18T07:04:09Z
type: bug
priority: 2
assignee: KMD
tags: [prefab, camera]
---
# F key frames origin instead of selection in prefab mode

Pressing F to frame the selection in prefab mode frames the world origin (0,0,0) instead of the selected bone(s) or entity. The frame-selection logic in scene_input.c needs to compute the centroid of selected bones (using rest_world positions transformed by the entity model matrix) when in prefab mode with bone selection active.

