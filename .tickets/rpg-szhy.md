---
id: rpg-szhy
status: closed
deps: []
links: []
created: 2026-07-04T23:00:39Z
type: bug
priority: 1
assignee: KMD
tags: [procgen, serializer, bug]
---
# procgen-fix: ramps and openings not serialized to JSON

## Design

procgen_serialize.c writes rooms, corridors, markers, and spawn but silently omits ramp and opening entities from JSON output. The data is in fr_dungeon_layout_t but never reaches the output file.

## Acceptance Criteria

- Ramps appear in JSON output as type "ramp"\n- Openings appear as type "door" or "window"\n- Entity IDs are sequential across all types\n- Existing tests pass

