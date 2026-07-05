---
id: rpg-ynd4
status: closed
deps: []
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-8sc6
tags: [procgen, critic, hooks, types]
---
# procgen-5a: Critic event type definitions

## Design

Define critic_event_type_t enum (DEATH, MARKER_HIT, FELL_OOB, TIMEOUT, STUCK) and critic_event_t struct (type, playthrough_id, frame, elapsed_s, player_pos, player_vel, marker_name, distance_to_marker, damage_source, death_pos). Define critic_hook_t struct (fn callback + userdata). Define critic_hook_registry_t for managing registered hooks. Place in include/ferrum/procgen/critic_hooks.h. Write RED test validating struct integrity.

## Acceptance Criteria

- All event types defined in enum\n- critic_event_t has all required fields\n- critic_hook_t supports callback + userdata\n- critic_hook_registry_t defined\n- Headers compile with -Wall -Wextra -Werror

