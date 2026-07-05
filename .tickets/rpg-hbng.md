---
id: rpg-hbng
status: closed
deps: [rpg-8sc6, rpg-wksd]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-oxnh
tags: [procgen, critic, runtime, events]
---
# procgen-7b: Event collector during playthrough

## Design

Implement critic_event_collector that aggregates all critic_event_t from hooks during a single playthrough. Track: death_positions[] (with times and causes), marker_hits[] (with names and times), stuck_events[], fall_events[]. Compute per-playthrough metrics: total distance traveled (sum of position deltas), survival time, markers_reached_count, markers_missed list. Write RED test.

## Acceptance Criteria

- All death events collected with complete data\n- All marker hits collected\n- Distance traveled computed correctly\n- Survival time computed\n- Markers reached vs missed tracked\n- Per-playthrough stats struct populated\n- Memory bounded (max events per type)

