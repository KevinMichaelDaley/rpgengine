---
id: rpg-kk7n
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
# Aerodynamic/hydraulic actuator driver

Velocity-dependent force model. Aerodynamic: F ∝ v² (drag coefficient, reference area). Hydraulic: force from pressure with flow rate limit (max_velocity under load). Parameters: drag_coeff or pressure, flow_limit, max_force. Useful for flight surfaces, hydraulic pistons, dampers.

