---
id: rpg-9sq6
status: open
deps: []
links: []
created: 2026-07-23T04:25:45Z
type: feature
priority: 2
assignee: KMD
---
# LA gen: driveways, alleys, and lots branching off + merging with the street


Extend the road-network generator (`assets/arch/proc/la/tools/streetscape.py`, the
skinned-mesh road/walk system feeding `LA_RoadNet_*`) with secondary circulation
that BRANCHES off the street centerline and MERGES back into the road surface
watertight (no z-fighting, no gaps at the junction):

- **Driveways**: short ramps from the street/curb across the sidewalk into a lot
  or garage — curb cut + apron + slope to the lot grade. Snap to lot frontages.
- **Alleys**: mid-block service lanes running between/behind buildings, connecting
  two streets; narrower, no sidewalk, own paving material.
- **Lots / parking aprons**: paved parcels (surface lots, gas-station/mini-mall
  aprons) whose edge meets the street via a curb cut, flush with the road surface.

Requirements:
- Junctions merge coplanar with the road ribbon — weld the branch edge into the
  road mesh (shared verts) so there is NO coincident-plane z-fight and NO seam,
  the way `terrain`/`road` clearance is handled elsewhere.
- Curb cuts: locally drop the curb where a driveway/alley/lot meets the street.
- Parameters: branch width, apron depth, ramp slope, paving material per type,
  and placement (per-building driveways, per-block alleys, per-lot aprons).
- Follows the terrain the way the skinned road does; keeps the road network a
  single consistent surface graph.

Parent: the LA road-network / streetscape system (links [[rpg-k4vv]] for the
adjacent geometry-defect cleanup).
