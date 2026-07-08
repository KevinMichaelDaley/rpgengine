---
id: rpg-z2y7
status: closed
deps: [rpg-i1sv]
links: []
created: 2026-07-05T22:54:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-9pjm
tags: [srd, rules]
---
# srd-rules-05: srd_rules_repair.c — Repair Rules 1-5

Implement the 5 repair rules: ResolveOverlap, RepairContained, AlignWall, ClampToBounds, EnsureConnected. These are registered with is_repair=true and inverse_rule_id=-1. They are applied unconditionally at end of each SRD outer iteration and never appear in the K-candidate set.

## Design

ResolveOverlap: compute overlap on axis of minimum penetration; move each box by half the separation. RepairContained: remove j if fully inside i and j has no external adj (adj only to i). AlignWall: snap i's wall to j's wall on nearest axis. ClampToBounds: clamp cx+-hw to [0,layout_w], cz+-hd to [0,layout_h]. EnsureConnected: BFS to find isolated boxes; AddCorridor to nearest neighbour.

## Acceptance Criteria

ResolveOverlap: after apply, sdf_box overlap is zero; RepairContained: removes fully-enclosed single-neighbour box; ClampToBounds: all corners within bounds; EnsureConnected: isolated box gains at least one adjacency entry; is_repair=true so excluded from srd_rule_find_applicable

