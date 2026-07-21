---
id: rpg-qdr5
status: closed
deps: [rpg-gh2z, rpg-5ljd]
links: []
created: 2026-07-21T09:32:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-2lyk
---
# LA gen A1: Dingbat Apartment generator (pattern-setter)


MODELING QUALITY BAR (see ref/archgen_dystopian_la.md section 0): NO simplified blockouts or bare primitives -- production topology from the first commit. All-quad meshes, no mesh errors, no T-junctions, good edge flow (complete loops around openings, holding edges for bevels, poles rerouted off visible flats). When in doubt, draw an ASCII topology diagram FIRST (module docstring + this ticket) referencing standard modeling practice. Programmatic validation (quad %, manifold, doubles, junction audit) in the smoke check.

ferrum.la_dingbat per ref/archgen_dystopian_la.md A1: body with carport void, external switchback stair, facade applique sets, window grid + awnings + AC units, address numerals, balcony rails. First full exerciser of the phase-0 glue: props/menu/tags/materials/colliders end-to-end. Topology plan REQUIRED before code (carport-void loops, window loops, stair landing flow).

## Acceptance Criteria

Acceptance: (1) build_* passes the programmatic topology validation; (2) redo-panel operator with every kwarg as a property, seeded determinism; (3) DISPLAY TO USER: have the user sign off on the wireframes after viewing them interactively in a live Blender session.

## Topology plan (drawn before code, per quality bar)

GLOBAL GRID STRATEGY: the shell is ONE welded bmesh whose loop lines are
shared globally -- horizontal Z-levels run around all four walls (so wall
grids weld at corners with plain 4-valence verts), X/Y lines carry every
window jamb. Openings are grid CELLS, never booleans.

Z-levels (floors=2):
  z8 5.55  ---------------------------  parapet top (cap ring)
  z7 5.15  --------------------------   roof plane (inset)
  z6 4.85  ---[head F2]---
  z5 3.65  ---[sill F2]---
  z4 2.75  ---------------------------  slab top / fascia bottom
  z3 2.45  ---------------------------  carport soffit / slab underside
  z2 2.10  ---[head F1]--- (rear/side walls only)
  z1 0.90  ---[sill F1]---
  z0 0.00  grade

FRONT FACADE (viewer facing -Y), width W, window_cols=4, carport full-width
between end piers (P = margin piers, carry ground load visually):

      x0  xa  j1a j1b  j2a j2b  j3a j3b  j4a j4b  xb  xW     <- shared X lines
  z8  +---+----+---+----+---+----+---+----+---+----+---+
      |   |    |   |    |   |    |   |    |   |    |   |     parapet band
  z6  +---+----+===+----+===+----+===+----+===+----+---+
      |   |    |win|    |win|    |win|    |win|    |   |     F2 window row
  z5  +---+----+===+----+===+----+===+----+===+----+---+
      |   |    |   |    |   |    |   |    |   |    |   |     spandrel
  z4  +---+----+---+----+---+----+---+----+---+----+---+
      |   |                 fascia                 |   |     slab edge band
  z3  +---+----+---+----+---+----+---+----+---+----+---+
      | P |///////////  CARPORT VOID  /////////////| P |     no faces: the
  z0  +---+  o    o    o    o    o    (posts)      +---+     boundary loop
                                                              rings the void
  o = square posts (separate closed shells, 6 quads each)

WINDOW CELL (all cells identical; 4-valence corners only):

   cell corner (shared with wall grid)
      +--------------------+
      |   frame ring quad  |      outer ring: wall plane
      |  +--------------+  |      inner ring: recessed -0.08 in Y
      |  | jamb quads   |  |      ring->pane: 4 jamb quads (3D return)
      |  |  +--------+  |  |
      |  |  | pane Q |  |  |      pane: 1 quad (glass material slot)
      |  |  +--------+  |  |
      |  +--------------+  |
      +--------------------+

CARPORT SECTION (side view, +Y into page):
                       front wall F2
  z4 ----------------+=================
  z3   soffit quads  |  fascia band     soffit grid shares X lines with
     +---------------+                  facade; meets recessed ground wall
     |   (grid y: 0..cd)                at shared verts (no T-junction)
  z0-+----[recessed wall + unit doors at y=cd]----

PARAPET (plan corner): outer wall rises z7->z8, cap ring (1 quad wide),
inner drop z8->z7, roof plane inset -- corner verts 4-valence throughout.

STAIR (switchback, separate shells): each step a closed 6-quad box; two
stringer plates (thin boxes); landing box; rail = square posts + quad-strip
handrail. No sawtooth end-caps (they would be L-shaped ngons).

Shells inventory: [body(walled+soffit+roof, ONE manifold-with-boundary mesh),
posts xN, stair steps, stringers, landing, rails, awnings (open quad strips),
AC boxes]. Auditor must report: 100% quads, 0 T-junction, 0 doubles.

## GENERATOR COMPLETENESS RULES (universal, see ref/archgen_dystopian_la.md 1b)

1. TWO MODES: `facade` AND `interior` -- interior adds all structural walls,
   party walls, load-bearing columns/beams, slabs, stair/corridor cores, as
   just-built and fully walkable. NO furniture/carpet/doors (separate tasks).
   Interior shares the global line grid + the same topology bar.
2. THREE PARAMETER TIERS: numeric variation of most features; MONOTONY
   BREAKERS (optional major structural elements that change the massing --
   e.g. an optional carport lowers the building to grade when absent;
   alternate footprint shapes where feasible); and story options.
3. STORY OPTIONS: 2-3 "particularly interesting" switches, OFF by default,
   telling a coherent thematic story (drought / abandonment / regime /
   resistance). See the doc 3b table for this tool's canonical set.

Story options: all_broken (shattered+boarded windows); sealed_unit (aberration resin); rooftop_roost (signal pigeon loft). Monotony breakers: carport optional (building drops to grade), L-plan variant, flat vs mansard roofline.

RULES 4+5 (addendum): (4) UV-unwrap AS YOU GO -- every object ships with a
real, non-degenerate UV layout (seams as deliberate as edge flow; consistent
texel density); never deferred. (5) NATURAL VERTEX GROUPS per subpart as you
build (e.g. steps/windows/awnings/facade_front/carport/doors/parapet) from
the shared name vocabulary -- one-click subpart selection forever after.

## Stair topology plan (rev 2 -- proper structure, no box kit)

FLIGHT (side view, one storey; all quads):

      stringer (sheared box, 6 planar quads: shear keeps every face planar)
     ____________________________________
    /                                   /|___ tread quad (horizontal)
   /   _/|_/|_/|_/|  <- riser quads    / /
  /___/________________________________/ /   soffit: ONE sloped quad closing
  |______________________________________/    the underside between stringers

  - tread/riser STRIP spans between the two stringers' inner faces; its open
    side boundaries terminate INSIDE the stringer faces (vert-on-face contact
    between clean shells -- sanctioned; never vert-on-EDGE).
  - the sawtooth profile that would be an ngon end-cap is exactly what the
    stringers cover: that is what stringers are FOR.

TOWER (plan): lane A (outer) up to the half-landing, lane B (inner) back up
to the walkway. Half-landing = slab plate carried by FOUR continuous square
posts grade -> top (outside the slab corners, 20 mm proud: touching reads
structural, no shared planes). Walkway extension over the lanes carries the
arrival; its own posts land under the extension edge.

Every flight element repeats per storey; stringers/strips/posts are separate
clean shells; the auditor gates: all-quad, no T-junction, no doubles, UVs.

## Notes

**2026-07-21T22:42:22Z**

Detail items split into follow-up tickets so A1 can close on wireframe sign-off of the built scope: rpg-gus3 (A1.1 cursive nameplate, text/SVG), rpg-vhvf (A1.2 facade applique sets), rpg-fwed (A1.3 address numerals), rpg-caur (A1.4 stair/walkway railings). Built + user-iterated in-scope: shell/carport(partial+open-side)/loggias(walkup corner + catwalk + skippable platform)/scissor stairs/rear walkway both modes/plan-driven fenestration/per-side gable sashes/awning+AC fit gates/story options.
