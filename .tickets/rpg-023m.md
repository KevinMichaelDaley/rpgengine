---
id: rpg-023m
status: open
deps: [rpg-bdrv]
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-6p4q
tags: [procgen, grammar, serializer]
---
# procgen-2b: Layout → engine entity creation

## Design

Implement procgen_spawn_into_world() that directly creates ECS entities from fr_dungeon_layout_t. For each room/corridor/ramp: create entity, attach transform component, attach mesh component (box/convex hull), attach physics body + collider. For markers: create invisible info_target. For spawn: create info_player_start. Write RED test verifying entity counts and types.

## Acceptance Criteria

- Correct number of entities created\n- Transform component positions match layout geometry\n- Physics colliders match room/corridor bounds\n- Marker entities are tagged correctly\n- Spawn entity has correct type

