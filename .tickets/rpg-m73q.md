---
id: rpg-m73q
status: closed
deps: []
links: []
created: 2026-03-11T02:18:20Z
type: feature
priority: 1
assignee: KMD
tags: [physics, joints]
---
# Single-axis twist joint (PHYS_JOINT_TWIST)

Add a new joint type that constrains rotation to a single axis, like a propeller shaft, motor axle, or door hinge with no cone freedom.

## Motivation
The existing cone-twist joint allows rotation within a cone plus twist. Many mechanical systems need pure single-axis rotation with no lateral freedom: wheels, propellers, cranks, hinges. A dedicated twist joint is simpler, cheaper, and more stable than a cone-twist with zero cone angle.

## Design
- Joint type: `PHYS_JOINT_TWIST`
- Constrains 2 angular DOFs (the non-twist axes) to zero
- Leaves 1 angular DOF (twist axis) free
- Constrains 3 linear DOFs (anchor coincidence) like ball-socket
- Total: 5 constraint rows (3 linear + 2 angular)
- Optional twist limits (min/max angle) add 1 row → 6 rows max
- Twist axis defined in local space of body A

## Acceptance Criteria
- [ ] New joint type enum PHYS_JOINT_TWIST
- [ ] Joint descriptor support (axis, optional min/max twist angle)
- [ ] Constraint build: 3 linear + 2 angular lock rows + optional twist limits
- [ ] Works with TGS, CG, and XPBD solvers
- [ ] Unit tests: free twist, locked non-twist axes, twist limits
- [ ] Visual test: spinning propeller or wheel on axle

