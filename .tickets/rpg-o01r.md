---
id: rpg-o01r
status: open
deps: []
links: []
created: 2026-07-11T19:44:13Z
type: task
priority: 2
assignee: KMD
---
# Arch reveal/soffit trim so masonry reads as joined, not UV-clipped

The tiling masonry material (`build_masonry_material`) + course-continuous reveal
UVs (`arch._masonry_course_uvs`, ticket rpg-ilmc/lb1q) course the wall through the
window reveal, but at the arch soffit crown and at the reveal↔face corners the
brick pattern is cut by the UV chart boundary — it reads as bricks *clipped by a
UV seam* rather than stones *joined at a real interface* (e.g. an arris where the
front-face course meets the reveal return, or voussoirs meeting at the crown).

Add a **trim** at these interfaces so the join reads as intentional masonry:
- an arris / quirk / small chamfer or a thin trim course along the reveal↔front
  corner and around the arch, OR
- a proper voussoir treatment at the soffit crown (the crown currently
  foreshortens because the arch uses a single Y–Z planar chart).

This is a geometry + UV/material trim pass on `build_arched_doorway`; the
underlying coursing is correct, this is the finishing interface.

