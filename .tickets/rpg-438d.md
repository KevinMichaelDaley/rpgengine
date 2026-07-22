---
id: rpg-438d
status: open
deps: [rpg-psto]
links: []
created: 2026-07-22T07:51:15Z
type: feature
priority: 2
assignee: KMD
parent: rpg-2lyk
---
# LA gen D1.1: automatic bridge meshes where the road net spans terrain

The road-network terrain fit BRIDGES abrupt thin canyons: the slope-limited upper envelope holds the deck level across any dip too narrow to descend into (spans where road_z - terrain_z exceeds ~2 m are exactly identifiable from the fit). Add actual bridge structure under those spans: deck side girders, piers/bents at seeded spacing (skipped where the gap is deep), abutment walls at the span ends, guard rails replacing the sidewalk band edge. Welded-quad discipline; ties into ferrum export with colliders.

## Acceptance Criteria

Live Blender review over a canyon-crossing network; audit-clean; spans detected automatically from the profile-vs-terrain gap.

