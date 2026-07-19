---
id: rpg-b5r3
status: open
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

