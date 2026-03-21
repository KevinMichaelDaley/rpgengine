---
id: rpg-uuze
status: closed
deps: []
links: []
created: 2026-03-18T07:04:06Z
type: bug
priority: 2
assignee: KMD
tags: [prefab, ui]
---
# Prefab outliner scrollbar drag does nothing

The bone outliner in prefab mode shows a scrollbar when bones overflow the panel, but dragging the scrollbar thumb has no effect. Scroll only works via mouse wheel. The scrollbar drag routing in scene_input.c likely doesn't handle the prefab outliner scrollbar (scrollbar_dragging state).

