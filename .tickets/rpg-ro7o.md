---
id: rpg-ro7o
status: closed
deps: [rpg-ezcn]
links: []
created: 2026-07-22T05:10:54Z
type: feature
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen B1.3: mini-mall stair + walkway railings

The office-strip access stair currently has stringers and treads but no railings, and the balcony deck rail stops at the stair opening with bare ends. Add welded railing meshes: stair flight handrails on both stringers (posts + top rail, mitred at the pitch break), and finished rail terminations at the deck opening. Same quality bar: welded quads, no coincident planes (embed posts 20 mm into stringers/deck), UV'd, vertex-grouped 'loggia'/'steps'. Display variants live for wireframe sign-off.

## Acceptance Criteria

Live Blender review + audit-clean (validate_object ok) across office-strip variants incl. projecting/mixed balconies.


## Closed 2026-07-24
Implemented in commercial.py (_flight_railing, _rail_u + jut wraps): stair flight
handrails on both stringers, deck-rail terminations, railing wraps around
projecting balcony outcroppings, joined at stairs. Reviewed live in the
regenerated la_sprawl scene (4 mini-malls) by KMD during the 07-23 session.
