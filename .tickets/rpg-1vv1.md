---
id: rpg-1vv1
status: open
deps: [rpg-didw]
links: []
created: 2026-07-13T05:24:16Z
type: task
priority: 2
assignee: KMD
parent: rpg-2ejn
---
# PCF cube shadow sampling + bias

PCF-filtered lookup into the point-light shadow cubemap with slope-scaled/normal-offset bias to avoid acne and peter-panning.

## Design

Core renderer. Configurable PCF tap kernel; cube-aware filtering.

