---
id: rpg-ovxp
status: closed
deps: [rpg-xgo2]
links: []
created: 2026-03-11T02:29:22Z
type: epic
priority: 2
assignee: KMD
tags: [physics, biomechanics, drivers]
---
# Biomechanical joint drivers: muscle-driven articulation

Extend the joint driver system to support biologically-inspired joint actuation. Instead of motors and springs, joints are driven by muscle-like actuators that model force-length and force-velocity relationships, tendon routing, antagonist pairing, and activation dynamics.

## Motivation
Ragdoll characters need to transition between animation and physics seamlessly. Biomechanical drivers let animated poses be enforced through physically plausible muscle forces rather than kinematic blending or servo-style PD control. This produces natural-looking responses to impacts, stumbles, and environmental interaction — muscles that strain, fatigue, and have realistic force limits based on joint angle and velocity.

## Architecture
Builds on the joint driver system (rpg-xgo2). A biomechanical driver replaces the simple driver with a muscle model that computes force from:
- Activation level (0-1, neural signal with rise/fall time constants)
- Force-length curve (peak force at optimal length, drops at extremes)
- Force-velocity curve (concentric force drops with speed, eccentric force increases)
- Tendon compliance (series elastic element)
- Moment arm geometry (force × moment arm = torque)

## Key Concepts
- **Muscle unit**: activation + force model + attachment geometry
- **Antagonist pairs**: flexor/extensor muscles on opposite sides of a joint
- **Activation dynamics**: first-order ODE modeling neural delay and calcium dynamics
- **Moment arm**: changes with joint angle (wrapping around bone geometry)
- **Co-contraction**: both antagonists active simultaneously for joint stiffness

## Acceptance Criteria
- [ ] Muscle model with force-length and force-velocity curves
- [ ] Activation dynamics (rise/fall time constants)
- [ ] Tendon model (series elastic element)
- [ ] Moment arm computation from attachment points
- [ ] Antagonist muscle pairing
- [ ] Integration with joint driver system
- [ ] Visual test: character maintaining pose under external load

