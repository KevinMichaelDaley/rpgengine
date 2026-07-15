---
id: rpg-2ejn
status: in_progress
deps: [rpg-w1qe, rpg-zket, rpg-fsvq]
links: []
created: 2026-07-13T05:23:16Z
type: epic
priority: 2
assignee: KMD
---
# Dynamic PCF shadow maps per dynamic light type (point-light cubemaps first)

Standard PCF shadow maps for fully-dynamic (movable) lights, one path per light type. First target: shadow-casting movable POINT lights via single- or double-cubemap depth maps with PCF filtering; then movable spot/directional 2D PCF maps. These dynamic shadows combine with the stationary-light static+dynamic shadows and the lightmap ambient in the material shader.

## Design

Core renderer module only; nothing in demo_client. Point light: cubemap (or dual-paraboloid) depth render + PCF cube lookup with bias. Spot/dir: 2D depth + PCF. Configurable PCF kernel + slope-scaled bias. Reuse the material shader's shadow-term integration from the stationary-shadow epic. TDD + extreme modularity.

## Acceptance Criteria

Movable point lights cast PCF-filtered cubemap shadows; movable spot/dir lights cast 2D PCF shadows. Dynamic shadow term combines with static shadows and lightmap ambient in the material shader. Entirely in core renderer. Clean under -Wpedantic.

