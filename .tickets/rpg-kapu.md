---
id: rpg-kapu
status: closed
deps: []
links: [rpg-iuxn, rpg-62o7]
created: 2026-03-02T00:42:44Z
type: feature
priority: 2
assignee: KMD
tags: [physics, events, aegis, gameplay]
---
# hit! collision event: contact-begin notifications

Add a **hit!** physics event that fires when two entities BEGIN colliding (first tick of contact, not sustained contact).

## Behavior

- **Contact-begin detection:** hit! fires on the FIRST tick that two entities have a collision contact, AND they were NOT colliding on the previous tick. Strictly: colliding THIS tick AND NOT colliding LAST tick.
- **Opt-in per entity:** Only fires if at least one of the two entities has a custom attribute/flag set indicating it should register hit events (e.g. a bitmask flag on the entity). Must be efficient — only iterate flagged entities, not all contacts.
- **Payload:** Both entity IDs, contact normal, contact point, impulse magnitude.
- **Island access:** The physics island that produced the contact should be available/passable to the event handler so scripts can query other bodies in the island.

## Implementation Approach

- In the narrowphase/solver, track a previous-tick contact-pair set using a pre-allocated hash set or bitset (NO per-frame malloc).
- After the solve, diff current contacts vs previous contacts to find new contact-begin pairs.
- For each new pair where either entity has the hit-events flag, publish a hit! event to the aegis event queue.
- The event is subscribable from Aegis scripts via SUBSCRIBE + AWAIT_EVENT.

## NOT in scope

- Sustained contact events or contact-end events
- Trigger volumes / interior overlap tests (that is the overlap! event, separate ticket)

## Acceptance Criteria

- hit! event fires exactly once on contact begin, not on sustained contact
- Does NOT fire if neither entity has hit_events flag set
- Payload contains both entity IDs, contact normal, contact point, impulse
- Island reference is accessible from the event handler
- Pre-allocated tracking structure, no per-frame malloc
- Works across all collider type pairs (sphere, box, capsule, convex, mesh)
- Unit tests for contact-begin detection and event publishing
- Integration test with Aegis script subscribing to hit!

