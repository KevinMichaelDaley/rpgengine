---
id: rpg-zl24
status: open
deps: [rpg-8sc6, rpg-hbng]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-oxnh
tags: [procgen, critic, runtime, stats]
---
# procgen-7d: Summary statistics

## Design

Implement critic_compute_summary() in critic/critic_summary.c. From playthrough results: compute success_rate (% of runs where all markers reached), avg_survival_time, median_survival_time, most_common_death_zone (nav node with most deaths within its boundary), death_heatmap_centroid (average death position), marker_reach_rates per marker, avg_distance_traveled. Output as text report + structured data.

## Acceptance Criteria

- Success rate computed correctly\n- Avg and median survival times\n- Death heatmap: centroid and per-zone counts\n- Per-marker reach rates\n- Report in human-readable text format\n- Summary struct populated for programmatic use\n- Handles edge case: zero deaths (all successes)

