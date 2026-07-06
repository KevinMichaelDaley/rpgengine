# SRD Redesign Plan — Stochastic Rewrite Descent for Dungeon Layout

**Reference:** Kodnongbua et al., "Design for Descent: What Makes a Shape Grammar Easy to Optimize?", SIGGRAPH Asia 2025.
**PDF:** `/home/kmd/Downloads/3757377.3764004.pdf`

---

## Algorithm Review: Bugs in Current Implementation

### Bug 1 — Parameter Index Confusion (fundamental, breaks everything)

`srd_param_t` says:
```c
SRD_PARAM_WIDTH = 0, HEIGHT = 1, EXTENT_X = 2, EXTENT_Z = 3
```

But `srd_optimize` initializes positions into `params[0]` and `params[3]`:
```cpp
n->params[0] = (float)(grid->regions[...].first_x * 2 + 1);   // center x stored as "WIDTH"
n->params[3] = (float)(grid->regions[...].first_z * 2 + 1);   // center z stored as "EXTENT_Z"
```

Then `gradient_step` reads `cx = params[SRD_PARAM_EXTENT_X]` = `params[2]` (never written,
stays 0.0) for the energy computation, but applies the gradient back to `params[0]` and
`params[3]`. The overlap energy is always computed at (0,0). The finite-diff step perturbs
`EXTENT_X`/`EXTENT_Z` as if they were positions — they aren't.

`soft_edge` compounds this by computing distance from `params[2]` and `params[3]`, i.e.
extent_x (~0) and extent_z (centre_z) — so the "dx" distance component is always ~0.

There are **no stored centre positions** consistent between `soft_edge`, `gradient_step`, and
`kernel_line_of_sight`. Each function looks at different slots.

### Bug 2 — `soft_edge` half_sum Formula

```cpp
double half_sum = a->params[EXTENT_X] + b->params[EXTENT_X] +
                  a->params[EXTENT_Z] + b->params[EXTENT_Z];
```

This adds all 4 extents together. For a proper box-proximity test you'd project onto the
distance axis and sum the extents along that axis only. The current formula gives a proximity
value uncorrelated with actual spatial overlap.

### Bug 3 — PATH_DISTANCE Loss Does Not Compute Path Distance

The code builds a row-normalised `Kp` (inverse-distance weights), then runs 500 iterations of
`L[i] = Σ Kp[i][j]*L[j]` with `L[na]=0` never explicitly set (it starts 0 by default, which
is only coincidentally correct for the source). This converges to a stationary distribution,
not a geodesic distance. `d = L[nb]` will be in [0,1], never matching a physical distance in
metres, so the squared error against `t->target_value` is meaningless.

### Bug 4 — LINE_OF_SIGHT Loss Formula Is Always Constant

```cpp
loss += (1.0 - L[na] + L[nb]) * 0.05;
```

`L[na] = 0` and `L[nb] = 1` are boundary conditions that never change through the 500 Jacobi
iterations. The loss contribution is always `(1.0 - 0 + 1) * 0.05 = 0.1`. It penalises nothing.

### Bug 5 — No Metropolis Criterion

The rewrite acceptance is `delta > 0` (pure greedy) plus a special-case for terminal emit
actions when `current_loss < 0.03`. True SRD would accept uphill moves with probability
`exp(delta/T)`, shrinking the acceptance window as temperature anneals. Without this the
algorithm cannot escape local minima and "stochastic" in the name is misleading.

### Bug 6 — Sandbox Rule Application Is Disconnected From Real Grammar

The sandbox deep-copies the grammar, applies the rule in the copy, evaluates loss, then
applies the **same rule independently** to the original grammar with a fresh random position
offset. The sandbox result does not predict what the real grammar will look like after the
apply, making the loss comparison invalid.

### Bug 7 — Grammar Tree Expansion Is the Wrong Representation for SDF-Based Optimisation

The current design conflates two incompatible things: a grammar parse tree (discrete
structural state) and a continuous parameter space (positions/extents). Gradient descent on
continuous params makes sense, but those params live in the grammar tree nodes alongside
non-terminal expansion state that the gradient should never touch. The result is that gradient
steps happen on params that the rewrite rules also modify structurally, with no coherent joint
state.

### Bug 8 — SRD Algorithm Itself Is Wrong (vs. Paper Algorithm 2)

The paper (Section 3.3 + Algorithm 2 in supplemental) says:

> Randomly select **K=64** valid rewrite rules. For each ρ, compute ΔL̃_ρ ≈ L(s,p) − L(s',p̂)
> where p̂ is the result of **one LocalOptimize step** under the new structure. Then apply
> **greedy max-cover** over the compatibility graph of all rules where ΔL_ρ > 0.

The current code does:
- Pick **one** random node (weighted by age/depth)
- Try **one** random matching rule
- Accept if `delta > 0` (greedy)

This is neither K-candidate evaluation, nor max-cover, and the LocalOptimize step (one
gradient step under the proposed structure) is completely absent. The age/depth weighting is
an invention not in the paper.

---

## Theoretical Foundation (Kodnongbua et al.)

The paper identifies **four non-negotiable properties** for a descent-friendly grammar.
The current implementation violates all four.

### Property 1 — REVERSIBILITY (violated completely)

> If there is a rule A→B, there must also be a rule B→A.
> Formal: ∀ρ ∈ R : ρ(x,p) = (y,q), ∃ρ′ ∈ R : ρ′(y,q) = (x,p)
> Examples from paper: Split/Merge, Add/Remove-Loop

The current grammar is **purely constructive**: every rule expands a non-terminal into
children. There are no inverse rules. Once a node is expanded, it can never be unexpanded.
The optimiser has no way to undo mistakes — any bad expansion is permanent. Ablation Tr-2
in Table 2 shows that adding RemoveLeaf alongside AddLeaf improves quality from 15.4→21.5
PSNR precisely because the optimiser needs to correct errors.

### Property 2 — JUMP CONTINUITY (violated)

> Applying rules incurs negligible instantaneous change in the shape.
> Formal: ∀ρ ∈ R applicable to (x,p), |I(x,p) − I(ρ(x,p))| < ε
> Example from paper: Local segment splitting

When `SRD_ACT_ROOM` fires, children are instantiated with random positions near the parent.
The rendered shape changes by an uncontrolled amount — potentially adding a full-sized room
in one step. This destroys gradient progress because the loss landscape jumps discontinuously.

**Fix:** New rooms must be spawned at size **ε** (e.g., hw=hd=0.01 world units), then grown
by continuous gradient descent. The discrete rewrite changes topology; the continuous
parameters absorb magnitude.

### Property 3 — LOCAL GEOMETRIC CONTROL (violated)

> There should exist rules which allow any local change anywhere on the shape without
> affecting distant parts.
> Formal: ∀v ∈ V, ∃ρ ∈ R : ρ(v,o) = (w,q), where |w| is small
> Example from paper: Add-Anywhere

The current grammar can only fire rules on nodes matching the original ASCII grid structure.
There is no `AddRoom-Anywhere` equivalent. Without this, the optimiser gets stuck if the
initial ASCII art is missing a region the critic wants. Ablation Tr-5 shows AddAnywhere
enables filling disconnected regions that Tr-4 misses entirely.

### Property 4 — REPAIRABILITY (violated)

> If constraints exist, there should exist rules to repair a shape which violates them.
> Formal: If C ⊂ X is a feasible region, ∀(x,p) ∈ X, ∃ρ ∈ R : ρ(x,p) ∈ C
> Example from paper: Resolve-Intersections

There are no repair rules for overlapping rooms. The optimiser must avoid constraint
violations entirely, which blocks exploration.

---

## Proposed Architecture

### State Representation: SDF Layout (Union-Rect style)

```c
#define SRD_MAX_BOXES   512
#define SRD_EPSILON     0.01f   /* minimum spawned size for jump continuity */

typedef enum {
    SRD_ROOM_GENERIC = 0,
    SRD_ROOM_BAR,
    SRD_ROOM_ENTRANCE,
    SRD_ROOM_PRIVATE,
    SRD_ROOM_STAIR_UP,
    SRD_ROOM_STAIR_DOWN,
    SRD_ROOM_CORRIDOR,
    SRD_ROOM_DEAD_END,
    SRD_ROOM_SECRET,
    SRD_ROOM_BOSS,
    SRD_ROOM_TREASURE,
} srd_room_type_t;

/* One per room or corridor segment */
typedef struct {
    float          cx, cz;     /* centre position in world units */
    float          hw, hd;     /* half-widths (x and z axes) */
    srd_room_type_t type;
    uint32_t       flags;      /* SRD_BOX_EPSILON | SRD_BOX_REPAIR_ONLY */
} srd_sdf_box_t;

typedef struct {
    srd_sdf_box_t boxes[SRD_MAX_BOXES];
    int           n_boxes;
    /* Flat adjacency bit array: adj[i*SRD_MAX_BOXES + j] */
    uint8_t       adj[SRD_MAX_BOXES * SRD_MAX_BOXES];
} srd_sdf_layout_t;
```

The continuous parameters are `(cx, cz, hw, hd)` per box — cleanly separated from the
discrete structure (which boxes exist, which are adjacent).

### ASCII → Initial SDF (Axiom)

Parse ASCII → flood-fill regions (existing `srd_grid_parse`) → each region becomes one box:
- `cx, cz` = centroid of cells (1 cell = 1.0 world unit)
- `hw` = (xmax − xmin + 1) * 0.5
- `hd` = (zmax − zmin + 1) * 0.5
- `type` = from region character (R→GENERIC, B→BAR, G→ENTRANCE, P→PRIVATE, ^→STAIR_UP, v→STAIR_DOWN)
- Adjacency from `grid->edges`

This is the axiom (s₀, p₀) fed into the SRD loop.

### Differentiable Soft Rasteriser

The rendering function `I(s, p)` converts the SDF layout to a soft occupancy grid:

1. **Box SDF:** `sdf_box(i, qx, qz) = max(|qx−cx_i|/hw_i − 1,  |qz−cz_i|/hd_i − 1)`
2. **Smooth-min union (LogSumExp):** `sdf_union(qx,qz) = −T·log(Σ_i exp(−sdf_box(i,qx,qz)/T))`
3. **Soft occupancy:** `occ(qx,qz) = sigmoid(−sdf_union(qx,qz) / T)`

The C function `srd_layout_rasterize(layout, grid_w, grid_h, temperature, float *out)`
fills a W×H float buffer. `srd_critic.cpp` wraps this in `torch::from_blob` for autograd.
Gradient of L w.r.t. `(cx, cz, hw, hd)` flows through libtorch autograd automatically.

---

## Full Rule Set

Rules are organised by the paper property they primarily satisfy. Every non-repair rule has
a registered inverse. Repair rules are exempt from the inverse requirement (paper §3.2).
Rules marked ε spawn new boxes at `hw = hd = SRD_EPSILON` to satisfy Jump Continuity.

---

### Group 1 — Room Topology (REVERSIBILITY + JUMP CONTINUITY)

**Rule 1: SplitRoomH(i, frac)**
- **Parameters:** box index `i`; split fraction `frac ∈ (0,1)` along the X axis
- **Precondition:** box `i` exists; `hw_i > 2*SRD_EPSILON`
- **Effect:** removes box `i`; inserts two new boxes `a` and `b` side-by-side along X.
  `hw_a = hw_i * frac`, `hw_b = hw_i * (1−frac)`.
  `cx_a = cx_i − hw_i + hw_a`, `cx_b = cx_i + hw_i − hw_b`.
  `hd_a = hd_b = hd_i`. Both inherit `type` and adjacency of `i`.
  Adjacency between `a` and `b` set to true.
- **Jump continuity:** children cover exactly the same area as parent; rendered shape unchanged.
- **Inverse:** Rule 2 `MergeRooms(a, b)`

**Rule 2: MergeRooms(i, j)**
- **Parameters:** two adjacent box indices `i`, `j`
- **Precondition:** `adj[i][j]` is true; boxes are axis-aligned and share a full edge
  (i.e., their bounding union is a valid rectangle, checked by comparing non-split axes)
- **Effect:** removes boxes `i` and `j`; inserts one new box whose bounds are the union AABB.
  Inherits `type` of whichever box is larger. Inherits combined adjacency of both.
- **Jump continuity:** union AABB equals the combined area; rendered shape unchanged.
- **Inverse:** Rule 1 `SplitRoomH` or Rule 3 `SplitRoomV` (direction inferred)

**Rule 3: SplitRoomV(i, frac)**
- **Parameters:** box index `i`; split fraction `frac ∈ (0,1)` along the Z axis
- **Precondition:** box `i` exists; `hd_i > 2*SRD_EPSILON`
- **Effect:** same as SplitRoomH but along Z. `hd_a = hd_i * frac`, `hd_b = hd_i * (1−frac)`.
  `cz_a = cz_i − hd_i + hd_a`, `cz_b = cz_i + hd_i − hd_b`.
- **Jump continuity:** children cover exactly the same area as parent.
- **Inverse:** Rule 2 `MergeRooms`

**Rule 4: AddRoomN(i)**
- **Parameters:** box index `i` (the anchor room)
- **Precondition:** box `i` exists; space available to the north (cz − hd − SRD_EPSILON > layout_min_z)
- **Effect:** inserts new box `j` with `cx_j = cx_i`, `cz_j = cz_i − hd_i − SRD_EPSILON`,
  `hw_j = hw_i`, `hd_j = SRD_EPSILON`, `type_j = SRD_ROOM_GENERIC`.
  Sets `adj[i][j] = adj[j][i] = true`.
- **Jump continuity:** ε — new box spawns at minimum size; rendered occupancy change < 0.001.
- **Inverse:** Rule 8 `RemoveRoom(j)`

**Rule 5: AddRoomS(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` exists; space available to the south
- **Effect:** inserts new box `j` at `cz_j = cz_i + hd_i + SRD_EPSILON`, otherwise identical
  to AddRoomN.
- **Jump continuity:** ε
- **Inverse:** Rule 8 `RemoveRoom(j)`

**Rule 6: AddRoomE(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` exists; space available to the east
- **Effect:** inserts new box `j` at `cx_j = cx_i + hw_i + SRD_EPSILON`,
  `cz_j = cz_i`, `hw_j = SRD_EPSILON`, `hd_j = hd_i`.
- **Jump continuity:** ε
- **Inverse:** Rule 8 `RemoveRoom(j)`

**Rule 7: AddRoomW(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` exists; space available to the west
- **Effect:** inserts new box `j` at `cx_j = cx_i − hw_i − SRD_EPSILON`,
  otherwise identical to AddRoomE.
- **Jump continuity:** ε
- **Inverse:** Rule 8 `RemoveRoom(j)`

**Rule 8: RemoveRoom(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` exists
- **Effect:** removes box `i` from the layout. Clears all adjacency entries for `i`.
  If removing `i` would disconnect the layout graph, the rule is still valid —
  REPAIRABILITY rules will re-establish connectivity.
- **Jump continuity:** not guaranteed if box is large; caller should shrink via
  TrimRoom before removing (or accept the discontinuity at removal time).
- **Inverse:** Rule 4/5/6/7 `AddRoom*` (direction determined by which neighbour `i` was adjacent to)

**Rule 9: TrimRoom(i, side, amt)**
- **Parameters:** box index `i`; `side ∈ {N,S,E,W}`; `amt > 0` in world units
- **Precondition:** box `i` exists; trimming by `amt` leaves `hw_i > SRD_EPSILON` (or `hd_i`)
- **Effect:** reduces the extent of box `i` on the given side by `amt`.
  e.g. side=N: `hd_i -= amt/2; cz_i += amt/2`.
- **Jump continuity:** ✓ — continuous parameter change; rendered shape shrinks by `amt`.
- **Inverse:** Rule 10 `ExtendRoom(i, side, amt)`

**Rule 10: ExtendRoom(i, side, amt)**
- **Parameters:** box index `i`; `side ∈ {N,S,E,W}`; `amt > 0`
- **Precondition:** box `i` exists
- **Effect:** increases the extent of box `i` on the given side by `amt`.
  e.g. side=N: `hd_i += amt/2; cz_i -= amt/2`.
- **Jump continuity:** ✓
- **Inverse:** Rule 9 `TrimRoom(i, side, amt)`

**Rule 11: ScaleRoom(i, sx, sz)**
- **Parameters:** box index `i`; scale factors `sx > 0`, `sz > 0`
- **Precondition:** box `i` exists; `hw_i*sx > SRD_EPSILON`, `hd_i*sz > SRD_EPSILON`
- **Effect:** `hw_i *= sx; hd_i *= sz`. Centre unchanged.
- **Jump continuity:** ✓ if sx,sz close to 1.0
- **Inverse:** Rule 11 `ScaleRoom(i, 1/sx, 1/sz)`

**Rule 12: AddAlcove(i, side)**
- **Parameters:** box index `i`; `side ∈ {N,S,E,W}`
- **Precondition:** box `i` exists
- **Effect:** inserts a new small box `j` of type `SRD_ROOM_GENERIC` adjacent to side of `i`,
  with `hw_j = SRD_EPSILON`, `hd_j = SRD_EPSILON`. Sets `adj[i][j]`.
- **Jump continuity:** ε
- **Inverse:** Rule 13 `RemoveAlcove(j)`

**Rule 13: RemoveAlcove(j)**
- **Parameters:** box index `j` flagged as an alcove (small box with ≤1 adjacency)
- **Precondition:** `j` has at most one neighbour
- **Effect:** removes box `j` and its adjacency entries.
- **Jump continuity:** ε if box was small
- **Inverse:** Rule 12 `AddAlcove`

**Rule 14: AddAntechamber(i, side)**
- **Parameters:** box index `i`; `side ∈ {N,S,E,W}`
- **Precondition:** box `i` exists
- **Effect:** inserts a new box `j` of type `SRD_ROOM_GENERIC` adjacent to `i` on the given
  side, with `hw_j = hw_i * 0.5`, `hd_j = SRD_EPSILON` (or Z variant). Sets `adj[i][j]`.
  Antechamber is wider than an alcove but shorter (acts as a lobby/buffer room).
- **Jump continuity:** ε
- **Inverse:** Rule 15 `RemoveAntechamber(j)`

**Rule 15: RemoveAntechamber(j)**
- **Parameters:** box index `j`
- **Precondition:** `j` has exactly one neighbour
- **Effect:** removes `j` and adjacency entries.
- **Jump continuity:** ε if box was small
- **Inverse:** Rule 14 `AddAntechamber`

**Rule 16: ConvertType(i, new_type)**
- **Parameters:** box index `i`; target type `new_type`
- **Precondition:** box `i` exists; `new_type != i->type`
- **Effect:** `i->type = new_type`. No geometric change.
- **Jump continuity:** ✓ — no geometry changes; critic loss may change due to type semantics.
- **Inverse:** Rule 16 `ConvertType(i, old_type)` (store old type in rule params)

---

### Group 2 — Corridors & Connections (REVERSIBILITY + LOCAL GEOMETRIC CONTROL)

**Rule 17: AddCorridor(i, j)**
- **Parameters:** two box indices `i`, `j` (need not be adjacent)
- **Precondition:** `i != j`; no corridor box already directly connecting them
- **Effect:** inserts a new corridor box `k` of type `SRD_ROOM_CORRIDOR` with
  `cx_k = (cx_i+cx_j)*0.5`, `cz_k = (cz_i+cz_j)*0.5`, `hw_k = SRD_EPSILON`, `hd_k = SRD_EPSILON`.
  Sets `adj[i][k] = adj[k][i] = adj[j][k] = adj[k][j] = true`.
- **Jump continuity:** ε — corridor spawns at minimum size; gradient grows it.
- **Inverse:** Rule 18 `RemoveCorridor(k)`

**Rule 18: RemoveCorridor(k)**
- **Parameters:** corridor box index `k` of type `SRD_ROOM_CORRIDOR`
- **Precondition:** `k` is a corridor box
- **Effect:** removes box `k`. Clears adjacency for `k`.
  Does NOT add a direct adj edge between `k`'s former neighbours.
- **Jump continuity:** ε if corridor was narrow
- **Inverse:** Rule 17 `AddCorridor`

**Rule 19: WidenCorridor(k)**
- **Parameters:** corridor box index `k`
- **Precondition:** `k` is a corridor box
- **Effect:** `hw_k *= 1.5; hd_k *= 1.5` (capped at some maximum)
- **Jump continuity:** ✓ — continuous change
- **Inverse:** Rule 20 `NarrowCorridor(k)`

**Rule 20: NarrowCorridor(k)**
- **Parameters:** corridor box index `k`
- **Precondition:** `k` is a corridor box; `hw_k > SRD_EPSILON`, `hd_k > SRD_EPSILON`
- **Effect:** `hw_k /= 1.5; hd_k /= 1.5`
- **Jump continuity:** ✓
- **Inverse:** Rule 19 `WidenCorridor(k)`

**Rule 21: BendCorridor(k, frac)**
- **Parameters:** corridor box index `k`; bend point fraction `frac ∈ (0,1)` along corridor
- **Precondition:** `k` is a straight corridor box
- **Effect:** splits `k` into two corridor segments `k1`, `k2` at the bend point.
  `k1` connects from first endpoint to midpoint; `k2` from midpoint to second endpoint.
  Both spawn at `SRD_EPSILON` extent perpendicular to their axis.
  The bend point position is a continuous parameter that gradient can move.
- **Jump continuity:** ε — the bend point starts coincident with the split point.
- **Inverse:** Rule 22 `StraightenCorridor(k1, k2)`

**Rule 22: StraightenCorridor(k1, k2)**
- **Parameters:** two corridor boxes `k1`, `k2` that share an endpoint
- **Precondition:** `adj[k1][k2]` true; both are corridor type; share exactly one endpoint
- **Effect:** merges `k1` and `k2` into a single corridor box spanning both endpoints.
- **Jump continuity:** ✓ if extents matched
- **Inverse:** Rule 21 `BendCorridor`

**Rule 23: SplitCorridor(k, frac)**
- **Parameters:** corridor box index `k`; fraction `frac ∈ (0,1)`
- **Precondition:** `k` is a corridor box
- **Effect:** inserts a waypoint room `w` of type `SRD_ROOM_GENERIC` at fraction `frac`
  along the corridor. Splits `k` into two corridor stubs connecting to `w`.
  `w` spawns at `SRD_EPSILON`.
- **Jump continuity:** ε
- **Inverse:** Rule 24 `MergeCorridor(k1, w, k2)`

**Rule 24: MergeCorridor(k1, w, k2)**
- **Parameters:** corridor box `k1`, waypoint box `w`, corridor box `k2`
- **Precondition:** `adj[k1][w]` and `adj[w][k2]` true; `w` has exactly 2 neighbours
- **Effect:** removes `k1`, `w`, `k2`; inserts a single corridor box spanning both original endpoints.
- **Jump continuity:** ✓ if `w` was small
- **Inverse:** Rule 23 `SplitCorridor`

**Rule 25: BridgeComponents(i, j)**
- **Parameters:** two box indices `i`, `j` in disconnected graph components
- **Precondition:** `i` and `j` are in different connected components of the adjacency graph
- **Effect:** same as `AddCorridor(i, j)` — inserts a corridor box at ε size.
  This is the **LOCAL GEOMETRIC CONTROL** rule: it can fire on any two boxes regardless
  of their grid origin, enabling the optimiser to bridge gaps not in the original ASCII art.
- **Jump continuity:** ε
- **Inverse:** Rule 18 `RemoveCorridor(k)`

**Rule 26: AddLoop(i, j)**
- **Parameters:** two already-connected box indices `i`, `j` (same component)
- **Precondition:** `i` and `j` are reachable but not directly adjacent; adding a corridor
  would create an alternate path (a loop in the graph)
- **Effect:** same as `AddCorridor(i, j)`.
- **Jump continuity:** ε
- **Inverse:** Rule 27 `RemoveLoop(k)`

**Rule 27: RemoveLoop(k)**
- **Parameters:** corridor box `k` whose removal does not disconnect the graph
- **Precondition:** removing `k` leaves the layout still connected
- **Effect:** same as `RemoveCorridor(k)`.
- **Jump continuity:** ε if narrow
- **Inverse:** Rule 26 `AddLoop`

**Rule 28: ShortcutPath(i, j)**
- **Parameters:** two box indices `i`, `j` with existing long path between them
- **Precondition:** shortest path length between `i` and `j` > 2 hops
- **Effect:** same as `AddCorridor(i, j)` — creates a direct corridor shortcut.
- **Jump continuity:** ε
- **Inverse:** Rule 29 `RemoveShortcut(k)`

**Rule 29: RemoveShortcut(k)**
- **Parameters:** corridor box `k` whose removal does not disconnect the layout
- **Precondition:** removing `k` still leaves both its neighbours connected via another path
- **Effect:** same as `RemoveCorridor(k)`.
- **Jump continuity:** ε if narrow
- **Inverse:** Rule 28 `ShortcutPath`

**Rule 30: RerouteCorridor(k, new_j)**
- **Parameters:** corridor box `k` connecting boxes `i` and `old_j`; new target box `new_j`
- **Precondition:** `k` is a corridor box; `new_j != old_j`; `new_j` exists
- **Effect:** moves the second endpoint of `k` from `old_j` to `new_j`.
  Updates adjacency: clears `adj[k][old_j]`, sets `adj[k][new_j]`.
  Updates `cx_k, cz_k` to midpoint of `i` and `new_j`.
- **Jump continuity:** ε if endpoint movement is small (gradient can refine)
- **Inverse:** Rule 30 `RerouteCorridor(k, old_j)`

---

### Group 3 — Doors & Portals (REVERSIBILITY)

**Rule 31: AddDoor(i, wall_side)**
- **Parameters:** box index `i`; `wall_side ∈ {N,S,E,W}`
- **Precondition:** box `i` exists; no door already on that wall
- **Effect:** sets a door flag on box `i` for the given wall. Stores door width parameter
  `door_width = SRD_EPSILON` as an extra float on the box. No geometry change to the box itself.
  The critic and rasteriser interpret the door flag when rendering wall permeability.
- **Jump continuity:** ε — door starts at minimum width
- **Inverse:** Rule 32 `RemoveDoor(i, wall_side)`

**Rule 32: RemoveDoor(i, wall_side)**
- **Parameters:** box index `i`; `wall_side`
- **Precondition:** door exists on `i` on `wall_side`
- **Effect:** clears the door flag and width parameter.
- **Jump continuity:** ε if door was narrow
- **Inverse:** Rule 31 `AddDoor`

**Rule 33: WidenDoor(i, wall_side)**
- **Parameters:** box index `i`; `wall_side`
- **Precondition:** door exists on `i` on `wall_side`; `door_width < max_door_width`
- **Effect:** `door_width *= 1.5`
- **Jump continuity:** ✓
- **Inverse:** Rule 34 `NarrowDoor(i, wall_side)`

**Rule 34: NarrowDoor(i, wall_side)**
- **Parameters:** box index `i`; `wall_side`
- **Precondition:** door exists; `door_width > SRD_EPSILON`
- **Effect:** `door_width /= 1.5`
- **Jump continuity:** ✓
- **Inverse:** Rule 33 `WidenDoor`

---

### Group 4 — Stairs (REVERSIBILITY + LOCAL GEOMETRIC CONTROL)

**Rule 35: AddStairUp(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` exists; no stair-up box already in layout (or per design: limit one)
- **Effect:** inserts a new box `j` of type `SRD_ROOM_STAIR_UP` inside or adjacent to `i`,
  with `hw_j = hd_j = SRD_EPSILON`. Sets `adj[i][j]`.
- **Jump continuity:** ε
- **Inverse:** Rule 37 `RemoveStair(j)`

**Rule 36: AddStairDown(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` exists; no stair-down box already in layout (or per design: limit one)
- **Effect:** inserts a new box `j` of type `SRD_ROOM_STAIR_DOWN`, otherwise identical to AddStairUp.
- **Jump continuity:** ε
- **Inverse:** Rule 37 `RemoveStair(j)`

**Rule 37: RemoveStair(j)**
- **Parameters:** stair box index `j`
- **Precondition:** `j` is of type `SRD_ROOM_STAIR_UP` or `SRD_ROOM_STAIR_DOWN`
- **Effect:** removes box `j` and adjacency entries.
- **Jump continuity:** ε if box was small
- **Inverse:** Rule 35 or 36 depending on type

**Rule 38: RelocateStair(j, target_i)**
- **Parameters:** stair box index `j`; new host room `target_i`
- **Precondition:** `j` is a stair box; `target_i` exists; `target_i != j`'s current host
- **Effect:** moves `j` so it is adjacent to `target_i`. Updates `cx_j`, `cz_j` to be
  just outside `target_i`'s boundary. Clears old adjacency, sets new `adj[j][target_i]`.
- **Jump continuity:** not guaranteed if relocation is large — but the stair is type-labelled
  so the critic can pull it to the right place via gradient after the rewrite.
- **Inverse:** Rule 38 `RelocateStair(j, original_i)`

---

### Group 5 — Special Rooms (LOCAL GEOMETRIC CONTROL + overparameterisation)

These rules are the primary LOCAL GEOMETRIC CONTROL mechanism for dungeon semantics —
they can fire on any box regardless of the original ASCII art.

**Rule 39: AddDeadEnd(i, dir)**
- **Parameters:** box index `i`; direction `dir ∈ {N,S,E,W}`
- **Precondition:** box `i` exists
- **Effect:** inserts a new box `j` of type `SRD_ROOM_DEAD_END` adjacent to `i` in direction `dir`,
  spawned at `SRD_EPSILON`. Sets `adj[i][j]`. `j` will have exactly one neighbour.
- **Jump continuity:** ε
- **Inverse:** Rule 40 `RemoveDeadEnd(j)`

**Rule 40: RemoveDeadEnd(j)**
- **Parameters:** dead-end box index `j`
- **Precondition:** `j` of type `SRD_ROOM_DEAD_END` or any box with exactly one neighbour
- **Effect:** removes `j` and its adjacency entry.
- **Jump continuity:** ε if box was small
- **Inverse:** Rule 39 `AddDeadEnd`

**Rule 41: AddSecretRoom(i, wall_side)**
- **Parameters:** box index `i`; `wall_side ∈ {N,S,E,W}`
- **Precondition:** box `i` exists
- **Effect:** inserts a new box `j` of type `SRD_ROOM_SECRET` adjacent to side of `i`,
  spawned at `SRD_EPSILON`. Sets `adj[i][j]`.
  The secret room flag means the critic treats the connecting wall as normally solid
  (penalises direct visibility from common areas).
- **Jump continuity:** ε
- **Inverse:** Rule 42 `RemoveSecretRoom(j)`

**Rule 42: RemoveSecretRoom(j)**
- **Parameters:** secret room box index `j`
- **Precondition:** `j` of type `SRD_ROOM_SECRET`
- **Effect:** removes `j` and adjacency entries.
- **Jump continuity:** ε if small
- **Inverse:** Rule 41 `AddSecretRoom`

**Rule 43: AddBossRoom(near_i)**
- **Parameters:** anchor box index `near_i` (should be near a stair)
- **Precondition:** no boss room already in layout; `near_i` exists
- **Effect:** inserts a new box `j` of type `SRD_ROOM_BOSS` adjacent to `near_i`,
  spawned at `SRD_EPSILON`. Sets `adj[near_i][j]`.
  The critic applies a large-size penalty specifically to boss rooms, driving gradient to grow them.
- **Jump continuity:** ε
- **Inverse:** Rule 44 `RemoveBossRoom(j)`

**Rule 44: RemoveBossRoom(j)**
- **Parameters:** boss room box index `j`
- **Precondition:** `j` of type `SRD_ROOM_BOSS`
- **Effect:** removes `j` and adjacency entries.
- **Jump continuity:** ε if small
- **Inverse:** Rule 43 `AddBossRoom`

**Rule 45: AddTreasureRoom(i, dir)**
- **Parameters:** anchor box index `i`; direction `dir ∈ {N,S,E,W}`
- **Precondition:** `i` exists; treasure room should be a dead end (one entrance)
- **Effect:** inserts a new box `j` of type `SRD_ROOM_TREASURE` adjacent to `i` in `dir`,
  spawned at `SRD_EPSILON`. Sets `adj[i][j]`. `j` intentionally has only one neighbour.
- **Jump continuity:** ε
- **Inverse:** Rule 46 `RemoveTreasureRoom(j)`

**Rule 46: RemoveTreasureRoom(j)**
- **Parameters:** treasure room box index `j`
- **Precondition:** `j` of type `SRD_ROOM_TREASURE`
- **Effect:** removes `j` and adjacency entries.
- **Jump continuity:** ε if small
- **Inverse:** Rule 45 `AddTreasureRoom`

---

### Group 6 — Repair Rules (REPAIRABILITY — no inverse required by paper)

Repair rules are applied unconditionally at the end of each SRD iteration, after all
accepted rewrites. They are never included in the K-candidate evaluation set.

**Repair Rule 1: ResolveOverlap(i, j)**
- **Parameters:** two box indices `i`, `j`
- **Precondition:** boxes `i` and `j` have positive SDF intersection (they overlap)
- **Effect:** separates `i` and `j` by moving each box's centre away from the other by half
  the overlap distance. Specifically: compute overlap vector `d = (cx_j−cx_i, cz_j−cz_i)`,
  overlap depths `ox = hw_i+hw_j − |dx|`, `oz = hd_i+hd_j − |dz|`, push along the axis
  of minimum overlap. Both boxes move by half the required separation.
- **Jump continuity:** ✓ — continuous parameter update; may be large but projects to feasibility.

**Repair Rule 2: RepairContained(i, j)**
- **Parameters:** outer box index `i`; inner box index `j`
- **Precondition:** box `j` is fully contained within box `i`
  (i.e., `cx_j−hw_j >= cx_i−hw_i` and `cx_j+hw_j <= cx_i+hw_i`, same for Z)
- **Effect:** removes box `j` if it has no adjacency connections outside `i`. Otherwise
  extends `j` just beyond `i`'s boundary on its longest axis.
- **Jump continuity:** ε if `j` was small

**Repair Rule 3: AlignWall(i, side, j)**
- **Parameters:** box index `i`; `side ∈ {N,S,E,W}`; reference box index `j`
- **Precondition:** `i` and `j` exist; their walls on the given axis are close but not flush
- **Effect:** snaps `i`'s wall on `side` to be exactly flush with `j`'s corresponding wall.
  e.g. AlignWall(i, E, j): sets `cx_i + hw_i = cx_j − hw_j` (abutting), or
  `cx_i + hw_i = cx_j + hw_j` (co-planar), whichever is closer.
- **Jump continuity:** ✓ — small continuous adjustment

**Repair Rule 4: ClampToBounds(i)**
- **Parameters:** box index `i`
- **Precondition:** any part of box `i` falls outside the layout boundary `[0, layout_w] x [0, layout_h]`
- **Effect:** clamps `cx_i`, `cz_i`, `hw_i`, `hd_i` so the box fits within layout bounds.
  Specifically: if `cx_i − hw_i < 0`, set `cx_i = hw_i`; if `cx_i + hw_i > W`, set `cx_i = W − hw_i`.
  Same for Z. Shrink half-extents if box is too large for the boundary.
- **Jump continuity:** ✓

**Repair Rule 5: EnsureConnected(i)**
- **Parameters:** box index `i`
- **Precondition:** box `i` has zero adjacency entries (isolated node)
- **Effect:** finds the nearest box `j` by Euclidean centre distance and applies AddCorridor(i, j).
  This ensures no box is ever a disconnected island in the graph.
- **Jump continuity:** ε — AddCorridor spawns at SRD_EPSILON

---

**Total built-in rules: 46** (Rules 1–46; repair rules 1–5 are outside the candidate set)

---

## Custom Rule Function Table API

```c
/* How many boxes a rule operates on at once */
typedef struct {
    int   indices[8];
    int   n;
} srd_selection_t;

/*
 * Rule condition predicate.
 * Returns true if the rule can legally fire on the given selection.
 * Must not modify the layout.
 * userdata: caller-supplied context (e.g., game-specific constraints).
 */
typedef bool (*srd_rule_cond_fn)(
    const srd_sdf_layout_t *layout,
    const srd_selection_t  *sel,
    const void             *userdata);

/*
 * Rule apply function.
 * Modifies layout in-place.
 * On success: returns number of new boxes added (0 if no boxes added, e.g. ConvertType).
 * On failure: returns negative, layout must be unchanged.
 * new_box_indices: out-array of indices of any added boxes (cap = cap_new_boxes).
 */
typedef int (*srd_rule_apply_fn)(
    srd_sdf_layout_t      *layout,
    const srd_selection_t *sel,
    const void            *userdata,
    int                   *new_box_indices,
    int                    cap_new_boxes);

/*
 * Rule descriptor registered into the rule table.
 */
typedef struct {
    const char       *name;
    int               inverse_rule_id;   /* index in table; -1 = no inverse (repair only) */
    int               n_select;          /* number of boxes the rule selects (0 = whole layout) */
    float             locality_radius;   /* world units; boxes within this radius are "affected" */
    bool              is_repair;         /* if true: applied unconditionally, not in candidate set */
    bool              jump_continuous;   /* assertion: rendered change < SRD_EPSILON */
    srd_rule_cond_fn  cond;
    srd_rule_apply_fn apply;
    void             *userdata;          /* passed through to cond and apply */
} srd_descent_rule_t;

/* Opaque rule table */
typedef struct srd_rule_table srd_rule_table_t;

/* Allocate a new empty rule table. */
srd_rule_table_t *srd_rule_table_create(fr_allocator_t *alloc);

/*
 * Register a rule. Returns the rule's index in the table, or -1 on failure.
 * In debug builds: asserts that inverse_rule_id (if >= 0) already exists in the table.
 */
int srd_rule_table_register(srd_rule_table_t *tbl,
                             const srd_descent_rule_t *rule);

/* Registers all 46 built-in dungeon layout rules. */
void srd_rule_table_register_builtins(srd_rule_table_t *tbl);

/* Sample n_select boxes from layout satisfying rule's cond. Returns false if no valid selection. */
bool srd_rule_sample_selection(const srd_rule_table_t *tbl,
                                int rule_idx,
                                const srd_sdf_layout_t *layout,
                                srd_selection_t *sel_out,
                                uint32_t *rng_state);

/* Return list of rules applicable to a given layout (excluding repair rules). */
int srd_rule_find_applicable(const srd_rule_table_t *tbl,
                              const srd_sdf_layout_t *layout,
                              int *out_rule_indices,
                              int max_out);
```

---

## Libtorch Swappable Critic

```cpp
/*
 * Abstract base for all critic implementations.
 * The SRD loop holds an ISrdCritic* and never knows which backend is active.
 */
class ISrdCritic {
public:
    virtual ~ISrdCritic() = default;

    /*
     * Score a layout.
     * layout_params: [N, 4] float tensor — (cx, cz, hw, hd) per box, requires_grad = true
     * layout_types:  [N]    int64 tensor — srd_room_type_t per box
     * Returns: scalar float tensor (lower = better). Must be differentiable w.r.t. layout_params.
     */
    virtual torch::Tensor score(torch::Tensor layout_params,
                                torch::Tensor layout_types) = 0;
};

/*
 * AnalyticalCritic: differentiable hand-crafted losses, no .pt file.
 *
 * Loss terms:
 *   - NonPenetration:  sum of soft SDF overlaps via smooth-min (σ = 1.0)
 *   - MinimumSize:     sum of max(min_size − hw, 0)² + max(min_size − hd, 0)² per room type
 *   - TypeSeparation:  penalise boss/treasure rooms adjacent to entrance
 *   - AdjacencyCount:  penalise rooms with wrong number of connections (type-specific targets)
 *   - SoftReachability: differentiable surrogate for graph connectivity (softmax Dijkstra)
 *   - BoundsViolation: penalise boxes outside layout boundary
 */
class AnalyticalCritic : public ISrdCritic {
public:
    struct Config {
        float min_room_size  = 1.0f;
        float min_corridor_w = 0.5f;
        float layout_w       = 20.0f;
        float layout_h       = 20.0f;
        float w_penetration  = 1.0f;
        float w_min_size     = 0.5f;
        float w_separation   = 0.3f;
        float w_adjacency    = 0.2f;
        float w_reachability = 1.0f;
        float w_bounds       = 2.0f;
    };
    explicit AnalyticalCritic(const Config &cfg = {});
    torch::Tensor score(torch::Tensor params, torch::Tensor types) override;
private:
    Config cfg_;
};

/*
 * TorchScriptCritic: loads a compiled .pt model at runtime.
 * The model must accept (params: Tensor[N,4], types: Tensor[N]) and return Tensor[].
 * Swap at startup by passing a different srd_critic_t* — no SRD loop changes required.
 */
class TorchScriptCritic : public ISrdCritic {
public:
    explicit TorchScriptCritic(const char *pt_path);
    torch::Tensor score(torch::Tensor params, torch::Tensor types) override;
private:
    torch::jit::script::Module module_;
};

/* C API (hides C++ from C callers) */
typedef struct srd_critic srd_critic_t;
srd_critic_t *srd_critic_create_analytical(float layout_w, float layout_h);
srd_critic_t *srd_critic_create_torchscript(const char *pt_path);  /* returns NULL on load failure */
void          srd_critic_destroy(srd_critic_t *c);
```

---

## Budget-Driven Configuration

```c
typedef struct {
    /* Time budget — drives all other defaults */
    double   time_budget_s;

    /* Continuous optimiser (L-BFGS via libtorch) */
    int      lbfgs_max_iter;              /* iterations per continuous phase */
    int      lbfgs_history_size;          /* quasi-Newton history; default 10 */
    float    lbfgs_tolerance_grad;        /* convergence threshold; default 1e-5 */
    float    lbfgs_tolerance_change;      /* loss change threshold; default 1e-9 */

    /* Discrete rewrite phase */
    int      k_candidates;               /* number of candidate rules to evaluate */
    int      continuous_steps_per_rewrite;  /* L-BFGS rounds between discrete steps */
    int      local_optimize_steps;       /* L-BFGS steps in candidate LocalOptimize */

    /* Temperature annealing */
    float    temperature_init;
    float    temperature_decay;          /* multiplied each outer iteration */
    float    temperature_min;

    /* Rule table and critic (caller owns both) */
    srd_rule_table_t *rules;
    srd_critic_t     *critic;
} srd_descent_config_t;

/*
 * Fills cfg with budget-appropriate defaults. Caller must set rules and critic.
 */
void srd_descent_config_from_budget(srd_descent_config_t *cfg, double budget_s);
```

| Budget | K candidates | L-BFGS iters/phase | LocalOpt steps |
|---|---|---|---|
| < 2s | 16 | 20 | 3 |
| 2–10s | 64 | 100 | 10 |
| 10–60s | 256 | 500 | 25 |
| > 60s | 512 | until convergence | 50 |

---

## Correct SRD Loop (Paper Algorithm 2)

```
(s, p) ← init_from_ascii(ascii)             // srd_sdf_layout_from_grid
params  ← torch::tensor(p, requires_grad=true)
optimizer ← LBFGS(params, lr=1.0, max_iter=lbfgs_max_iter, history_size=lbfgs_history_size)

loop until time_budget exceeded:

  // ── Continuous phase (L-BFGS) ──────────────────────────────────────
  for _ in 0..continuous_steps_per_rewrite:
    closure = lambda:
      optimizer.zero_grad()
      loss = critic.score(params, types)
      loss.backward()
      return loss
    optimizer.step(closure)

  // ── Discrete phase ──────────────────────────────────────────────────
  current_loss = critic.score(params.detach(), types).item()

  // Sample K valid rule/selection pairs (uniform over applicable rules, exclude repair)
  candidates = []
  for _ in 0..k_candidates:
    rule_idx = sample_uniform(applicable_rules(tbl, s))
    sel      = srd_rule_sample_selection(tbl, rule_idx, s)
    s_prime  = copy(s)
    new_boxes = rule.apply(s_prime, sel)         // modifies s_prime in-place
    p_prime  = copy(p); p_prime[new_boxes] = SRD_EPSILON
    params_prime = torch::tensor(p_prime, requires_grad=true)

    // LocalOptimize: one L-BFGS mini-run under proposed structure
    local_opt = LBFGS(params_prime, max_iter=local_optimize_steps)
    for _ in 0..local_optimize_steps:
      local_opt.step(lambda: critic.score(params_prime, s_prime.types).backward())

    delta_L = current_loss - critic.score(params_prime.detach(), s_prime.types).item()
    candidates.append({rule_idx, sel, s_prime, params_prime.detach(), delta_L})

  // Build compatibility graph and greedy max-cover
  // Two candidates are compatible iff their affected box sets (within locality_radius) don't overlap
  positive  = [c for c in candidates if c.delta_L > 0]
  selected  = greedy_max_cover(positive)         // maximise total delta_L, no conflicting pairs

  // Apply all selected rewrites to real state
  for c in selected:
    s, p = c.s_prime, c.params_prime

  // Apply repair rules unconditionally (do NOT add to candidate set)
  for each repair rule r applicable to s:
    r.apply(s, p)

  temperature *= temperature_decay
  temperature  = max(temperature, temperature_min)
```

---

## File Plan

### New Files

| File | Purpose |
|---|---|
| `include/ferrum/procgen/srd/srd_sdf_layout.h` | `srd_sdf_box_t`, `srd_sdf_layout_t`, `srd_room_type_t`, public API |
| `src/procgen/srd/srd_sdf_layout.c` | `srd_sdf_layout_init`, `srd_sdf_layout_from_grid`, box ops, adjacency |
| `src/procgen/srd/srd_sdf_layout_ops.c` | `srd_layout_rasterize`, smooth-min union, box SDF eval |
| `include/ferrum/procgen/srd/srd_descent_rules.h` | `srd_descent_rule_t`, `srd_rule_table_t`, `srd_selection_t`, full table API |
| `src/procgen/srd/srd_descent_rules.c` | Table alloc, register, `register_builtins`, sample, find_applicable |
| `src/procgen/srd/srd_rules_room.c` | Rules 1–16: SplitH/V, Merge, AddN/S/E/W, Remove, Trim, Extend, Scale, Alcove, Antechamber, ConvertType |
| `src/procgen/srd/srd_rules_corridor.c` | Rules 17–30: AddCorridor, Remove, Widen, Narrow, Bend, Straighten, SplitCorridor, MergeCorridor, Bridge, AddLoop, RemoveLoop, Shortcut, RemoveShortcut, Reroute |
| `src/procgen/srd/srd_rules_feature.c` | Rules 31–46: Doors, Stairs, DeadEnd, SecretRoom, BossRoom, TreasureRoom |
| `src/procgen/srd/srd_rules_repair.c` | Repair Rules 1–5: ResolveOverlap, RepairContained, AlignWall, ClampToBounds, EnsureConnected |
| `include/ferrum/procgen/srd/srd_critic.h` | `ISrdCritic`, `AnalyticalCritic`, `TorchScriptCritic`, C API |
| `src/procgen/srd/srd_critic.cpp` | Both critic implementations |
| `src/procgen/srd/srd_descent_loop.cpp` | Correct SRD loop: L-BFGS + K-candidate + LocalOptimize + max-cover |
| `src/procgen/srd/srd_descent_config.c` | `srd_descent_config_from_budget` + budget table |
| `tests/srd_sdf_layout_tests.c` | Layout init, box ops, adjacency, rasterizer, gradient |
| `tests/srd_descent_rules_tests.c` | Each rule: apply, cond, inverse round-trip, jump-continuity bound |
| `tests/srd_critic_tests.cpp` | Analytical critic scores, gradient flow, torchscript load/score |
| `tests/srd_descent_loop_tests.cpp` | Integration: ASCII→SDF→optimise, verify loss decreases |

### Modified Files

| File | Change |
|---|---|
| `src/procgen/srd/srd_bridge.cpp` | Wire ASCII→SDF init; call new loop + critic; remove all SymX |
| `src/procgen/srd/srd_grammar.c` | Keep only `srd_grid_parse` + flood-fill + adjacency; strip rule table |

### Removed Files

| File | Reason |
|---|---|
| `src/procgen/srd/srd_loop.cpp` | Replaced by `srd_descent_loop.cpp` |
| `src/procgen/srd/srd_energy.cpp` | SymX-based, superseded by libtorch autograd |
| `src/procgen/srd/srd_optimizer.cpp` | SymX-based, dead |
| `src/procgen/srd/srd_loss_gradient.cpp` | Superseded by autograd |
| `src/procgen/srd/srd_eikonal.cpp` | Superseded by differentiable critic |
| `src/procgen/srd/srd_transport.cpp` | Superseded by differentiable critic |
| `src/procgen/srd/srd_loss_primitives.cpp` | Superseded by critic |
| `src/procgen/srd/srd_loss_compiler.cpp` | Superseded by critic |

---

## Key Design Invariants

1. **Jump continuity is enforced structurally:** every `Add*` and `Spawn*` rule initialises
   new boxes with `hw = hd = SRD_EPSILON` (0.01 world units). Continuous optimisation grows
   them. Full-size spawning is forbidden.

2. **Every non-repair rule has a registered inverse:** `srd_rule_table_register` asserts this
   in debug builds. Repair rules (is_repair=true) are exempt.

3. **The compatibility graph is based on affected box sets:** two candidate rewrites are
   compatible iff the union of boxes within `locality_radius` of their respective selections
   does not overlap. Computed lazily per candidate pair during max-cover.

4. **The critic is the single source of loss:** no separate loss terms outside the critic.
   `AnalyticalCritic` encodes all geometric constraints. Swapping to `TorchScriptCritic`
   replaces all of them with the learned model — the SRD loop does not change.

5. **No SymX anywhere:** all differentiation goes through libtorch autograd. The build must
   not link against SymX after this work is complete.

6. **All parameters live in libtorch tensors during optimisation:** the `srd_sdf_layout_t`
   is the discrete structural state (which boxes exist, adjacency). The continuous
   `(cx, cz, hw, hd)` values are extracted into a `torch::Tensor` for L-BFGS and written
   back to the layout after each outer iteration.
