---
id: rpg-b5r3
status: closed
deps: [rpg-nbp2]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 3
assignee: KMD
parent: rpg-hjck
tags: [net, physics, client]
---
# Stream physics colliders to the client for prediction

Stream the level + entity physics colliders to the client so client-side prediction can run REAL collision, not integration-only. Today BODY_SPAWN carries only shape_type + half-extents + flags; full collider/level geometry is server-only, so prediction just integrates and relies on reconciliation.

## Design

Extend BODY_SPAWN or add a collider-data schema (mesh/convex/compound colliders via the existing loaders/asset ids); the client builds a local collision world from the streamed level colliders + dynamic body colliders and runs prediction with collision enabled (phys prediction_mode currently disables solve). Reuse the networked-physics prediction/reconcile path already in demo_client.

## Acceptance Criteria

Client receives level + body colliders, builds a local collision world, and predicts with collision (a predicted body rests on the streamed floor instead of drifting); reconciliation still corrects drift.


## Notes

**2026-07-19T18:44:37Z**

SCOPE CORRECTION (user): client physics stays INTEGRATION-ONLY + reconciliation -- do NOT run real collision on the client (collision is server-authoritative). The collider primitives on the client exist ONLY so posed dynamic geometry can be injected into the dynamic SDF for the probe builder / GI rendering (and later audio propagation) -- i.e. they feed rpg-85as, not a client collision solve. prediction_mode stays as-is. Re-scope: the client already HAS the colliders it needs -- level static colliders come from the scene descriptor (rpg-51nf, loaded client-side) and dynamic body colliders come from BODY_SPAWN (shape+extents) / MESH_DATA. So b5r3 is the small 'expose those known colliders as gi_collider_t proxies positioned by the reconciled/interpolated body transforms, ready for SDF injection' piece (+ extend BODY_SPAWN only if a field needed for an accurate proxy is missing, e.g. capsule half-height / local offset). The actual per-frame SDF injection is rpg-85as.

**2026-07-19T18:49:13Z**

REFINED (user): schema completeness spans BOTH channels because level-authored dynamic objects are NOT server-spawned -- their colliders come through the asset pipeline (scene descriptor / streamer), not BODY_SPAWN. Deliverable: (1) a CANONICAL collider-primitive type (fr_collider_prim_t: kind + local offset/rot + box/sphere/capsule/halfspace/mesh params) usable by both the net layer and the asset streamer; (2) extend net_repl_body_spawn to fully carry it (server-spawned dynamics); (3) map the descriptor's scene_desc_collider_t (rpg-51nf, already complete) to the canonical primitive (level-authored dynamics via the streamer). Both channels describe a collider the SAME way so the SDF-injection adapter (rpg-85as) consumes one type regardless of source. The live posing + SDF injection stays in rpg-85as.

**2026-07-19T18:57:00Z**

Completed (schema completeness, both channels). Canonical fr_collider_prim_t (include/ferrum/asset/collider_prim.h) covers the full physics set (box/sphere/capsule/halfspace/mesh/convex/compound/point) + local offset/rotation + streamed geom_asset (mesh/convex/compound) + bone keying (animated hierarchy) + solid. NET channel: net_repl_body_spawn extended with collider offset (payload 33->39), shape codes for convex/compound/point + halfspace normal/distance convention documented, + fr_collider_prim_from_body_spawn. ASSET/DESCRIPTOR channel: scene_desc_collider extended (convex/compound/point kinds, bone, geom_asset, solid) + fr_collider_prim_from_desc -- covers level-authored dynamic objects that are NOT server-spawned. 3/3 tests (wire round-trip incl offset, net->prim per shape, desc->prim incl bone/convex). demo_server.c compiles clean with the wider schema. Live posing + SDF injection is rpg-85as.
