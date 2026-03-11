---
id: rpg-p2oy
status: closed
deps: [rpg-tm6g]
links: []
created: 2026-03-11T02:20:04Z
type: task
priority: 2
assignee: KMD
parent: rpg-xgo2
tags: [physics, drivers]
---
# Physical spring driver

Hooke's law restoring force: F = -k*x - c*v. Parameters: stiffness (k), damping (c), rest_length/rest_angle. Maps directly to constraint compliance (α = 1/k) and damping terms. Works on any joint type with a positional DOF.

