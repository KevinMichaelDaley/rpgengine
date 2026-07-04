---
id: rpg-9ug6
status: open
deps: [rpg-bux3]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-8sc6
tags: [procgen, critic, hooks, marker]
---
# procgen-5d: Marker proximity hook

## Design

Implement marker proximity detection. Given the critic_hooks_t marker list, check each frame whether the player is within a configurable radius (default 2m) of any marker. On first contact with a marker: fire FR_CRITIC_EVENT_MARKER_HIT with marker name and distance. Track which markers have been hit in the current playthrough to avoid duplicate events. Write RED test with test markers.

## Acceptance Criteria

- Marker proximity detected within configurable radius\n- MARKER_HIT event fires once per unique marker\n- Event includes marker name and distance\n- All markers from layout tracked\n- No duplicate events for same marker\n- Works with markers at any position

