---
id: rpg-l4hc
status: open
deps: []
links: []
created: 2026-03-15T04:36:02Z
type: task
priority: 2
assignee: KMD
---
# Delta-compressed entity list updates with configurable full snapshot frequency

## Summary

Replace the current full-entity-list-on-every-change sync model with delta-compressed updates. The server should normally send only changed entities (deltas), with infrequent full snapshot-based resyncs.

## Motivation

Currently, every entity-modifying command (spawn, delete, clone, move, rotate, scale) triggers a full `list_entities` round-trip. For large scenes with thousands or millions of entities, this causes:
- Network bandwidth spikes
- CPU time serializing/deserializing the full entity list
- Visible lag/stutter interrupting the user's workflow

## Requirements

### Delta updates
- Server tracks per-entity version/generation counters
- Entity list responses include only entities that changed since the client's last known version
- Client maintains a local version counter and sends it with each request
- Delta format: same entity JSON objects but only including modified entities
- Deletions communicated as tombstone entries (entity ID + deleted flag)

### Full snapshots
- Periodic full snapshots for consistency (corrects any drift from missed deltas)
- Frequency must be **configurable** and should depend on scene size:
  - Small scenes (<1000 entities): more frequent (every few seconds)
  - Large scenes (>100k entities): much less frequent to avoid interrupting workflow
  - User should be able to tune this via config or TUI command
- Full snapshot should never cause visible lag — consider streaming/pagination

### Implementation notes
- The `clone_id` command already returns entity lists inline — delta compression should apply there too
- Consider a sequence number protocol: client sends "I have version N", server sends "here's everything from N+1 to current"
- Tombstone expiry: old tombstones can be pruned after all connected clients have acknowledged past them
- For the UDP game path (replication), this is separate — that already uses quantized snapshots

## Non-goals
- This does NOT change the UDP replication path for in-game networking
- This does NOT add conflict resolution — server remains authoritative

## References
- `src/editor/commands/cmd_list_entities.c` — current full list implementation
- `src/editor/commands/cmd_clone_id.c` — returns entity list inline
- `src/editor/scene/scene_frame.c` — client-side entity list processing (`process_entity_list`, `scene_frame_request_entity_list`)

