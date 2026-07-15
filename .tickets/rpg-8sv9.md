---
id: rpg-8sv9
status: closed
deps: []
links: []
created: 2026-07-15T08:30:01Z
type: task
priority: 1
assignee: KMD
parent: rpg-tjtp
---
# Compute-shader GI gather kernel (DDA traversal + near path-trace + far cone/mip + sky)


## Notes

**2026-07-15T08:52:48Z**

SPHERE-TRACE STRATEGY (from design review):
- Build the JFA SDF at a COARSE resolution: 128^3, from a LOWER SVO depth (not the finest leaf level) => 7 JFA passes (log2 128). Cheap to build, small to store.
- The gather sphere-traces on this coarse 128^3 SDF for the big empty-space jumps.
- Once a ray gets closer than sqrt(2) coarse voxels to a surface, DROP to exact traversal (DDA) down to the SVO's MAX depth for the precise hit + material read.
- Consequence: the SVO can now carry a really HIGH max-depth AROUND SURFACES (fine geometric/material detail near surfaces) while the coarse SDF handles empty-space skipping -- two-level: coarse SDF accel + high-res SVO accuracy. This is what makes the trace massively faster without losing near-field precision.

**2026-07-15T09:15:07Z**

DONE (kernel). GPU gather compute kernel feature-complete + analytic-validated on Iris Xe: uniform-hemisphere sampling + SH9 (CPU-convention exact) -> open sky=pi; multi-bounce geometric series (3.14/4.62/5.71); all light types -- directional/point/spot direct (2.000/0.120/0.120) + emissive voxels; sphere-traced occlusion (enclosed black=0); SDF-trace-then-refine-into-SVO octree (octant descent correct for all 8; gather via single-leaf SVO == dense grid). Remaining for the full baker (-> rpg-k4lk): run on the real hall SVO + coarse SDF, the far-field cone/mip (may be subsumed by SDF+SVO), and CPU parity + speedup measurement.
