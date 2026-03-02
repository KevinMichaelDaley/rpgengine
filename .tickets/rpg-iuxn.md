---
id: rpg-iuxn
status: closed
deps: []
links: [rpg-kapu, rpg-62o7]
created: 2026-03-02T00:43:12Z
type: feature
priority: 2
assignee: KMD
tags: [physics, events, aegis, gameplay]
---
# overlap! intersection event: interior overlap detection

Add an **overlap!** physics event — similar to hit! but uses full interior intersection tests instead of collision contact detection.

## Behavior

- **Interior overlap test:** overlap! fires when two entities' shapes fully intersect (interior overlap), using `phys_test_overlap()` from `phys_overlap.h` which does boolean overlap tests for shape pairs.
- **Contact-begin semantics:** Like hit!, overlap! only fires on the FIRST tick of overlap (overlapping THIS tick AND NOT overlapping LAST tick). Does NOT fire on sustained overlap.
- **Opt-in per entity:** Only fires if at least one entity has an overlap_events flag set. Efficient — only test flagged entity pairs.
- **Mesh collider restriction:** Does NOT support overlaps between two mesh colliders (mesh-vs-mesh interior test is undefined/impractical). Mesh-vs-primitive (sphere, box, capsule, convex) IS supported.
- **Payload:** Both entity IDs, overlap center estimate (midpoint of shape centers or AABB intersection center).

## Difference from hit!

| | hit! | overlap! |
|---|---|---|
| **Detection** | Collision contact (narrowphase) | Interior intersection (phys_test_overlap) |
| **Trigger** | Surfaces touching | Volumes interpenetrating |
| **Mesh-vs-mesh** | Supported | NOT supported |
| **Use case** | Projectile impacts, physical collisions | Trigger volumes, area-of-effect, proximity zones |

## Implementation Approach

- Run overlap tests in a separate pass after broadphase, only for entity pairs where at least one has the overlap flag.
- Use bounding-sphere pre-filter (already in phys_test_overlap) to cull pairs cheaply.
- Track previous-tick overlap pairs in a pre-allocated hash set (same pattern as hit!).
- Diff current vs previous to find overlap-begin events.
- Publish overlap! events to the aegis event queue, subscribable via SUBSCRIBE + AWAIT_EVENT.

## Acceptance Criteria

- overlap! fires on first tick of interior overlap, not sustained
- Does NOT fire if neither entity has overlap_events flag
- Does NOT attempt mesh-vs-mesh overlap (returns false or skips)
- Mesh-vs-primitive overlaps work correctly
- Pre-allocated tracking, no per-frame malloc
- Unit tests for overlap detection logic
- Integration test with Aegis script subscribing to overlap!

