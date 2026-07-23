# LA Architectural Generators — Reuse-Driven Build Order

Suggested construction order for the `Arch gen: … (bpy/bmesh)` backlog derived from
the *rag & bone shop* reference set (tag `ragbone-ref`; category tags `arch/*`).

## Principle: kit of parts, shared elements first

The generators form a dependency stack. Building them bottom-up means each layer is
assembled from **already-finished, reusable** helpers instead of re-deriving the
same geometry (a railing, a window unit, a cornice) in every building generator:

```
Phase 0  Core elements (railings, stairs, columns, windows, doors, cornices,
         awnings, signs, balconies, roof/truss kits)  ← everything reuses these
Phase 1  Assemblies (storefronts, facades, arcades, fire escapes, halls)  ← compose Phase 0
Phase 2  Building typologies (whole dingbats/blocks/towers)               ← compose Phase 1
Phase 3  Independent infrastructure & fixtures                            ← parallel track
```

Each helper should expose parameters (profile, spacing, infill, material) so the
assemblies above can drive it. Match the repo's existing discipline: 100% quads,
welded islands, no coincident planes, vertex-group tags, UVs.

---

## Phase 0 — Core reusable elements (build first)

### 0a. Railings & guards — **the single most-reused element; do this first**
Reused by every stair, balcony, mezzanine, arcade, walkway.
- `rpg-giun` wrought-iron juliet balcony railing  ← canonical picket/rail profile
- `rpg-jekw` mall atrium balcony balustrade (curved run)
- `rpg-22o2` wrought-iron stairwell gate

### 0b. Stair flights (reuse **0a**)
- `rpg-yhkd` exterior straight-run stucco stair (pipe rail)
- `rpg-mv16` exterior steel switchback stair
- `rpg-2gji` cantilevered folded-plate concrete stair
- `rpg-o1d0` exterior steel stoop staircase
- `rpg-dlqc` interior switchback stair + mezzanine landings
- `rpg-gu4x` exterior concrete parapet stair
- `rpg-6nv1` hillside public stair street
- `rpg-gtfv` curved stone concourse stair (tubular pipe-handrail banks → feeds 0a)

### 0c. Columns, piers & frames (reused by colonnades, arcades, halls, canopies, viaducts)
- `rpg-4pdp` round columns w/ banded capitals (canonical column)
- `rpg-hsgd` tile-clad column
- `rpg-badf` exposed steel post-and-beam frame
- `rpg-mgjo` riveted plate-girder frame w/ knee braces

### 0d. Windows & grilles (reused by every facade & typology)
- `rpg-bit5` multi-pane steel-sash factory window ← canonical window unit
- `rpg-unwk` stone-lintel double-hung window (its window sub-unit)
- `rpg-ywvm` wrought-iron window security grille (overlay onto any window)

### 0e. Doors, shutters & entries (reused by storefronts, service walls, typologies)
- `rpg-0ag1` rolling metal shutter (canonical roll-up)
- `rpg-afkp` segmented roll-up garage-door bank
- `rpg-98iu` service wall w/ roll-up door + louver vents
- `rpg-oyu3` masonry arched doorway + keystone
- `rpg-pvk7` recessed streamline arched entry porch
- `rpg-fxl8` PVC strip-curtain storefront

### 0f. Cornices, parapets & crowns (reused by facades, blocks, towers)
- `rpg-9s27` Beaux-Arts modillion cornice (canonical cornice)
- `rpg-qlur` flat tar rooftop + parapet + mechanical penthouses
- `rpg-9glo` setback art-deco skyscraper crown
- `rpg-z814` domed cupola clocktower crown
- `rpg-yp3c` brutalist stepped-parapet entry

### 0g. Awnings & canopies (applied to storefronts/markets)
- `rpg-v3y9` cantilevered corrugated shop awning ← canonical projecting awning
- `rpg-imsr` projecting canvas storefront awning
- `rpg-xv7m` hanging-goods market-stall canopy
- `rpg-d8xv` strung tarp alley canopy
- `rpg-vumt` fuel-station canopy

### 0h. Signs (applied onto facades / roofs / storefronts)
- `rpg-8o58` projecting blade + rooftop letter signs ← canonical blade
- `rpg-ztkr` projecting theater marquee w/ letterboard
- `rpg-zohh` rooftop lattice billboard sign frame
- `rpg-w1nc` wall-mounted billboard panel
- `rpg-k4lg` backlit box fascia sign band (channel letters)
- `rpg-9u64` rooftop channel-letter sign rig
- `rpg-1e3n` projecting neon script sign
- `rpg-wqvo` cylindrical barber-pole sign column
- `rpg-vds0` suspended alley sign cluster
- `rpg-grdb` atomic-age starburst wall ornament + address plaque

### 0i. Balcony units (reuse **0a**)
- `rpg-kqj4` welded steel cage balcony enclosure
- `rpg-m1zv` projecting enclosed balcony sunroom box
- `rpg-66wz` cantilevered motel balcony walkway stack

### 0j. Roof planes & truss kits (reused by arcades, halls, atriums)
- `rpg-6b5a` exposed steel gable roof truss run ← canonical truss
- `rpg-tvdc` steel-truss barrel-vault glazed atrium skylight
- `rpg-lzpd` sawtooth steel-truss skylight roof
- `rpg-62kx` glass atrium dome skylight
- `rpg-essl` Spanish Colonial clay barrel-tile roof
- `rpg-w8lo` butterfly/shed roof w/ projecting fascia

---

## Phase 1 — Assemblies (compose Phase 0)

- `rpg-a1ep` tiled glazed **storefront bay** ← windows (0d) + shutter (0e) + awning (0g) + fascia (0h)
- `rpg-a72h` boarded-up storefront bay (state variant of `rpg-a1ep`)
- `rpg-5x5q` commercial strip storefront row ← array of `rpg-a1ep`
- `rpg-dtnm` covered sidewalk **arcade colonnade** ← columns (0c) + roof (0j)
- `rpg-tqpt` covered **market arcade** ← columns (0c) + gable skylight (0j) + canopy (0g)
- `rpg-if8j` steel zigzag **fire escape** ← flights (0b) + railings (0a) + landings, hung on a facade
- `rpg-791j` accreted kowloon **stacked tenement facade** ← windows (0d) + signs (0h) + balconies (0i)
- `rpg-cnzx` dense **tenement lightwell facade** ← windows (0d) + balconies (0i)
- `rpg-unwk` brick **tenement facade** ← double-hung (0d) + cornice (0f)
- `rpg-503n` applied relief-tile facade cladding panel (facade module)
- `rpg-yc56` rounded-corner cantilever **balcony tower** ← balconies (0i) + railings (0a)
- `rpg-7gmg` concrete **warehouse column hall** + mezzanine ← columns (0c) + truss (0j) + mezz railing (0a)
- `rpg-4y12` concrete oculus skylight vault ·  `rpg-2pv8` big-box rooftop skylight monitor
- **Interiors** (pair with halls; mostly standalone): `rpg-fk42` coffered ceiling ·
  `rpg-zb0u` drop-ceiling grid · `rpg-2isf` ductwork ceiling · `rpg-qfls` joist ceiling ·
  `rpg-lqaw` cage partition · `rpg-dynq` pegboard retail wall

---

## Phase 2 — Building typologies (assemble Phase 1)

- `rpg-cver` mid-century **dingbat** apartment ← carport columns (0c) + balcony walkway (0i) + exterior stair (0b) + windows (0d)  *(partly built — see `commercial.py`/`residential.py`)*
- `rpg-csaa` Googie pilotis block on stilts
- `rpg-pjet` Mediterranean-revival stepped apartment block
- `rpg-xupt` stepped ziggurat apartment block
- `rpg-kafu` Beaux-Arts commercial block ← facade (1) + cornice (0f) + storefront (1)
- `rpg-elxo` decayed **strip-mall** storefront block ← storefronts (1) + parapet (0f) + signs (0h)
- `rpg-75i8` big-box retail shell ← roll-up (0e) + sign panel (0h)
- `rpg-km8m` flat-roof brick industrial low-rise block
- `rpg-d2q1` setback high-rise office tower ← crown (0f)
- `rpg-j5fp` mushroom/disc megastructure tower

---

## Phase 3 — Independent infrastructure & fixtures (parallel track)

Low interdependency — can proceed on a separate contributor at any time; the noted
ones reuse Phase-0 columns/frames/trusses.

- **Roadway**: `rpg-lkl2` freeway viaduct (reuses columns 0c) · `rpg-la2z` interchange stack
- **Drainage**: `rpg-w135` trapezoidal concrete flood canal
- **Heavy steel**: `rpg-qgjc` gantry crane (reuses frame 0c) · `rpg-4e0h` pipe gantry + catwalk
- **Utilities**: `rpg-lcne` utility pole + wire tangle · `rpg-vfa8` meter + conduit wall · `rpg-9uxy` drainpipe/conduit run
- **Street furniture**: `rpg-kg17` cast-iron ornamental streetlamp · `rpg-3zlb` gooseneck cobra streetlight · `rpg-rhca` vending-machine bank
- **Rooftop props**: `rpg-ibjo` wooden water tank · `rpg-fjzk` TV antenna cluster
- **Mech/fixtures**: `rpg-qw67` AC condenser cluster · `rpg-sxsr` bulkhead cage lamp
- **Transit/circulation props**: `rpg-kx2x` pedestrian skybridge · `rpg-qh9i` glass elevator shaft · `rpg-ldev` escalator bank
- **Misc structure**: `rpg-aoeu` shipping-container stack wall

---

## Critical path (recommended sequence)

1. **Railing kit** (0a) — unblocks all circulation + balconies.
2. **Stair flights** (0b) + **windows** (0d) + **doors/shutters** (0e) — the everyday facade + access vocabulary.
3. **Columns** (0c) + **cornices/crowns** (0f) — the massing vocabulary.
4. **Awnings** (0g) + **signs** (0h) + **balconies** (0i) + **roof/truss kits** (0j) — the applied dressing.
5. **Storefronts & facades** (Phase 1a: `rpg-a1ep`, `rpg-791j`, `rpg-cnzx`, `rpg-unwk`).
6. **Fire escape, arcades, halls** (Phase 1b: `rpg-if8j`, `rpg-dtnm`, `rpg-tqpt`, `rpg-7gmg`).
7. **Typologies** (Phase 2) — assemble finished assemblies into whole buildings.

Phase 3 (infrastructure/fixtures) runs in parallel throughout — it barely touches the
building kit, so it never blocks the critical path.
