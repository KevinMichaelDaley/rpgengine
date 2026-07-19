---
id: rpg-q1cp
status: open
deps: [rpg-51nf]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [server, asset]
---
# Server level-load path (descriptor -> physics world + headless GI + stream priorities)

Replace the empty-world/editor-only startup with a real level loader: parse the scene descriptor (T1), populate the physics world with the level collision geo, init the headless GI/visibility model (T10), and drive the per-client asset-stream priorities the client uses.

## Design

Insertion point: demo_server main() around the --scene edit_level_load hook and bridge_on_spawn_ -> cmd_channel PHYS_CMD_SPAWN_BODY. Load colliders straight to phys bodies (static, inv_mass=0) instead of via the editor round-trip. Server holds the authoritative chunk/priority map and assigns per-client streaming priority based on player position/interest (extends the existing tier/priority-body-sender concept).

## Acceptance Criteria

Server boots from a scene descriptor into a populated physics world + initialized headless GI; a joining client receives the level identity + initial stream priorities; no editor interaction required to load a level.


## Notes

**2026-07-19T18:27:14Z**

Dep on rpg-k4jk removed: k4jk (headless stealth/visibility GI) is deferred to the end of the pipeline, so q1cp loads collision geo + physics world + per-client stream priorities WITHOUT it; the headless GI init is wired in when k4jk lands.
