# Dystopian-LA Architectural Generators — Tool Plan (rpg-2lyk)

Design document for expanding the arch-primitive library (`assets/arch/proc/`,
epic `rpg-pm1c`) from the Romanesque set into the architecture of a **run-down
future Los Angeles**: a desert city governed by mysterious aberrations through
human puppets. Style target: **Half-Life 2 — but grittier, closer to the HL2
beta** — grafted onto real LA typologies.

Reference photo set: `assetsrc/ref/la/` (12 categories, gathered + curated
2026-07-21). Cited below as `[NN_category]`.

---

## 0. MODELING QUALITY BAR (non-negotiable)

None of this is to be done with simplified blockouts or bare primitives — no
corner-cutting anywhere. Every generator emits **production topology from the
first commit**:

- **All-quad meshes.** No n-gons; triangles only where genuinely unavoidable.
  No mesh errors: non-manifold edges, zero-area faces, doubled verts — none.
- **No T-junctions.** Adjoining parts share vertices or keep clean separate
  shells; a face edge never lands mid-edge of a neighbour.
- **Good edge flow.** Loops follow the form: openings (windows/doors/carport
  voids) get complete surrounding face loops; bevels get holding edges; poles
  (3- and 5-valence verts) are steered away from visible flats via standard
  reroutes (diamonds/spirals), never dumped as triangles.
- **When in doubt, draw the topology FIRST**: an ASCII diagram of the edge
  loops around each opening/corner/transition, referencing common 3D-modeling
  practice, goes into the module docstring (and the ticket) before any bmesh
  code is written.
- **Programmatic validation** in every generator's smoke check: quad
  percentage, manifoldness, no doubles, junction audit.
- **Interactive wireframe sign-off**: every tool ticket's acceptance includes
  displaying the result to the user and getting approval of the wireframes in
  a live session before close.

## 1. The workflow contract (the catch)

Every generator ships as a **`bpy.types.Operator`** the user picks from a menu,
with **every parameter editable in the operator redo panel** (F9 / "Adjust Last
Operation"). The goal is a hybrid workflow: generator scripts and direct UI use
without tabbing into Python.

Hard requirements, applying to every tool below:

1. **Operator properties for every `build_*()` kwarg.** `FloatProperty` /
   `IntProperty` / `BoolProperty` / `EnumProperty` / `FloatVectorProperty`,
   with `min`/`max`/`soft_min`/`soft_max` and units (`unit='LENGTH'`) so the
   redo panel sliders are usable without typing. Regeneration happens through
   the normal undo/redo cycle — `execute()` must be **pure**: delete nothing it
   didn't create this invocation, build into a fresh object (or objects) at the
   3D cursor, and be fast enough (< ~0.5 s) that redo-panel scrubbing feels
   live. Heavier tools get a `preview: BoolProperty` that builds a decimated
   version while scrubbing.
2. **Thin wrappers.** Operators wrap standalone `build_*(params) -> objects`
   functions in the same module — the script/MCP path stays fully usable, and
   the operator layer stays testable-by-eye only. One module per tool family,
   one operator per generator.
3. **Menu taxonomy.** One registration module (`la/menu.py`) builds
   `Add > Dystopian LA > <Family> > <Tool>` from a declarative table; each
   family below is a submenu. Search (F3) finds every tool by its label.
4. **Engine tags flow through.** Generators set `ferrum_*` custom props exactly
   as `great_hall.py` does: `ferrum_dynamic`, `ferrum_lightmap_res`, collider
   markers, material assignments from `material_nodes.py`-style node builders —
   so `scripts/export_scene.py` exports the result unchanged.
5. **Seeded determinism.** Every stochastic tool takes `seed: IntProperty`; the
   same seed + params = the same mesh (no `random` without the seed). The redo
   panel then makes "reroll" a one-click act.
6. **Naming**: `bl_idname = "ferrum.la_<tool>"`, objects named
   `LA_<Tool>_<seed>`; everything a tool builds lands in a
   `LA_<Tool>` collection so a whole invocation can be grabbed or deleted as
   one unit.

---

## 2. Visual language (grounded in the reference set)

What the photos actually say, translated into generator obligations:

- **The stucco box is the atom** `[01_dingbats]`. Two stories, flat or
  barely-pitched roof, cantilevered over a black **tuck-under carport void** —
  the single most LA silhouette there is. Fantastic-facade appliqué (starbursts,
  script address numerals, mansard eyebrows) on an otherwise dumb box. Windows
  are sliders with clip-on awnings and window AC units.
- **Signage is architecture** `[02_strip_malls, 09_googie_neon]`. Mini-malls
  read as: flat canopy + continuous sign band of mismatched tenant panels +
  one tall **pole sign** at the curb. Googie survivors: upswept parabolas and
  starburst pylons, half the neon tubes dead.
- **Infrastructure is megaform** `[03_la_river_channel,
  04_freeway_underpasses_interchanges, 10_parking_structures]`. Trapezoidal
  concrete river channel with graffiti at reach height and a trickle down the
  low-flow notch; ranks of freeway pillars and stacked deck edges; parking
  structures as striped concrete shelves with sagging cable rails.
- **The wire canopy** `[07_alleys_power_lines]`. Alleys and side streets sit
  under a sagging mesh of power/phone lines, wood poles, transformers, and
  service drops to every parcel. This is cheap geometry with enormous
  silhouette value — a first-class generator, not clutter.
- **Ornament over decay** `[05_broadway_theater_district, 06_art_deco_towers]`.
  Glazed terracotta, deco piers and chevrons, dead blade signs and empty
  marquee letter boards — with discount storefronts and roll-up shutters
  grafted into the ground floor. The contrast IS the look.
- **Horizontal courts** `[12_bungalow_courts_spanish_revival]`. Mirrored rows
  of tiny gabled/Spanish units around a central walk — the residential texture
  between the boxes.
- **Desert pressure** `[11_drought_desert_encroachment]`. Dead lawns, dust
  drifts against curbs, bleached paint, sand occupying the gutters. The desert
  is a *pass* applied to everything, not a place.
- **The aberration layer** (HL2-beta grit, no direct reference photos —
  deliberately alien against the above): monolithic matte grafts that ignore
  the host building's logic, surveillance stalks, checkpoint retrofits,
  puppet-administration banners (the one dynamic-cloth element we already
  have), and cabling that is *too* clean next to the human wire canopy.

Common material palette implied: cracked/patched **stucco** (3 colorways +
bleach gradient), **board-formed + smooth concrete** (graffiti band variant),
**glazed terracotta/tile**, corrugated + ribbed **metal**, **asphalt/gravel
roof**, dead-**neon**/enamel sign faces, chain-link, plywood.

---

## 3. Tool catalog

Grouped by menu family. Every param listed is a redo-panel property; `seed`
and a `Materials` enum (auto / explicit palette entry) are implied on all.

### Family A — Residential massing

**A1. Dingbat Apartment** — `ferrum.la_dingbat`
The flagship. `[01_dingbats]`
- `width: 14 m (8–22)`, `depth: 9 m (6–14)`, `floors: 2 (1–3)`
- `carport_bays: 4 (0–6)`, `carport_side: enum(front/back/none)`
- `facade_style: enum(plain/starburst/mansard/tiki/script)` — appliqué set
- `window_cols: 5 (2–8)`, `awnings: bool`, `ac_units: 0.4 (0–1 density)`
- `stair_side: enum(left/right/rear)` — external switchback stair + railing
- `address_text: string` — script numerals plate (curve-to-mesh)
- `balcony_rail: enum(none/iron/breezeblock)`
- Outputs: body, carport posts, stair, appliqué; colliders: body box +
  carport posts; `ferrum_lightmap_res` scaled by footprint.

**A2. Bungalow Court** — `ferrum.la_bungalow_court`  `[12_bungalow_courts…]`
- `units_per_side: 3 (1–6)`, `unit_w/d/h`, `court_width: 5 m (3–9)`
- `style: enum(craftsman/spanish)` — gable+brackets vs. parapet+arch
- `roof_pitch`, `porch: bool`, `path_width`, `gate_pylons: bool` (stone piers)
- `condemned: 0.2 (0–1)` — fraction of units boarded (ties into pass E2)

**A3. Courtyard Block / Deco Walk-up** — `ferrum.la_walkup`
3–5 story infill: flat roof, parapet, fire escape, ground-floor retail option.
- `floors: 4 (3–6)`, `bays: 5`, `retail_ground: bool`, `fire_escape: bool`
- `parapet_style: enum(plain/deco_step/cornice)`, `light_well: bool`

### Family B — Commercial strips

**B1. Mini-Mall** — `ferrum.la_minimall`  `[02_strip_malls]`
- `tenants: 5 (2–9)`, `depth: 12 m`, `canopy_depth: 2.5 m`
- `sign_band: bool` + per-tenant randomized panel sizes/colors (seeded)
- `parking_rows: 1 (0–2)` — striped lot slab + wheel stops
- `pole_sign: bool`, `pole_height: 9 m (5–15)`, `pole_panels: 4 (1–8)`
- `corner_lot: bool` — L-plan variant
- `shutters: 0.3 (0–1)` — fraction of roll-up doors down (decay tie-in)

**B2. Googie Pylon / Roadside Sign** — `ferrum.la_googie_sign`  `[09_googie…]`
- `style: enum(starburst/parabola/blade/bowling_pin)`, `height`, `text: string`
- `neon_dead: 0.6 (0–1)` — fraction of tube segments dark (emissive split:
  live tubes get `ferrum` emissive material, dead get glass-grey)
- `cabinet_lit: bool`, `rust_streaks: bool`

**B3. Broadway Theater Front** — `ferrum.la_theater_front`  `[05_broadway…]`
A FACADE generator (depth ~3 m) to graft onto walk-up/deco masses.
- `width`, `floors: 4`, `ornament: enum(terracotta/churrigueresque/deco)`
- `marquee: enum(none/flat/vee)`, `marquee_text: string`, `blade_sign: bool`
- `ground_floor: enum(theater/discount_store/boarded)` — the grafted-retail look
- `letter_board_fill: 0.3` — fraction of marquee letters present (seeded gibberish)

**B4. Deco Tower** — `ferrum.la_deco_tower`  `[06_art_deco_towers]`
- `floors: 12 (6–16)`, `setbacks: 2 (0–3)`, `pier_rhythm: 3 m`
- `crown: enum(step/ziggurat/sign)`, `crown_text: string`
- `tile: enum(turquoise/cream/gold_trim)`, `spandrel_ornament: bool`

### Family C — Infrastructure

**C1. River Channel Section** — `ferrum.la_river_channel`  `[03_la_river…]`
- `top_width: 80 m (20–140)`, `depth: 8 m`, `bank_angle: 35°`, `length: 60 m`
- `low_flow_notch: bool` + `water: enum(dry/trickle/flow)`
- `graffiti_band: bool` (material zone at reach height), `service_road: bool`
- `outfalls: 2 (0–4)` — storm-drain mouths in the banks (gameplay doors!)
- Sections chain end-to-end (snap verts on the open ends, like `brick_wall`).

**C2. Freeway Segment / Underpass** — `ferrum.la_freeway`  `[04_freeway…]`
- `deck_width: 20 m`, `deck_count: 1 (1–3, stacked)`, `height: 7 m/deck`
- `pillar_style: enum(round/rect/flared)`, `span: 18 m`, `curve_radius: 0 (straight)`
- `soffit_detail: bool` (girder ribs), `fence_topper: bool`, `shoulder_debris: bool`
- Underpass = 1 segment used as a street ceiling; on/off-ramp variant via
  `ramp: enum(none/on/off)` sweep.

**C3. Parking Structure** — `ferrum.la_parking`  `[10_parking_structures]`
- `floors: 4 (2–8)`, `bay_w: 16 m`, `bays: 3`, `ramp: enum(internal/external_helix)`
- `edge: enum(open_cable/precast_panel/mesh)`, `stripe_paint: bool`
- `stair_tower: bool`, `rooftop_sign: bool`

**C4. Power-Line Run** — `ferrum.la_powerlines`  `[07_alleys_power_lines]`
The wire canopy. Operates along a picked curve OR a straight run param.
- `length: 60 m`, `pole_spacing: 25 m (15–40)`, `pole_style: enum(wood/steel_h)`
- `crossarms: 2 (1–3)`, `wires_per_arm: 3`, `sag: 0.6 m (0.1–1.5)`
  (catenary curves, curve-bevel meshes, `ferrum_lightmap_res=0`)
- `transformers: 0.4` (density on poles), `service_drops: 0.5` — drops toward
  parcel side, `street_lamps: 0.3` — cobra-heads (light entities optional)
- `dead_sag: 0.1` — fraction of snapped/dangling lines (dystopia dial)

**C5. Storm Drain / Culvert Kit** — `ferrum.la_storm_drain`
Round + box culverts, headwalls, trash racks, grates. The stealth-route kit.
- `profile: enum(round/box)`, `diameter`, `length`, `grate: bool`, `silt: 0–1`

### Family D — Streetscape & lots

**D1. Street Section** — `ferrum.la_street`
Curb-to-curb kit the lots plug into: roadway, curbs, sidewalk slabs (heaved),
`lane_count`, `median: enum(none/turn/palms)`, `crosswalk/stop_bar paint`,
`asphalt_patches: 0–1`, `sinkhole: bool`.

**D2. Lot Furniture Scatter** — `ferrum.la_lot_clutter`
Seeded scatter over a picked face/lot: dumpsters, shopping carts, pallets,
tires, mattresses, chain-link segments, jersey barriers, sandbag stacks.
- `density`, `kit: multi-enum`, `keep_clearance: 1.2 m` (navmesh-friendly)

**D3. Palm / Dead-Tree Row** — `ferrum.la_palms`
- `count`, `spacing`, `height: 8–20 m`, `alive: 0.3 (0–1)` — dead = bare
  trunk + skirt stub `[11_drought…]`; cheap card fronds, `ferrum_dynamic=0`.

**D4. Fence & Barrier Run** — `ferrum.la_fence`
`style: enum(chain_link/razor_top/plywood_hoarding/wrought_iron)`, `height`,
`lean: 0–1`, `gap: bool` (the hole the player uses), posts colliders only.

### Family E — Decay & desert passes (selection operators)

These act on the **selected objects** (any generator output, or imported
meshes), not standalone builds — same redo-panel contract.

**E1. Weathering Pass** — `ferrum.la_weather`
Material-level: assigns the weathered variant of each palette material with
per-object params baked to vertex color masks:
- `bleach: 0–1` (south-face sun gradient), `grime_streaks: 0–1` (under sills,
  scuppers), `stucco_crack: 0–1` (decal geometry at openings' corners),
  `patch_paint: 0–1` (mismatched rectangles — graffiti-buff ghosts)

**E2. Board-Up / Abandonment Pass** — `ferrum.la_board_up`
Detects window/door openings (by material slot or face tag from Family A/B
generators): `boarded: 0–1`, `style: enum(plywood/corrugated/cinderblock)`,
`posted_notices: bool` (wheatpaste quads).

**E3. Desertification Pass** — `ferrum.la_desert`
- `sand_drift: 0–1` — drift meshes against the windward side of selected
  objects' bases (needs a `wind_dir` global), `dust_tint: 0–1` material shift,
  `dead_lawn: bool` for lot planes, `tumbleweed_count` (dynamic props).

**E4. Graffiti / Poster Pass** — `ferrum.la_graffiti`
Decal quads on reach-height zones of large flat faces: `density`, `kind:
multi-enum(tags/throwies/posters/buffed)`, `resistance_stencils: bool`
(anti-aberration marks — worldbuilding channel).

### Family F — Aberration overlay (the regime)

Deliberately breaks the human grammar above. Matte, seamless, oversized.

**F1. Monolith Graft** — `ferrum.la_ab_graft`
A smooth prismatic volume INTERSECTING a host building (boolean optional —
prefer interpenetration, it reads more alien): `size`, `cant_angle: 0–15°`,
`facet_count`, `seam_glow: 0–1` (thin emissive channels), `material:
enum(gunmetal/bone/void_black)`.

**F2. Surveillance Stalk** — `ferrum.la_ab_stalk`
Roof/pole-mounted sensor: `height`, `head: enum(tri_eye/ring/dish)`,
`cable_drops: n` (too-clean cables contrasting C4's sag), `sweep_light: bool`
(spot light entity, `ferrum` realtime tags).

**F3. Checkpoint Retrofit** — `ferrum.la_ab_checkpoint`
Street-blocking kit: gate frame sized from a picked street section, scanner
pylons, queue rails, `barrier: enum(shutter/field_frame)`, puppet-guard booth
(human-made, shabby — the contrast again), `banner_pair: bool` — mounts the
existing dynamic cloth banner (`ferrum_dynamic=1`) with regime texture slot.

**F4. Administration Banner / Screen** — `ferrum.la_ab_banner`
Standalone: building-scale cloth drop (dynamic) or rigid screen (emissive,
`flicker: bool`), `aspect`, `mount: enum(facade/spanning/pole)`.

**F5. Utility Parasite** — `ferrum.la_ab_parasite`
Conduit runs + node pods crawling a host facade (curve-walk over the surface):
`run_length`, `branch_prob`, `pod_every: n m`, `pulse_emissive: 0–1`. The
aberration answer to C4 — same silhouette instinct, wrong cleanliness.

### Family G — Assembly

**G1. Parcel Filler** — `ferrum.la_parcel`
Takes a rectangle (dims or picked face): fills with one Family A/B building +
setbacks + lot clutter + fence, all seeded: `use: enum(dingbat/court/minimall/
walkup/vacant)`, `decay: 0–1` (drives E-pass params), `aberration: 0–1`
(chance of F-graft).

**G2. Block Assembler** — `ferrum.la_block`
A street-ringed block of parcels: `block_w/d`, `parcel_w`, `use_mix` weights,
`corner_commercial: bool`, calls G1 per parcel + D1 around. THE demo-level
tool: one redo-panel scrub = a different city block.

**G3. Skyline Card Ring** — `ferrum.la_skyline`
Distant-LOD backdrop: extruded silhouette cards of towers/pylons on a ring,
`radius`, `density`, `haze_tint` — feeds the vista past the playable zone.

---

## 4. Shared library work (prerequisites)

1. **`la/params.py`** — the operator/property glue: a decorator that builds
   operator properties from a `build_*` signature (single source of truth for
   defaults/ranges), the menu table, seeded-RNG helper, collection management.
2. **`la/materials.py`** — the palette above as node builders (extend
   `material_nodes.py`); every material in two states (sound/weathered) +
   vertex-color mask hookup for the E-passes; bake-able via the existing
   prefab bake path (`bake_material_prefabs.py`).
3. **Opening tags** — Family A/B generators tag window/door faces (face maps
   or material slots) so E2/E4 can find them without geometry heuristics.
4. **Curve-run utilities** — catenary generator (C4), surface-walk (F5),
   end-snapping for chainable sections (C1/C2/D1).

## 5. Engine/export notes

- Colliders: box proxies per massing generator (body + posts); C1/C2 use
  their hull mesh (they ARE the level geometry). Thin items (wires, fronds,
  decals) get `ferrum_lightmap_res=0` and no collider.
- Lightmap budget: massing tools set `ferrum_lightmap_res` from surface area;
  decal/wire geometry excluded. Dynamic: F3/F4 banners only.
- Probe placement: nothing to do — the offline `probe_bake` pass reads the
  baked SDF, so generated levels inherit adaptive probes for free (rpg-pjkb).
- Scale check: 1 BU = 1 m as in `great_hall.py`; dingbat floor height 2.7 m,
  freeway clearance 4.9 m, pole spacing per C4.

## 6. Build order

1. **Phase 0**: shared library (§4) + A1 Dingbat as the pattern-setter (it
   exercises props/menu/tags/materials/colliders end-to-end).
2. **Phase 1 (streetscape MVP)**: B1 mini-mall, C4 power lines, D1 street,
   D4 fence, E1 weathering — one block of recognizable LA.
3. **Phase 2 (verticality + landmarks)**: B4 deco tower, B3 theater front,
   C3 parking, A3 walk-up, E2/E4 passes.
4. **Phase 3 (megaform)**: C1 river channel, C2 freeway, C5 drains, D3 palms,
   E3 desertification.
5. **Phase 4 (the regime)**: F1–F5 aberration overlay.
6. **Phase 5**: G1–G3 assembly + a demo district scene.

Each phase = one ticket batch under rpg-2lyk; each tool = TDD-ish (a headless
`build_*` smoke script asserting counts/bounds/tags via the MCP bridge, since
bmesh logic is testable without the operator layer).
