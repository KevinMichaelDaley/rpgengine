---
id: rpg-6p4q
status: closed
deps: [rpg-bdrv]
links: []
created: 2026-07-04T20:39:25Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, grammar, serializer, tdd]
---
# procgen: Phase 2 - Rasterizer + Serializer

## Design

Convert the intermediate fr_dungeon_layout_t into two output targets: (1) JSON level format compatible with the existing edit_level_deserialize system, and (2) engine entities via direct ECS creation. Rooms become func_geo entities with convex hull colliders. Corridors become func_geo entities with extruded colliders. Markers become info_target entities. Spawn becomes an info_player_start entity. Also generates critic hook data (fr_critic_hooks_t) for the playtester subsystem.

## Acceptance Criteria

- fr_dungeon_layout_t → valid JSON level file\n- JSON parses correctly by existing edit_level_deserialize\n- Rooms produce func_geo entities with correct colliders\n- Corridors produce func_geo entities with extruded colliders\n- Markers produce info_target entities at correct positions\n- Spawn produces info_player_start at correct position\n- Critic hook data structure filled correctly\n- Roundtrip: layout → JSON → load → verify entities match

