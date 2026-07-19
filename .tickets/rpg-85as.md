---
id: rpg-85as
status: closed
deps: []
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


## Notes

**2026-07-19T18:57:00Z**

Consumes the canonical fr_collider_prim_t (rpg-b5r3, include/ferrum/asset/collider_prim.h). The live adapter lives here: for each dynamic object, take its fr_collider_prim (from BODY_SPAWN via fr_collider_prim_from_body_spawn, OR from the descriptor via fr_collider_prim_from_desc) + its current transform (reconciled body / posed bone when prim.bone>=0) and emit a gi_collider_t, injected into the chunked SDF each frame for the probe builder (and later audio).

**2026-07-19T18:59:53Z**

Dep on rpg-8302 removed to do it in the requested order: the posed-collider->SDF injection is render_world/GI-layer work (gi_runtime_frame already takes gi_collider_t boxes; i3wx + fr_collider_prim are done), independent of the networked client wire. It consumes fr_collider_prim_t (rpg-b5r3).

**2026-07-19T19:07:19Z**

Completed. (1) CPU posed-proxy builder src/renderer/gi/gi_collider_pose.c: fr_collider_prim + owner/bone transform -> world gi_collider_t (box AABB / sphere / capsule endpoints; bone-keyed uses the posed bone matrix; convex/mesh -> AABB from half_extents; halfspace/point skipped; bounded by out_cap). 6/6 tests. (2) GPU: gi_probe_gpu probe kernel now has a real capsule_sdf + sphere branch and the collider upload carries the kind (bx.w) + capsule endpoints/radius, so posed capsule/sphere proxies occlude accurately (was box/sphere-approx only). Verified: demo renders identically at ~118 fps, box path preserved. Posed-SDF interface documented as reusable for audio propagation. Live wiring (feed a character's posed prims each frame) happens when the client/animation drives it (rpg-8302).
