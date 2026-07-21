---
id: rpg-ncpj
status: open
deps: []
links: []
created: 2026-07-21T01:38:36Z
type: bug
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, bug]
---
# Bug: punctual lights silently dropped past hardcoded 64

Section 8 #3. The punctual light store is hardcoded to 64 (client_scene_load.c:261,353 -- calloc(64,...)) while rc.max_lights defaults to 512; lights past 64 are silently dropped in add_descriptor_lights (:226). Size the store to max_lights (or warn + clamp visibly).

## Acceptance Criteria

The light store is sized to max_lights; levels with >64 lights are not silently truncated.

