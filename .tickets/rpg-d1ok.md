---
id: rpg-d1ok
status: closed
deps: []
links: []
created: 2026-07-16T06:47:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-fo9r
---
# Combined dynamic SDF: baked field min-combined with analytic collider SDFs

Sample/stream the baked scene SDF (per-chunk 128^3 .sdf, rpg-iudw) and MIN-COMBINE it at query time with cheap ANALYTIC signed-distance functions of the nearest/largest dynamic colliders (sphere/box/capsule from physics), so moving bodies occlude the dynamic-light GI. min(d_static, d_dyn...) = union of solids. Provide a sampler usable by the probe cone-trace (CPU + GPU). Bound the folded-in collider count.


## Notes

**2026-07-16T06:56:20Z**

CPU sampler done (TDD, gi_sdf_tests pass): gi_collider_distance (sphere/box/capsule analytic SDFs), gi_sdf_baked_sample (trilinear over a raw baked field, large-positive outside the grid), gi_sdf_combined = min(baked, colliders...). Decoupled from the lightmap format (takes raw dist/dims/origin/voxel). The probe kernel (rpg-p3w3) mirrors this min-combine in its trace (CPU-job or GLSL) and does the per-frame nearest/largest collider selection (the 'bound the folded-in count').
