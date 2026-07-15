---
id: rpg-fg05
status: open
deps: []
links: [rpg-1gj9]
created: 2026-07-13T06:24:48Z
type: feature
priority: 2
assignee: KMD
---
# Multi-bounce GI in the lightmap baker (colour bleed onto distant surfaces)

The Cornell box back wall should pick up a yellow-ish tint (red bounce from the left wall + green bounce from the right wall), but currently reads neutral grey -- the second-bounce surface->surface colour transport is not reaching distant receivers. Extend/verify the baker so multi-bounce indirect (light -> wall -> back wall -> ...) is correctly accumulated, producing the expected colour bleed across the whole scene, not just on surfaces adjacent to a coloured wall.

## Design

The progressive form-factor radiosity solver (lm_solve, Southwell shooting) is multi-bounce in principle: each shot deposits incident radiance into receiver SH AND adds reflected (albedo*received) to the receiver residual, which is shot again on later iterations. Investigate why distant colour bleed (Cornell back wall) is missing: (a) convergence/max_shots cutting higher bounces; (b) near_radius / near-field-only shooting losing or mis-weighting long-range transport (energy not conserved to far receivers); (c) form-factor magnitude at range; (d) the direct white term dominating and washing the tint (may be correct/physical). Add a diagnostic (e.g. per-surface incident-radiance chromaticity) and a regression check (back-wall SH should carry red+green). Keep TDD + modularity. Relates to the SH-lightmap shader term (rpg-0i1w) that visualises it.

## Acceptance Criteria

A baked Cornell box shows the expected multi-bounce colour bleed: the back wall carries a measurable red+green (yellow-ish) tint from the side walls, and colour bleed appears on distant surfaces, not only adjacent ones. A regression test asserts the back-wall luxel SH has non-trivial red and green irradiance. Verified visually through the PBR SH-lightmap render.

