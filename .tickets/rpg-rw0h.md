---
id: rpg-rw0h
status: open
deps: []
links: []
created: 2026-03-11T02:30:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-ovxp
tags: [physics, biomechanics]
---
# Hill-type muscle force model

Implement the Hill muscle model computing instantaneous force from:
- Active force-length curve: Gaussian-like peak at optimal fiber length
- Passive force-length curve: exponential rise at long lengths (ligament stretch)
- Force-velocity curve: hyperbolic (Hill equation) — force drops with shortening velocity, increases with lengthening
- F_total = activation * f_active(length) * f_velocity(vel) + f_passive(length)
- All curves parameterized per muscle (optimal_length, max_force, max_velocity, pennation_angle)

