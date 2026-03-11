---
id: rpg-tm6g
status: closed
deps: []
links: []
created: 2026-03-11T02:20:04Z
type: task
priority: 1
assignee: KMD
parent: rpg-xgo2
tags: [physics, drivers]
---
# Joint driver architecture and base types

Design and implement the core driver system:
- phys_joint_driver_type_t enum (MOTOR, LINEAR_ACTUATOR, SERVO, SPRING, AERO_HYDRAULIC)
- phys_joint_driver_t struct with union of per-type parameters
- API: phys_joint_set_driver(), phys_joint_clear_driver()
- Integration point in constraint build: driver modifies row bias/target velocity
- Driver state (current position, accumulated error for servo PD) persists across ticks

