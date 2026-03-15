---
id: rpg-pymj
status: open
deps: [rpg-nnfd, rpg-1sjm]
links: []
created: 2026-03-15T07:05:55Z
type: task
priority: 2
assignee: KMD
parent: rpg-rkj9
tags: [editor, snap, physics]
---
# Constraint-aware snap modes (snap-to-constraint, constrained movement)

Move snap-to-constraint and constrained-movement modes out of §1.4 Object Mode into their own ticket, since they depend on the editor constraint/joint system (rpg-nnfd) being in place first.

## Snap Modes

- **Snap to constraint**: edit-time rigid body constraints (positional anchors, distance limits) enforced locally using solver subset; derives absolute position/orientation sent to server
- **Constrained movement**: move objects while respecting active snap constraints (may use local solver to derive final position/orientation update sent to server)

## Requirements

- Constraint store must exist (rpg-nnfd)
- Local solver subset that evaluates constraints without full physics step
- Absolute position/orientation derived and sent to server
- Works with existing gizmo drag workflow

