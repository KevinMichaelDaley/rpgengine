---
id: rpg-2lyk
status: open
deps: []
links: []
created: 2026-07-21T07:44:10Z
type: epic
priority: 2
assignee: KMD
---
# Dystopian-LA architectural generators as Blender operators (redo-panel UI)


Expand the arch-primitive library (assets/arch/proc/, epic rpg-pm1c) beyond the
Romanesque set into the architectural elements of a DYSTOPIAN DESERT CITY: a
run-down future Los Angeles where mysterious aberrations run the city through
human puppets. Style target: Half-Life 2 architecturally, but grittier -- closer
to the HL2 BETA look -- over real LA typologies (dingbat apartments, mini-malls,
the concrete river channel, freeway underpasses, Broadway-theater decay, deco
towers, power-line alleys, industrial Vernon, googie/dead neon, parking
structures, drought/desertification, bungalow courts).

THE CATCH (workflow requirement, user-stated): every generator must be a Python
bpy.types.Operator the user can pick from a menu, with EVERY parameter editable
in the operator redo panel (F9 / "Adjust Last Operation"). Goal: hybridize the
workflow between generator scripts and direct UI use without tabbing into Python
much. Concretely:
- Operator properties (Float/Int/Bool/Enum/Vector) for every build_* kwarg, so
  the redo panel is the live parameter editor (undo/redo-driven regeneration).
- A menu hierarchy (Add > Dystopian LA > ...) organized by category, "vast menu"
  of selectable tools.
- Operators wrap the existing standalone build_*() functions (keep the
  script/MCP path fully working -- operators are a thin UI layer).
- Keep ferrum_* export tags (ferrum_dynamic, ferrum_lightmap_res, colliders,
  materials) flowing through so generated pieces export via export_scene.py
  unchanged.

DELIVERED (2026-07-21): ref/archgen_dystopian_la.md -- the full tool plan:
operator/redo-panel contract, visual-language summary grounded in the curated
reference set (assetsrc/ref/la/, 12 categories, ~68 usable photos after culling
dead scrapes), 30 tools across 7 families (residential massing, commercial
strips, infrastructure, streetscape, decay/desert passes, aberration overlay,
assembly), per-tool parameter tables, shared-library prerequisites, engine/
export notes, and a 6-phase build order. Phase 0 starts with the shared
operator glue + the Dingbat Apartment generator as pattern-setter.
