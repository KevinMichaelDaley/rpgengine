---
id: rpg-mnwh
status: closed
deps: [rpg-uuft, rpg-z2y7]
links: []
created: 2026-07-05T22:56:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-ct3l
tags: [srd, discrete, rules]
---
# srd-discrete-03: repair rule application

After all selected rewrites are applied, scan the layout for any condition satisfied by a repair rule and apply each repair rule once per applicable pair/box. Repair rules are applied in a fixed order: ClampToBounds first (establishes boundary), then ResolveOverlap (pairwise), then RepairContained, then AlignWall (optional, only if boxes nearly flush), then EnsureConnected last.

## Design

srd_apply_repairs(layout, rule_table): iterate repair rules in fixed order. For ClampToBounds: apply to every box. For ResolveOverlap: iterate all pairs (i,j) where SDF overlap > 0. For RepairContained: iterate all pairs. For EnsureConnected: BFS to find isolated nodes. Run multiple passes (up to 4) until no repair rules fire.

## Acceptance Criteria

After repairs: no box outside layout bounds; no pair of boxes has positive SDF overlap (within 1e-4); no box is fully contained by another; no isolated box (all have >= 1 adjacency entry); repair application is idempotent (second pass fires no rules)

