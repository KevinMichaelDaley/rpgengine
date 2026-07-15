---
id: rpg-8sv9
status: open
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
