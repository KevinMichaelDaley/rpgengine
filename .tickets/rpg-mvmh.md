---
id: rpg-mvmh
status: open
deps: [rpg-zket, rpg-w1qe]
links: []
created: 2026-07-13T05:09:19Z
type: epic
priority: 2
assignee: KMD
---
# Tile-based deferred post-pass for small-radius particle lights

A tiled deferred post-process that accumulates many small-radius particle/point lights after the forward+ pass, avoiding per-light forward overdraw for tiny lights. Screen-space tile binning of particle lights + an additive deferred accumulation shading pass blended onto the forward+ result, using a thin G-buffer (depth + normal + albedo/roughness as needed).

## Design

Lives entirely in the core renderer module; NONE embedded in demo_client. Runs after the forward+ pass (depends on that epic). Thin G-buffer captures the minimal attributes the deferred BRDF needs (reconstruct position from depth, sample normal/albedo/roughness/metalness). Particle lights binned into screen tiles; per-tile light lists drive an additive pass. Reuse the PBR BRDF terms from the shader epic where possible. TDD + extreme modularity.

## Acceptance Criteria

Thousands of small-radius particle lights render via the tiled deferred pass at far lower cost than forward, blended correctly onto the forward+ image. Entirely in core renderer; demo_client only submits particle lights. Clean under -Wpedantic.

