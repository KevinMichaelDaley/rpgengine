---
id: rpg-ixuv
status: closed
deps: []
links: []
created: 2026-07-05T22:52:21Z
type: task
priority: 1
assignee: KMD
parent: rpg-02fm
tags: [srd, foundation]
---
# srd-layout-01: srd_sdf_layout.h — types and public API

Write the public header defining srd_sdf_box_t, srd_sdf_layout_t, srd_room_type_t, box flag constants, and all public function signatures. Pure header task — no .c file yet. Must satisfy 2-type rule: srd_room_type_t goes in srd_room_type.h if needed.

## Design

srd_room_type_t: GENERIC=0, BAR, ENTRANCE, PRIVATE, STAIR_UP, STAIR_DOWN, CORRIDOR, DEAD_END, SECRET, BOSS, TREASURE. srd_sdf_box_t: {float cx,cz,hw,hd; srd_room_type_t type; uint32_t flags}. Flags: SRD_BOX_EPSILON (spawned at min size), SRD_BOX_REPAIR_ONLY. SRD_MAX_BOXES=512, SRD_EPSILON=0.01f. Adjacency: flat uint8_t adj[SRD_MAX_BOXES*SRD_MAX_BOXES].

## Acceptance Criteria

Header compiles standalone in C11 with no warnings; Doxygen on every public symbol; 2-type rule satisfied; SRD_EPSILON defined as 0.01f; all array sizes are compile-time constants

