# SRD-Based Procedural Dungeon Generation — Redesign

## Overview

Replace the current token-string grammar (`ROOM_QUAD`, `CORRIDOR_H`, etc.) with:

1. **VLM generates ASCII floor plans** (one grid per level)
2. **VLM composes a loss function** from a library of differentiable primitive loss
   terms, expressed as field equations (eikonal, gradient transport) evaluated at
   anchor points and propagated through the voxel grid via PDE coupling rules
3. **ASCII parser** extracts a room graph with adjacency constraints
4. **SymX-based SRD optimizer** (Stochastic Rewrite Descent) expands the graph
   into detailed geometry via a recursive shape grammar with context-sensitive
   rewrite rules, optimizing continuous parameters (positions, sizes, heights)
   against the VLM-provided loss, using symbolic differentiation + Newton's method
5. **Existing SVO/mesh pipeline** consumes the optimized geometry unchanged

**Key insight**: The LLM does not directly control geometry. It selects which
loss terms matter from a fixed library of differentiable primitives. SRD then
finds the grammar expansion that minimizes that loss. The loss terms behave
like Poisson equations: evaluated at specific anchor points, then diffused
through the rest of the grid using PDE coupling rules.

---

## 1. Voxel & World Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Voxel size | **0.25m** | Finer detail: 128m world / 512 cells |
| World extent | 64m (per axis) | World bounds [-64, +64], span = 128m |
| Max SVO depth | 9 | 2⁹ = 512 cells → 128m / 512 = 0.25m/voxel |
| Room min size | 6m (24 voxels) | Enough for walls + interior |
| Corridor min width | 2m (8 voxels) | Walkable after 0.25m walls |
| ASCII cell ≈ | Variable, optimized by SRD | Initial guess ~2–4m |

---

## 2. Data Flow

```
 ┌──────────┐
 │ User     │  "Build a dungeon where the treasure is far from the
 │ Prompt   │   entrance, but visible from the guard room"
 └────┬─────┘
      │
      ▼
 ┌──────────────────────────────────────────────────────────────┐
 │  VLM (Architect)                                              │
 │                                                               │
 │  1. Generates ASCII floor plan (one grid per level)           │
 │  2. Composes loss function from primitive library:            │
 │       PathDistance(entrance, treasure) > 30m                 │
 │     + LineOfSight(guard_room, treasure)                      │
 │     + NonPenetration(all)                                    │
 │     + MinimumSize(bar_rooms, 6m)                             │
 └──────────┬──────────────────────────┬────────────────────────┘
            │ ASCII grid               │ Loss expression
            ▼                          ▼
 ┌──────────────────┐    ┌──────────────────────────────────────┐
 │  ASCII Parser    │    │  Loss Compiler                        │
 │  (C)             │    │  (SymX)                               │
 │                  │    │  Parses loss term tree →              │
 │  RoomGraph:      │    │  compiles per-element energy          │
 │   nodes: rooms   │    │  functions + propagation rules        │
 │   edges: adjac.  │    │                                       │
 │   stairs: ^ v    │    │  E = Σ w_i · L_i(geometry)            │
 └────────┬─────────┘    └──────────────┬───────────────────────┘
          │                             │
          ▼                             ▼
 ┌─────────────────────────────────────────────────────────────────┐
 │  SRD Optimizer (SymX)                                           │
 │                                                                 │
 │  Input: RoomGraph + user-defined Energy(geometry)               │
 │                                                                 │
 │  Loop until time budget exhausted:                              │
 │    ├─ SymX assemble gradient + Hessian from elements            │
 │    ├─ Newton step on continuous parameters                     │
 │    ├─ Every P steps: grammar proposes rewrites                  │
 │    │   → evaluate each against user's loss                      │
 │    │   → accept rewrites that improve the loss                  │
 │    └─ Timer check                                               │
 └─────────────────────────────────────────────────────────────────┘
                                                  │
                                                  ▼
 ┌───────────────┐    ┌──────────────┐    ┌───────────────┐
 │  SVO Builder  │ ──→│  Chunk Mesh  │ ──→│  Renderable   │
 │  (existing C) │    │  (existing C) │    │  Mesh         │
 └───────────────┘    └──────────────┘    └───────────────┘
```

---

## 3. ASCII Floor Plan Format

### 3.1 Format Specification

The VLM emits one ASCII block per floor level, delimited by headers:

```
=== FLOOR 0: GROUND FLOOR ===
W W W W W W W W W W
W B B B B R R R ^ W
W B B B B R R R . W
W R R R R R R R . W
W W W W G W W W W W

=== FLOOR 1: UPPER FLOOR ===
W W W W W W W W W W
W P P P P P P P v W
W P P P P P P P . W
W P P P P P P P . W
W W W W W W W W W W
```

### 3.2 Character Legend

| Char | Meaning | Expands to... |
|------|---------|---------------|
| `W`  | Outer wall / boundary | World perimeter constraint (not a room) |
| `B`  | Bar area | BarRoom (1–2 doors, bar geometry optional) |
| `R`  | Common room | CommonRoom (1–4 doors depending on adjacency) |
| `P`  | Private bedroom | PrivateRoom (1 door, smallest footprint) |
| `G`  | Entrance | EntranceRoom (1 door to outside) |
| `^`  | Stairs up | StairAnchor(up, position) |
| `v`  | Stairs down | StairAnchor(down, position) |
| `.`  | Open floor / passage | FloorSpace (connective, no walls) |
| *(future)* | | Expandable: `T`=treasury, `K`=kitchen, etc. |

### 3.3 Parsing Rules

- **Flood-fill**: Contiguous same-char cells = one room node
- **Adjacency**: Cells sharing an edge with different chars → connection edge
  - Two `R` cells adjacent to two `B` cells → one `R ⇄ B` edge, not four
- **Stair anchors**: `^` / `v` position becomes a `StairAnchor` node linked to
  its containing room
- **`W` boundary**: Not itself a room; constrains rooms to stay inside
- **`.` floor**: Merged into adjacent room regions as interior space

### 3.4 Initial Room Sizing

Each room is initialized with:
- `center_xy` = centroid of its cell region
- `half_extent` = proportional to `cell_count × cell_scale` + growth margin
- `cell_scale` is a learnable parameter optimized by SRD
- Growth margin = ~1–2 cells around the region, allocated for SRD expansion

---

## 4. Shape Grammar (Recursive, Context-Sensitive)

### 4.1 Element Types (SymX energy elements)

Each non-terminal expands to one or more SymX energy elements:

```
RoomElement(center_xy, half_extent_xy, floor_z, ceil_z, material, type)
CorridorElement(from_xy, to_xy, width, floor_z, ceil_z)
StairElement(anchor_xy, direction, n_steps, step_h, step_d, floor_from, floor_to)
```

### 4.2 Rewrite Rules

#### Room Structure Rules

| Rule | Variants | Context Condition |
|------|----------|-------------------|
| **SplitRoom**(r, axis, frac) | axis∈{X,Z}, frac∈{½,⅓,¼} | r half-extent on axis > split_threshold |
| **MergeRooms**(r1, r2, mode) | mode∈{FullMerge, DoorOnly(1m)} | adjacent, combined size < max |
| **ResizeRoom**(r, axis, delta) | axis∈{X,Z,Y,Both}, δ∈{+1,+2,−1 cell} | within bounds, no overlap |
| **AddRoom**(type, pos, size) | type∈{any char}, size∈{S,M,L} | empty space ≥ min_room exists |
| **RemoveRoom**(r) | — | r disconnected or area < min |

#### Connection / Corridor Rules

| Rule | Variants | Context Condition |
|------|----------|-------------------|
| **AddConnection**(r1, r2, w) | width∈{2m, 3m, 4m} | adjacent in graph, not yet connected |
| **RemoveConnection**(c) | — | rendered redundant by merge or overlap |
| **RerouteConnection**(c, wp) | wp ∈ {N,S,E,W} offsets | c overlaps another room/corridor |
| **WidenConnection**(c, δ) | δ ∈ {+1m, +2m} | corridor connects ≥3 rooms |

#### Stair Rules

| Rule | Variants | Context Condition |
|------|----------|-------------------|
| **ExpandStair**(a, dir, n, h, d) | dir∈{N,S,E,W}, n=ceil(H/h) | step_h∈[0.25,0.5], step_d∈[0.5,1.5] |
| **AlignFloors**(f1, f2, Δx, Δz) | Δx,Δz ∈ ℝ (learned) | matching stair anchors on f1,f2 |

#### Wall / Opening Rules

| Rule | Variants | Context Condition |
|------|----------|-------------------|
| **InsertOpening**(seg, pos, w) | width∈{1m, 2m} | wall between two connected rooms |
| **RemoveOpening**(o) | — | rooms merged |

#### Floor Layout Rules

| Rule | Variants | Context Condition |
|------|----------|-------------------|
| **ShiftFloor**(f, dx, dz) | dx,dz ∈ ℝ (learned) | stair alignment loss > ε |
| **ResizeFloor**(f, min, max) | any direction | rooms near edge, need space |

### 4.3 Context-Sensitivity Example

```
Room R at grid position (2,2), neighbors:
  N: B  S: R  E: R  W: W
  ↓
  - 1 connection edge to B (north)
  - 2 adjacency edges (south, east = internal)
  - 1 boundary edge (west = wall)
  ↓
  Proposed expansions:
  - Door on north edge (connection to B)
  - No door on south/east (same room type, merged cell)
  - Solid wall on west (boundary)
```

---

## 5. Loss Function Primitive Library

The VLM does not emit arbitrary loss code. It selects and composes from a fixed
library of differentiable primitive loss terms. Each primitive is a SymX energy
element that is differentiable w.r.t. the voxel grid occupancy (and transitively
w.r.t. room/corridor/stair parameters via the SDF chain).

The VLM's output includes both the ASCII floor plan AND a loss expression string:

```
LOSS:
  PathDistance(entrance, treasure) > 30
  LineOfSight(guard_room, treasure)
  NonPenetration(all)
  MinimumSize(bar_rooms, 6)
  AdjacencyCount(treasure, 1)
```

### 5.1 Primitive Catalog

Each primitive defines:
- **Anchor semantics**: which labeled rooms/markers it references
- **Field coupling rule**: how the loss evaluated at anchors propagates
- **Differentiable implementation**: how SymX computes energy + gradient

#### 5.1.1 `PathDistance(from, to) [<|>|=] [d]`

Shortest traversable path from room `from` to room `to` through the empty-space
SDF. Implemented via the **continuous eikonal equation**.

```
Loss:  max(0, d_target - T(to))   for "> d"
       max(0, T(to) - d_target)   for "< d"
       |T(to) - d_target|          for "= d"

where T(x) satisfies |∇T| = 1/v(x) with T(from) = 0
  v(x) = 1 where empty, 0 where solid (occupancy)
```

No navigation graph needed — the eikonal equation propagates naturally through
open voxels and around obstacles. The resulting `T` field is a shortest-path
distance map from the source room. The gradient `∇T` points along the shortest
path, providing gradient information to SRD about *how* to improve the path.

#### 5.1.2 `LineOfSight(from, to)`

Whether room `to` is visible from room `from`. Implemented via **anisotropic
gradient transport**.

```
R(x) satisfies:
  ∇·(a(x) ∇R) = 0    in domain
  R = 1               on ∂Ω_from (source room surface)
  R = 0               on ∂Ω_world (boundary absorption)

where a(x) is the anisotropic diffusion tensor:
  a(x) = ε·I + (1-ε)·occ(x)·d̂d̂ᵀ      where d̂ = direction from → to
  (diffusion is weak perpendicular to line-of-sight, strong along it)
```

High values of R at `to` indicate clear line-of-sight. The energy is
`(1 - R(to))²`, minimized when R(to) → 1.

This naturally handles partial occlusion: if a wall blocks 60% of the line,
R(to) ≈ 0.4, producing gradient that pushes the obstructing geometry aside.

#### 5.1.3 `NonPenetration(rooms...)`

Rooms must not interpenetrate (corridors may overlap rooms).

```
Loss = Σ_{i≠j} ∫ max(occ_i(x) · occ_j(x), 0) dx
```

Product of occupancy fields; nonzero only where both rooms occupy the same voxel.

#### 5.1.4 `MinimumSize(rooms..., min_m)`

Room half-extent must exceed a minimum on each axis.

```
Loss = Σ max(min_extent - room.half_extent, 0)²
```

#### 5.1.5 `Separation(type_A, type_B) [<|>|=] [d]`

Rooms of different types must be a certain Euclidean distance apart. Useful for
separating noisy areas (bar) from quiet areas (private rooms).

```
Loss = max(0, d - dist(center_A, center_B))   for "> d"
```

#### 5.1.6 `Containment(room, region)`

Room must stay within a specified region (floor boundary, or other room).

```
Loss = ∫ max(occ_room(x) - occ_region(x), 0) dx
```

#### 5.1.7 `AdjacencyCount(room, n)`

A room must have exactly `n` connections (doors/corridors).

```
Loss = |actual_connections(room) - n|²
```

#### 5.1.8 `HeightSpan(room, min_y, max_y)`

Room must have at least `min_y` floor-to-ceiling height.

```
Loss = max(min_y - (ceil_z - floor_z), 0)²
```

#### 5.1.9 `StairAlignment(anchor_from, anchor_to)`

Matching stair markers on adjacent floors must align in XZ.

```
Loss = |anchor_from.xy - anchor_to.xy|²
```

#### 5.1.10 `FloorAccessibility(floor)`

Every room on a floor must be reachable from the floor entrance. Equivalent to:

```
Loss = Σ_{room∈floor} PathDistance(entrance, room) == NaN  // unreachable → high penalty
```

### 5.2 VLM Loss Composition

The VLM emits loss expressions as a DSL:

```
LOSS:
  PathDistance(entrance, treasure) > 30
  LineOfSight(guard_room, treasure)
  NonPenetration(all)
  MinimumSize(all_rooms, 6)
```

The Loss Compiler parses this into a tree of primitive loss terms, each
parameterized by room labels. During SRD optimization, the total energy is:

```
E_total(geometry) = Σ w_i · L_i(geometry)
```

where `L_i` are the instantiated loss primitives and `w_i` are default weights
(modifiable: `PathDistance(...) > 30 weight=2.0` for emphasis).

### 5.3 Room Label Resolution

Room labels in loss expressions refer to:
- **Named rooms**: `entrance`, `treasure`, `guard_room` — matched to the ASCII
  character type in the floor plan (e.g., `G` → entrance, `R` → guard_room when
  specified by the VLM with annotations)
- **Type groups**: `bar_rooms`, `all_rooms` — aggregate all rooms of a given type
- **Special**: `all` — all geometry elements; `entrance` — the room marked `G`

The VLM annotates ASCII cells with optional labels in comments:

```
W W W W W W W W W W
W B B B B R R R ^ W  <-- B=bar_area, R=common
W B B B B R R R . W
W R R R R R R R . W  <-- R=treasure at [x=4, y=3]
W W W W G W W W W W  <-- G=entrance
```

---

## 6. PDE Propagation Rules

Loss primitives that depend on global properties (path existence, line-of-sight,
accessibility) must propagate information through the grid. We use two PDE
coupling rules, both differentiable.

### 6.1 Continuous Eikonal Equation (Shortest-Path)

Used by: `PathDistance`, `FloorAccessibility`

```
|∇T(x)| = 1 / v(x)       in domain Ω
T(x) = 0                 on anchor set A (source room surface)

where v(x) = clamp(1 - occ(x), ε, 1):
  v = 1  in empty space (fast travel)
  v = ε  inside solid (near-zero speed, effectively blocked)
```

**Discretization** (finite differences on voxel grid):

For each voxel `(i,j,k)`, the eikonal equation couples it to its 6 neighbors:

```
max(T_{i,j,k} - T_{i\pm1,j,k}, 0)² +
max(T_{i,j,k} - T_{i,j\pm1,k}, 0)² +
max(T_{i,j,k} - T_{i,j,k\pm1}, 0)² = h² / v_{i,j,k}²
```

where `h = voxel_size`. This is a Godunov upwind scheme, solved via fast
sweeping or fast marching within a fixed number of iterations.

**Differentiability**: The eikonal update `T_new = min_neighbor(T + h·v)` is
a composition of min, add, and multiply — all differentiable. Backpropagation
through the eikonal solve propagates gradients from `T(to)` back to changes in
the voxel occupancy `occ(x)` that would shorten the path.

**SymX implementation**: Each voxel in the grid is an element that couples
to its neighbors. SymX computes the symbolic gradient of `T(to)` w.r.t.
`v(x)` at every voxel, which chains back to `occ(x)` → SDF → room parameters.

### 6.2 Anisotropic Gradient Transport (Line-of-Sight)

Used by: `LineOfSight`

Solve the steady-state anisotropic diffusion equation:

```
∇·(a(x) ∇R(x)) = 0      in domain Ω
R(x) = 1                 on ∂Ω_source  (emitter surface)
R(x) = 0                 on ∂Ω_boundary (absorbing boundary)

where a(x) = ε·I + (1-ε)·n̂n̂ᵀ + occ(x)·δ·n̂⊥n̂⊥ᵀ
  n̂ = unit vector from source to target
  ε = small isotropic background (0.001)
  δ = large obstacle penalty (1000)
  occ(x) = voxel occupancy (0=empty, 1=solid)
```

The tensor `a(x)` has two regimes:
- **In empty space** (`occ` ≈ 0): `a ≈ n̂n̂ᵀ` — diffusion moves almost
  exclusively along the line-of-sight direction
- **In solid** (`occ` ≈ 1): `a ≈ δ·n̂⊥n̂⊥ᵀ` — strong cross-direction
  resistance, blocking transport

The result: R(x) is high along clear sightlines from source to target, and
drops sharply when walls intervene.

**Discretization** (finite volume on voxel grid):

```
For voxel (i,j,k) with neighbor faces f ∈ {±x, ±y, ±z}:
  Σ_f a_f · (R_neighbor - R_ijk) / h² = 0

where a_f is the face-averaged diffusion tensor projected onto the
face normal direction n_f:
  a_f = n_fᵀ · a(x_face) · n_f
```

This produces a sparse linear system `A·R = b`, where A is a 7-point stencil
(center + 6 neighbors). Since the system is symmetric positive-definite, we
solve with a few Jacobi or conjugate-gradient iterations (10-20 suffices for
gradient propagation).

**Differentiability**: `R` is the solution of `A(occ)·R = b`. The gradient of
`R(to)` w.r.t. `occ(x)` is computed via the adjoint method:

```
∂R(to)/∂occ(x) = -λᵀ · (∂A/∂occ(x)) · R

where Aᵀ·λ = e_to   (adjoint solve, same structure as forward solve)
```

SymX can symbolically differentiate through the iterative solver steps,
or we can implement the adjoint manually for efficiency.

### 6.3 Sampling Strategy

PDE solves operate on a **downsampled grid** (e.g., coarsened by 2× or 4× in
each dimension) during SRD optimization, then the final geometry is rasterized
at full resolution (0.25m/voxel) via the existing SVO builder.

| Phase | Grid resolution | Voxel size | Purpose |
|-------|----------------|------------|---------|
| SRD continuous opt | 128³ (or 64³) | 1.0m (or 2.0m) | Fast Newton steps |
| SRD rewrite eval | 128³ | 1.0m | Proposal acceptability |
| Final rasterization | 512³ | 0.25m | SVO → mesh (existing pipeline) |

### 6.4 Coupling Summary

```
Loss Term          Anchor Type     PDE Coupling          Propagation Field
──────────────────────────────────────────────────────────────────────────
PathDistance       room→room       Eikonal |∇T|=1/v      T(x) = travel time
LineOfSight        room→room       Transport ∇·(a∇R)=0   R(x) = visibility
FloorAccessibility entrance→rooms  Eikonal (multi-src)   T(x) per room
NonPenetration     room×room       (none)                 occ(x) direct
MinimumSize        room            (none)                 parameter direct
Separation         room×room       (none)                 center dist direct
Containment        room×region     (none)                 occ overlap direct
AdjacencyCount     room            (none)                 graph count direct
HeightSpan         room            (none)                 parameter direct
StairAlignment     stair×stair     (none)                 parameter direct
```

---

## 7. SymX Energy Formulation

### 7.1 SDF-Based Voxel Occupancy

Each geometry element contributes a signed distance function. Occupancy is a
soft (sigmoid) falloff, differentiable w.r.t. element parameters.

```
Room SDF:
  sdf(x,z,y) = max(|x-cx|-hx, |z-cz|-hz, |y-cy|-hy, 0)   // axis-aligned box
  occ(x,z,y) = sigmoid(-sdf / temperature)

Corridor SDF:
  t  = clamp(dot(xz - from, to-from) / length², 0, 1)
  cxz = lerp(from, to, t)
  d_2d = |xz - cxz| - radius
  d_y  = max(floor_y - y, y - ceil_y)
  sdf = max(d_2d, d_y, 0)
  occ = sigmoid(-sdf / temperature)

Stair Step SDF:
  sdf = box(pos_step, half_extent)   // one box per step
  occ = sigmoid(-sdf / temperature)

Composite scene:
  scene_occ = 1 - ∏(1 - element_occ)   // differentiable union
```

Temperature starts high (soft occupancy) and anneals to low (sharp/hard) during
optimization, allowing gradients to flow through early and hardening for the
final geometry.

### 7.2 VLM-Composed Energy

The total energy is not fixed — it is assembled from the VLM's loss expression:

```cpp
// LLM emits: "PathDistance(entrance, treasure) > 30 + LineOfSight(guard, treasure)"
// Loss Compiler builds:
Scalar E = 0.0;

// PathDistance term (eikonal)
E += w_path * path_distance_energy(room_entrance, room_treasure, target=30, op=GREATER);

// LineOfSight term (anisotropic transport)
E += w_los * line_of_sight_energy(room_guard, room_treasure);

// Always-included hard constraints
E += w_overlap * non_penetration_energy(all_rooms);
E += w_bounds * bounds_energy(all_rooms, world_limits);
E += w_size  * minimum_size_energy(all_rooms, 6.0);

// SymX differentiates E w.r.t. all room/corridor/stair parameters
```

### 7.3 Per-Element Energy Definitions (SymX C++)

```cpp
// Eikonal-based path distance energy
Scalar path_distance_energy(Element room_a, Element room_b, Scalar d_target, Operator op) {
    // 1. Set boundary conditions
    T[anchor_points(room_a)] = 0;  // source

    // 2. Solve eikonal on coarse grid (differentiable)
    for (int sweep = 0; sweep < MAX_SWEEPS; sweep++) {
        for (auto& voxel : grid) {
            Scalar v = clamp(1.0 - scene_occ(voxel), EPS, 1.0);
            Scalar t_new = min(T[neighbors] + h / v);  // Godunov scheme
            T[voxel] = min(T[voxel], t_new);
        }
    }

    // 3. Evaluate loss at target
    Scalar t_target = interpolate(T, center(room_b));
    if (op == GREATER) return pow(max(d_target - t_target, 0.0), 2);
    if (op == LESS)    return pow(max(t_target - d_target, 0.0), 2);
    if (op == EQUAL)   return pow(t_target - d_target, 2);
}

// Anisotropic transport for line-of-sight
Scalar line_of_sight_energy(Element room_a, Element room_b) {
    Vector direction = (center(room_b) - center(room_a)).normalized();

    // Build diffusion tensor field
    for (auto& voxel : grid) {
        Scalar occ = scene_occ(voxel);
        a[voxel] = EPS * I + (1-EPS) * outer(direction, direction);
        if (occ > 0.5)
            a[voxel] += DELTA * (I - outer(direction, direction));  // block perpendicular
    }

    // Set boundary: R=1 on room_a surface, R=0 on domain boundary
    set_boundary(R, room_a, 1.0);
    set_boundary(R, domain_boundary, 0.0);

    // Solve A·R = b with conjugate gradient (few iterations)
    for (int iter = 0; iter < CG_ITERS; iter++)
        conjugate_gradient_step(A, R, b);

    // Loss: R should be high at room_b
    Scalar R_target = interpolate(R, center(room_b));
    return pow(1.0 - R_target, 2);
}

// Direct (non-PDE) primitives
Scalar non_penetration_energy(vector<Element> rooms) { /* ... */ }
Scalar minimum_size_energy(vector<Element> rooms, Scalar min_m) { /* ... */ }
// ... etc.
```

### 7.4 Gradient Chain

The gradient flows from loss → field solution → occupancy → SDF → geometry:

```
∂E/∂room.pos = (∂E/∂T · ∂T/∂occ · ∂occ/∂sdf · ∂sdf/∂pos)
                 ~~~~~~~~~~~~~~~   ~~~~~~~~~~~~~~~~~~~~~~~~~
                   eikonal solve     SDF chain (analytic)
```

SymX's symbolic differentiation handles the PDE solve by differentiating
through the iterative updates (fast sweeping / CG steps). This produces
exact symbolic gradients for the entire chain.

---

## 8. SRD Optimization Loop

### 8.1 Algorithm

```
function SRD_optimize(graph, loss_expression, time_budget):
    elements = graph_to_elements(graph)         // rooms, connections, stairs
    E = loss_compiler.build(loss_expression)     // LLM-composed energy (Section 5)
    newton = NewtonSolver(E, elements.parameters())
    best_elements = elements
    best_energy = ∞

    t0 = now()
    step = 0
    while (now() - t0) < time_budget:
        // Continuous optimization with LLM's loss
        newton.step(elements.parameters())
        elements.project_to_valid()             // clamp sizes, bounds

        // Structural rewrites every P steps
        if step % REWRITE_INTERVAL == 0:
            proposals = grammar.propose_rewrites(elements, graph)
            for each proposal in shuffle(proposals):
                candidate = proposal.apply(elements)
                // Few Newton steps to relax
                newton.step(candidate.parameters(), n_steps=3)
                candidate_energy = E.evaluate(candidate)
                if candidate_energy < best_energy * (1 - ε):
                    elements = candidate
                    best_energy = candidate_energy
                    best_elements = elements
                    E.recompile(elements)  // SymX regenerates expressions
                                           // for new element set
                    newton.reset()

        step++
        if annealing_schedule(step):
            temperature *= 0.95

    return best_elements
```

**Key addition**: The loss function `E` is not hardcoded — it comes from the
VLM's `LOSS:` block, compiled into SymX energy elements. Different VLM prompts
produce different loss landscapes, causing SRD to find different grammar
expansions optimized for different goals.

### 8.2 Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `time_budget` | 10.0s | Max wall clock seconds |
| `REWRITE_INTERVAL` | 50 | Steps between rewrite proposals |
| `temperature_init` | 0.5 | Starting softness |
| `temperature_min` | 0.01 | Minimum (sharp) temperature |
| `temperature_decay` | 0.995 | Per-step decay |
| `split_threshold` | 8m | Half-extent above which split is proposed |
| `min_room_half` | 2m | Below which room is candidate for removal/merge |
| `newton_max_iters` | 3 | Max Newton iterations per step |
| `newton_proposal_iters` | 2 | Newton iters for proposal evaluation |
| `proposal_accept_threshold` | 0.99 | Must reduce energy by ≥1% |
| `floor_cell_scale_init` | 3.0m | Initial ASCII cell → world meters |

---

## 9. Source File Layout

### 9.1 New Files

```
include/ferrum/procgen/
  procgen_srd_types.h           — RoomBox, CorridorSeg, StairDef, FloorDef,
                                   RoomGraph, StringPool structs
  procgen_ascii_parse.h         — Parse ASCII → RoomGraph
  procgen_srd_grammar.h         — Rewrite rule signatures
  procgen_loss_primitives.h     — Loss primitive type enum + signatures

src/procgen/
  procgen_ascii_parse.c         — Grid parser (flood-fill, adjacency)
  procgen_srd_grammar.c         — Rule context checks, proposal generation
  procgen_srd_bridge.cpp        — C↔SymX interface entry point

src/procgen/srd/
  srd_energy.cpp                — Fixed hard-constraint energy elements
  srd_loss_primitives.cpp       — 10 primitive loss functions (Section 5)
  srd_loss_compiler.cpp         — Parse LOSS: block → energy expression tree
  srd_eikonal.cpp               — Differentiable eikonal solver (Section 6.1)
  srd_transport.cpp             — Differentiable anisotropic transport (Section 6.2)
  srd_optimizer.cpp             — SRD main loop (Newton + rewrites)
  srd_rewrite.cpp               — Apply rewrite specs to element sets
  srd_sampler.cpp               — Sample-point generation (SDF eval)
  srd_anneal.cpp                — Temperature schedule
```

### 9.2 Modified Files

```
src/procgen/procgen_svo_builder.c   — Accept RoomBox[] output from SRD instead
                                      of fr_dungeon_layout_t token layout
src/procgen/architect/architect.c   — Emit ASCII grids, not token strings

CMakeLists.txt                      — Add extern/SymX submodule, srd/ targets
AGENTS.md                           — Document new pipeline
scripts/auto_play.sh                — Feed .asc grid files instead of .txt tokens
```

### 9.3 Deleted Files

```
src/procgen/procgen_tokenize.c              — Replaced by ASCII parser
src/procgen/grammars/grammar_blockout.c     — SRD grammar replaces this
src/procgen/procgen_grammar_registry.c      — No registry needed
src/procgen/chunk/*                         — Can be removed (duplicate path)
tests/procgen/procgen_tokenize_tests.c
tests/procgen/procgen_grammar_blockout_tests.c
datasets/dpo_txt/*.txt                      — Replaced by .asc grid files
```

### 9.4 Preserved Files

```
src/procgen/procgen_svo_builder.c           — Heavily modified, kept
src/procgen/procgen_chunk_mesh.c            — Unchanged
src/procgen/procgen_chunk_builder.c         — Unchanged
src/npc/nav/npc_svo_init.c                  — Unchanged
include/ferrum/procgen/procgen_svo_builder.h — Updated config, kept
```

---

## 10. SymX Integration

### 10.1 Dependencies

```bash
# SymX is header-only + bundles its deps. Just add as submodule:
git submodule add https://github.com/InteractiveComputerGraphics/SymX extern/SymX

# SymX requires only:
# - C++17 compiler
# - CMake 3.15+
```

### 10.2 Build Integration

```cmake
# In CMakeLists.txt:
add_subdirectory(extern/SymX)

add_library(srd_energy STATIC
    src/procgen/srd/srd_energy.cpp
    src/procgen/srd/srd_optimizer.cpp
    src/procgen/srd/srd_rewrite.cpp
    src/procgen/srd/srd_sampler.cpp
    src/procgen/srd/srd_anneal.cpp
)
target_link_libraries(srd_energy PRIVATE SymX::SymX)
target_compile_features(srd_energy PRIVATE cxx_std_17)

# Link into headless lib
target_link_libraries(headless PRIVATE srd_energy)
```

### 10.3 Energy Definition Pattern (SymX API)

```cpp
// Per-room energy element
auto room_energy_expr = [&](Element& elem) {
    // Make symbols for this element's parameters
    Vector center   = elem.make_vector(room.center_xy, room.half_height);
    Vector size     = elem.make_vector(room.half_extent);
    Scalar target   = elem.make_scalar(1.0);  // want occupied

    // SDF computation (symbolically differentiated by SymX)
    Scalar energy = 0.0;
    for (auto& s : samples_in_box(center, size)) {
        Vector diff = (s - center).abs() - size;
        Scalar sdf = max(max(diff.x(), diff.y(), diff.z()), 0.0)
                   + min(max(diff.x(), diff.y(), diff.z()), 0.0);
        Scalar occ = sigmoid(-sdf / temperature);
        energy += pow(occ - target, 2);
    }
    return energy;
};

// Register elements with SymX
auto G = GlobalPotential::create();
G->add_potential("room", room_indices, room_energy_expr);
G->add_dof(room_centers);
G->add_dof(room_sizes);
G->add_dof(room_heights);

// Solve
NewtonsMethod newton(G, context);
newton.solve();
```

---

## 11. Test Structure

### 11.1 Unit Tests

```
tests/procgen/srd/
  srd_energy_tests.cpp         — SDF formulas, gradient/Hessian correctness
  srd_loss_primitives_tests.cpp— Each of 10 primitives: correct value, correct grad
  srd_eikonal_tests.cpp        — Eikonal solve on known obstacle, verify T field
  srd_transport_tests.cpp      — Transport solve, verify R drops through walls
  srd_loss_compiler_tests.cpp  — Parse LOSS: expression, build energy tree
  srd_grammar_tests.c          — Context check logic for each rewrite rule
  srd_rewrite_tests.c          — Apply/undo rewrite, element count correctness
  srd_smoke_tests.cpp          — 2→4 room ASCII + loss → valid geometry
  srd_connectivity_tests.cpp   — PathDistance loss produces reachable rooms
  srd_lineofsight_tests.cpp    — LineOfSight loss produces visible rooms
  srd_stair_tests.cpp          — Stair alignment across floors
  srd_overlap_tests.cpp        — NonPenetration loss → zero room overlap
  srd_timeout_tests.cpp        — Budget exhaustion produces valid partial result

tests/procgen/
  procgen_ascii_parse_tests.c       — Grid parsing, flood-fill, adjacency edges
  procgen_srd_integration_tests.c   — End-to-end: .asc + LOSS → renderable mesh
```

### 11.2 Test Data

```
datasets/ascii_grids/
  small_2room.asc              — 2 rooms, 1 corridor, 1 floor
  medium_4room.asc             — 4 rooms, 3 corridors
  large_12room.asc             — Multi-floor, stairs
  edge_cases/                  — Single cell, all-W, disconnected
  loss_examples/               — Grids with LOSS: blocks for each primitive
    path_distance.asc + loss
    line_of_sight.asc + loss
    separation.asc + loss
```

---

## 12. Migration from Current System

### 12.1 What Changes

| Before | After |
|--------|-------|
| `@grammar blockout v1` token string | `=== FLOOR 0: ... ===` ASCII grid |
| `ROOM_QUAD x=0 y=0 w=10 h=10` | `R R R` cells in grid |
| `CORRIDOR_H from=(5,0) to=(10,0)` | Implied by adjacency in grid |
| `SPAWN x=5 y=2 z=1` | First room of type `G` (Entrance) |
| `MARKER x=0 y=0 z=0 name=s` | `^` / `v` stair marker cells |
| Blockout grammar rasterizer | SymX SRD optimizer |
| Token string files (`*.txt`) | ASCII grid files (`*.asc`) |

### 12.2 What Stays

- **SVO (`progcen_svo_builder.c`)**: Unchanged algorithm, new input struct
- **Chunk mesh (`procgen_chunk_mesh.c`)**: Unchanged
- **Chunk builder (`procgen_chunk_builder.c`)**: Unchanged
- **SVG init (`npc_svo_init.c`)**: Unchanged
- **Client rendering (`demo_client.c`)**: Unchanged
- **Face winding**: Engine reference cube, unchanged
- **Architect VLM**: Reprompted to emit ASCII grids

---

## 13. Implementation Order

1. **Extern setup**: Add `extern/SymX` submodule, verify builds
2. **Types**: `procgen_srd_types.h` — RoomBox, CorridorSeg, StairDef, RoomGraph,
   LossPrimitive enum, LossExpression tree
3. **ASCII parser**: `procgen_ascii_parse.c` — flood-fill, adjacency, label extraction
4. **Loss primitives**: `srd_loss_primitives.cpp` — 10 differentiable primitives
5. **Loss compiler**: `srd_loss_compiler.cpp` — parse `LOSS:` block → energy tree
6. **PDE solvers**: `srd_eikonal.cpp` + `srd_transport.cpp` — differentiable field solvers
7. **SDF energy**: `srd_energy.cpp` — fixed hard-constraint elements (overlap, bounds, etc.)
8. **Grammar context**: `procgen_srd_grammar.c` — context-sensitive rewrite proposals
9. **Rewrite engine**: `srd_rewrite.cpp` — apply rewrite specs, index management
10. **Anneal**: `srd_anneal.cpp` — temperature schedule
11. **SRD loop**: `srd_optimizer.cpp` — Newton + rewrite schedule + VLM loss
12. **Bridge**: `procgen_srd_bridge.cpp` — C API: `srd_generate(ascii, loss, budget) → geometry`
13. **SVO integration**: Modify `procgen_svo_builder.c` to consume SRD geometry output
14. **Tests**: Write tests in parallel with each module (TDD)
15. **VLM migration**: Update architect prompt to emit ASCII grids + `LOSS:` blocks
16. **Dataset**: Generate .asc + .loss paired datasets for VLM fine-tuning
12. **Dataset**: Generate `.asc` grid dataset for DPO training

---

## 14. Open Decisions

1. **Sample point strategy**: For SDF evaluation, how many sample points per room?
   Uniform grid vs importance sampling near boundaries? Uniform 4×4×4 per room
   is a reasonable start; importance sampling near walls later.

2. **Rewrite acceptance**: Evaluate all proposals or random subset?
   If ≤30 proposals, evaluate all. If >30, sample ~20 randomly.

3. **Multi-floor ordering**: Optimize floor 0 first, then floor 1 with stair
   constraint from floor 0? Or all floors jointly? Joint optimization is
   cleaner but has 2× more parameters — start with sequential.

4. **Existing code removal**: Delete old token/grammar code now, or keep as
   `#ifdef` fallback until SRD is stable? Delete now — the SVO builder and
   mesh pipeline are the stable parts; the intermediate layer is the new code.
