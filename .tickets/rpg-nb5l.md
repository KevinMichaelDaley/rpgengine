---
id: rpg-nb5l
status: open
deps: [rpg-l2jd]
links: []
created: 2026-03-02T18:39:23Z
type: task
priority: 2
assignee: KMD
---
# Phase 5b: XPBD bone constraints and IK

Extend the physics XPBD solver to handle bone constraints for post-animation correction. See ref/renderer_spec.md §6.4.

Deliverables:
- include/ferrum/renderer/anim/anim_constraint.h: Bone constraint types — look-at/aim (orient bone toward target), hinge/ball-socket limits (reuse phys_joint_t API), IK chains (FABRIK or CCD IK finalized by XPBD), spring/damper for secondary motion (hair, cloth, tails)
- src/renderer/anim/anim_constraint.c: Constraint solver operating on post-blend-tree bone transforms, iterative XPBD with configurable iteration count
- Integration with existing physics joint system (phys_joint_t) for shared constraint math
- Constraints process after blend tree evaluation, before bone palette upload
- Tests in tests/p004_renderer_anim_constraint_tests.c

Depends on: rpg-l2jd (blend tree provides input transforms), rpg-nnfd (editor physics constraints for shared joint types)

