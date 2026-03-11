---
id: rpg-w9im
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
# Linear actuator driver

Drives a prismatic or distance joint toward a target separation at a controlled speed. Parameters: target_position, max_speed, max_force. Computes velocity bias from position error clamped by max_speed; force limited by lambda bounds.

