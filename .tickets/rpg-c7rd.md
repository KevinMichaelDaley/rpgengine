---
id: rpg-c7rd
status: open
deps: []
links: []
created: 2026-07-12T22:25:27Z
type: bug
priority: 2
assignee: KMD
---
# Fix non-squared deep sill watertightness (6 holes)


## Notes

**2026-07-12T22:25:47Z**

build_arched_doorway (assets/arch/proc/arch.py) with voussoir_trim=True and a NON-squared deep sill (sill_square=False, sill_extrude>0) leaves 6 boundary-edge holes (non-manifold).

Repro:
  build_arched_doorway(opening_width=1.0, opening_height=1.15, panel_width=2.2,
    panel_height=2.8, wall_thickness=0.4, arch_shape="round", head_rise=0.5,
    sill_height=0.8, voussoir_trim=True, trim_width=0.11, trim_extrude=0.05,
    trim_bevel=0.0, sill_square=False, sill_extrude=0.05)

Edge link-face histogram = {1:6, 2:249}. Holes cluster at the sill front/bottom near
(0,-0.2,0.72), (±0.5,-0.25,0.68), (0,-0.2,0.68), (±0.54,-0.25,0.7).

Only occurs when sill_extrude>0 (the non-squared step-riser path in _front_trim_band's
`if deep:` branch, ~lines 1172-1184). The SQUARED corner path (sill_square=True) is fully
watertight. Pre-existing; independent of the recent squared-sill hand-bevel work (rpg-o01r).
Verify the fix with a bmesh edge link_face Counter ({2:N} only).
