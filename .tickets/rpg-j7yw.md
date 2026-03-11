---
id: rpg-j7yw
status: open
deps: [rpg-rw0h]
links: []
created: 2026-03-11T02:30:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-ovxp
tags: [physics, biomechanics]
---
# Tendon and series elastic element

Model the tendon as a series elastic element between muscle fiber and bone:
- Tendon force-length curve: stiff nonlinear spring (slack below rest length, stiff above)
- Muscle-tendon equilibrium: F_muscle = F_tendon at each timestep
- Tendon length = total musculotendon length - fiber length * cos(pennation)
- Enables energy storage (e.g., Achilles tendon in running)

