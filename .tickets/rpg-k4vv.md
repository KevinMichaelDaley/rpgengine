---
id: rpg-k4vv
status: open
deps: []
links: []
created: 2026-07-23T03:34:47Z
type: bug
priority: 2
assignee: KMD
---
# Dingbat/MiniMall geometry defects: z-fighting + interior inconsistencies


The procedural LA buildings (`assets/arch/proc/la/tools/residential.py` — dingbats,
`.../commercial.py` — minimalls) have geometry defects visible in-scene:

- **Z-fighting**: coincident/near-coplanar faces flickering (overlapping slabs,
  double walls, decals or trim sharing a plane with the surface behind them).
- **Door sizes**: inconsistent / wrong-scaled door openings vs frames (openings not
  matching the door mesh, or heights varying between instances).
- **Internal plate glass**: interior glass (`la_glass`) panes misplaced, wrong-sized,
  or z-fighting with mullions/frames; some interior glazing missing or doubled.
- **Wall gaps**: seams/holes where wall segments meet (partitions not meeting the
  shell, corner gaps, floor/wall junctions leaving slivers) — these also leak light
  and break the SDF interior enclosure.

Fix in the generators so the exported meshes are watertight and coplanar faces are
offset/merged. Watertight interiors matter for the baked GI + the new SDF
transmission (light through windows) landing correctly.

Scope: dingbat + minimall generators. Audit each defect class, add regression
coverage where practical (e.g. no-coincident-face / enclosure checks on the
generated mesh).
