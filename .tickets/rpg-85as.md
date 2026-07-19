---
id: rpg-85as
status: open
deps: [rpg-8302]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 3
assignee: KMD
parent: rpg-hjck
tags: [gi, physics, animation]
---
# Inject posed/dynamic geo into the chunked SDF for the probe builder

Inject posed/animated assets (skeletal meshes posed by physics/animation, moving props) into the chunked SDF of the dynamic geo each frame, so the probe builder gathers correct dynamic occlusion/bounce -- generalizing today's analytic gi_collider_t boxes to real posed geometry. Foundation for later dynamic audio propagation over the same SDF.

## Design

Extend the gi_collider path (gi_runtime_frame boxes) to accept posed capsule/convex/mesh proxies derived from the animation/physics pose (bone AABBs/capsules are cheap first step). Fold them into the combined SDF min() the probe trace already does. Keep the per-frame cost bounded (proxy count cap, GI_VIS_MAX_BOXES=64). Design so the same posed-SDF is reusable by a future audio-propagation query.

## Acceptance Criteria

A posed character occludes/bounces dynamic GI (moving shadow + color bleed) via injected proxies, not just static boxes; per-frame cost stays within budget; the posed-SDF interface is documented as reusable for audio.

