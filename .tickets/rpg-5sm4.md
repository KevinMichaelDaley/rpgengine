---
id: rpg-5sm4
status: closed
deps: []
links: []
created: 2026-02-15T08:44:28Z
type: task
priority: 2
assignee: KMD
---
# Swept-volume CCD for fast primitives vs static mesh

## Problem
Capsules orbiting at high speed (e.g. 90 m/s at t=5s) tunnel through the armadillo mesh because they traverse their own diameter in less than one physics tick. The solid mesh backface fix cannot help if the primitive never overlaps triangle AABBs at the discrete sample instant.

## Approach: SDF-based swept-volume CCD
Instead of substepping the entire physics simulation, compute CCD only for fast-moving dynamic bodies against static geometry:

1. **Per-substep swept volume**: For each dynamic body moving faster than a velocity threshold, compute a swept AABB (or swept capsule/sphere) covering its motion from t to t+dt.
2. **Static mesh SDF query**: Query the static mesh BVH with the swept volume to find candidate triangles. For each candidate, compute the signed distance from the swept path to the triangle.
3. **Time-of-impact (TOI)**: Binary search or analytical solve for the earliest time the primitive surface touches a triangle surface along its swept path.
4. **Contact generation**: At the TOI, generate a contact with correct normal and penetration, and clamp the body's position/velocity to the impact point.

## Implementation details
- Add a `bool ccd_enabled` flag to `phys_body_t` or as a world-level option for static-dynamic pairs.
- Velocity threshold: only trigger CCD when `|v| * dt > radius` (body moves more than its own radius per tick).
- The CCD pass runs after integration but before the regular narrowphase, injecting synthetic contacts.
- Should work with the existing `solid` flag on mesh shapes.
- Implement in the physics module (not demo-specific) so any game can opt in.

## Scope
- Static-dynamic CCD only (not dynamic-dynamic).
- Sphere and capsule swept volumes (box can come later).
- Uses existing mesh BVH infrastructure.

