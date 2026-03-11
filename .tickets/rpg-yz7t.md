---
id: rpg-yz7t
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
# Fixed-speed motor driver

Constant target velocity on a rotational or linear DOF. Parameters: target_velocity, max_force/max_torque. The solver row bias is set to target_velocity; lambda bounds are clamped to ±max_torque. Works on twist, hinge, and cone-twist joints.

