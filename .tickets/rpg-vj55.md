---
id: rpg-vj55
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
# srd-rules-04: srd_rules_feature.c — Rules 31-46 (doors, stairs, special rooms)

Implement Rules 31-46: AddDoor, RemoveDoor, WidenDoor, NarrowDoor, AddStairUp, AddStairDown, RemoveStair, RelocateStair, AddDeadEnd, RemoveDeadEnd, AddSecretRoom, RemoveSecretRoom, AddBossRoom, RemoveBossRoom, AddTreasureRoom, RemoveTreasureRoom. Door width is stored as an extra float in srd_sdf_box_t flags/params area.

## Design

Door rules: add/remove a door flag on a box wall side; door_width stored in a per-box extra float (extend srd_sdf_box_t with door_width[4] for N/S/E/W). AddBossRoom: cond checks no existing BOSS room; fires on any box near a stair. AddTreasureRoom: spawns dead-end (1 neighbour). RelocateStair: moves stair box centre to be adjacent to new host; updates adj.

## Acceptance Criteria

AddDoor then RemoveDoor round-trip; AddBossRoom cond false when boss already exists; AddTreasureRoom creates box with exactly 1 adjacency entry; RelocateStair correctly updates adj entries; all 16 rules appear in applicable list for appropriate layouts

