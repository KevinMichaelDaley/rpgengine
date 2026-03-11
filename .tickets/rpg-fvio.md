---
id: rpg-fvio
status: closed
deps: []
links: []
created: 2026-03-11T02:30:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-ovxp
tags: [physics, biomechanics]
---
# Moment arm and attachment geometry

Compute the moment arm of a muscle about a joint from attachment points:
- Origin and insertion points defined in bone-local coordinates
- Moment arm = perpendicular distance from joint axis to muscle line of action
- Changes with joint angle (wrapping around bone convex hull)
- Simple wrapping model: cylinder or sphere wrap for muscles that cross bony prominences
- Torque = muscle_force * moment_arm

