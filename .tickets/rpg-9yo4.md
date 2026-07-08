---
id: rpg-9yo4
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
# srd-rules-02: srd_rules_room.c — Rules 1-16 (room topology)

Implement Rules 1-16: SplitRoomH, SplitRoomV, MergeRooms, AddRoomN, AddRoomS, AddRoomE, AddRoomW, RemoveRoom, TrimRoom, ExtendRoom, ScaleRoom, AddAlcove, RemoveAlcove, AddAntechamber, RemoveAntechamber, ConvertType. Each rule has cond and apply functions. All Add* rules spawn at hw=hd=SRD_EPSILON.

## Design

SplitRoomH: cond checks hw>2*EPSILON; apply removes box i, inserts two children whose extents partition i exactly. MergeRooms: cond checks adj[i][j] and that their union is a valid AABB (non-shared axes match within EPSILON). AddRoomN/S/E/W: spawns at EPSILON adjacent to anchor. RemoveRoom: no cond check (always valid). TrimRoom/ExtendRoom: cond checks remaining extent > EPSILON. 4-function rule: split into srd_rules_room_split.c, srd_rules_room_add.c, srd_rules_room_modify.c if needed.

## Acceptance Criteria

SplitH then MergeRooms round-trip restores original box to within EPSILON; AddRoomN spawn is at EPSILON size; RemoveRoom removes adjacency entries; TrimRoom beyond EPSILON is rejected by cond; each rule registers its inverse_rule_id correctly; 16 rules appear in applicable list for appropriate layouts

