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

