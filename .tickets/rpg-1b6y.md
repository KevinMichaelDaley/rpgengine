---
id: rpg-1b6y
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
# srd-rules-03: srd_rules_corridor.c — Rules 17-30 (corridors and connections)

Implement Rules 17-30: AddCorridor, RemoveCorridor, WidenCorridor, NarrowCorridor, BendCorridor, StraightenCorridor, SplitCorridor, MergeCorridor, BridgeComponents, AddLoop, RemoveLoop, ShortcutPath, RemoveShortcut, RerouteCorridor. BridgeComponents is the key LOCAL GEOMETRIC CONTROL rule — it fires on any two boxes in disconnected graph components.

## Design

AddCorridor: inserts CORRIDOR box at midpoint between i and j at EPSILON size; sets adj[i][k]=adj[j][k]=true. RemoveCorridor: removes corridor box, clears adj. BridgeComponents: cond uses BFS to verify i and j are in different components. AddLoop cond: verifies i and j are reachable (same component) but not directly adjacent. RemoveLoop/RemoveShortcut cond: verifies removing k leaves layout still connected (BFS without k).

## Acceptance Criteria

AddCorridor then RemoveCorridor round-trip; BridgeComponents cond false when already connected; BridgeComponents cond true when disconnected; RemoveLoop rejected when removal disconnects graph; RerouteCorridor updates both adj and centre position; all 14 rules in applicable list

