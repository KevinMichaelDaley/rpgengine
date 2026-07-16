---
id: rpg-q82b
status: open
deps: [rpg-qthg]
links: []
created: 2026-07-16T06:47:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-fo9r
---
# Forward+ probe sampler: nearest-probe distance-weighted SH indirect term

In the pbr/forward+ material shader, gather the NEAREST probes to the fragment via the accel grid (fragment cell + neighbours), blend their SH9 by distance weight (inverse-distance / k-nearest), evaluate against the surface normal, and add as a DYNAMIC indirect term on top of the baked static lightmap + direct lights. Handle no-probe-nearby gracefully (fall back to 0 dynamic indirect).


## Notes

**2026-07-16T07:02:41Z**

CPU reference done (TDD, gi_probe_sample_tests pass): gi_probe_sample gathers nearby probes via the accel grid, inverse-distance-weights their SH9 (per RGB block), cosine-reconstructs irradiance for the normal, clamps negative ring. Returns false + zero when no probe is near. Tests: nearest-weighting (bright vs dark probe), no-probe-nearby. REMAINING (integration, with rpg-cyx1): GLSL port in the forward+/pbr shader (gather via uploaded probe + accel-grid TBOs/SSBOs, add as dynamic indirect term on top of the baked lightmap) + GL upload of the probe buffers.
