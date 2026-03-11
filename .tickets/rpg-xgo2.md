---
id: rpg-xgo2
status: closed
deps: [rpg-m73q]
links: []
created: 2026-03-11T02:18:52Z
type: epic
priority: 1
assignee: KMD
tags: [physics, joints, drivers]
---
# Joint drivers: motors, actuators, springs

Add a joint driver system that applies forces/torques to joints to produce controlled motion. Drivers attach to existing joints and inject energy in a physically consistent way through the constraint solver.

## Motivation
Ragdolls and mechanical systems need powered joints — wheels that spin, arms that reach, springs that bounce. Currently joints only passively constrain; they cannot actively drive motion. A driver system enables vehicles, robots, animated characters with physical response, and any mechanism with powered joints.

## Architecture
- `phys_joint_driver_t` struct attached to a joint, specifying driver type and parameters
- Drivers integrate into the constraint solver by modifying the target velocity or position of constraint rows
- Each driver type maps to a bias/target computation; the solver enforces it
- Drivers can be added/removed at runtime
- Multiple drivers on one joint are NOT supported (one driver per joint)

## Driver Types
1. **Fixed-speed motor** — constant target angular/linear velocity, with max torque/force limit
2. **Linear actuator** — drives a prismatic/distance joint to a target position at controlled speed
3. **Servo** — PD controller driving to a target angle/position with configurable stiffness and damping
4. **Physical spring** — Hooke's law restoring force with configurable stiffness (k) and damping (c)
5. **Aerodynamic/hydraulic actuator** — force proportional to velocity² (drag) or pressure-driven with flow limits

## Acceptance Criteria
- [ ] Driver system architecture and public API
- [ ] All 5 driver types implemented and tested
- [ ] Drivers work with twist, cone-twist, ball-socket, hinge, prismatic, and distance joints
- [ ] Energy-consistent: drivers inject energy only through solver bias, no position teleporting
- [ ] Runtime add/remove drivers
- [ ] Visual test: motorized wheel, spring-loaded door, servo arm

