---
id: rpg-22h2
status: open
deps: []
links: []
created: 2026-07-22T20:41:10Z
type: feature
priority: 2
assignee: KMD
---
# GI: per-region probe unfreeze around GI_DYNAMIC objects

Builds on the baked/frozen probe field (offline .probesh bake + freeze). Most
probes are frozen static GI; SOME regions need live GI. Add a GI_DYNAMIC object
tag and unfreeze probes near those objects' AABBs.

EXPORTER: tag objects GI_DYNAMIC via a custom property (or name/category
heuristic). Target: building INTERIORS (LA_*_Interior, and building bodies with
interior mode) and HEAVILY STREETLIT road segments (roads under sign/streetlight
clusters). Emit per-object "gi_dynamic": true + optionally a radius in the scene
descriptor. Depends on the object-category tagging groundwork (interior/road/
terrain) discussed for probe density.

DESCRIPTOR: parse gi_dynamic (+ radius) per object.

RUNTIME: the freeze is currently GLOBAL (gi->frozen). Make it PER-PROBE: a
per-probe frozen mask. Probes whose position is within radius of any GI_DYNAMIC
object's world AABB stay UNFROZEN (keep re-tracing each tick); the rest freeze
after convergence / load from .probesh. This gives:
  - dynamic irradiance in those regions (moving sun/lights, doors opening), and
  - irradiance FROM dynamic objects (NPCs/wildlife/vehicles) that wander into
    the region, bounced onto nearby surfaces.
The staggered trace already supports a subset; extend the active/group gating
so only unfrozen probes dispatch (bounded per-frame cost = size of the dynamic
regions, not the whole level).

Acceptance: a level with a GI_DYNAMIC interior shows GI updating there (e.g. a
moving light / dynamic object bleeds colour) while the surrounding frozen static
GI + framerate are unaffected.

