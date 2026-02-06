# Physics Performance Analysis (3 ms Budget)

This document analyzes memory usage, performance scaling, bottlenecks, and
gameplay scenarios for the Ferrum physics engine.

**Budget variant: 3.0 ms/tick at 30 Hz.** This doubles the physics budget
compared to the 1.5 ms baseline, allowing richer scenes with more active
bodies, denser barricades, and heavier combat. See `physics_performance_analysis.md`
for the 1.5 ms baseline.

---

## 1. Memory Budget Per Rigid Body

### 1.1 Persistent Memory (survives across frames)

| Structure | Size per Body | Notes |
|-----------|---------------|-------|
| `phys_body_t` × 2 | 160 B | Double-buffered state |
| `phys_aabb_t` | 24 B | Bounding box |
| `phys_collider_t` | 36 B | Shape reference + local transform |
| Shape data (avg) | ~8 B | Sphere: 4B, Box: 12B, Capsule: 8B |
| Manifold cache (amortized) | ~20 B | ~0.25 cached manifolds per body |
| **Total persistent** | **~248 B/body** | |

### 1.2 Transient Memory (per tick, arena-allocated)

| Structure | Size per Body | Scaling Factor |
|-----------|---------------|----------------|
| Tier list index | 4 B | Always 1 per body |
| Collision pairs | ~40 B | ~5 pairs/body average |
| Contact candidates | ~280 B | ~2 contacts/body average |
| Manifolds | ~192 B | ~1 manifold/body average |
| Constraints | ~648 B | ~3 constraints/manifold |
| Velocity delta | 24 B | 1 per active body |
| Island membership | ~8 B | Amortized |
| **Total transient** | **~1.2 KB/body** | Varies with density |

### 1.3 Total Memory Scaling

| Body Count | Persistent | Transient/Tick | Total Working Set |
|------------|------------|----------------|-------------------|
| 100 | 24.8 KB | 120 KB | ~145 KB |
| 500 | 124 KB | 600 KB | ~725 KB |
| 1,000 | 248 KB | 1.2 MB | ~1.5 MB |
| 5,000 | 1.24 MB | 6.0 MB | ~7.2 MB |
| 10,000 | 2.48 MB | 12.0 MB | ~14.5 MB |
| 50,000 | 12.4 MB | 60.0 MB | ~72 MB |
| 100,000 | 24.8 MB | 120.0 MB | ~145 MB |

**Note:** Transient memory is reused each tick (arena reset), so it doesn't
accumulate. The "working set" is what must fit in cache for good performance.
With a 256 MB pool, memory is not the limiting factor — CPU time is.

### 1.4 Memory Pressure Thresholds

| Threshold | Body Count | Implication |
|-----------|------------|-------------|
| L2 cache (256 KB) | ~200 bodies | Hot path stays in L2 |
| L3 cache (8 MB) | ~5,500 bodies | Hot path stays in L3 |
| Frame arena (64 MB) | ~53,000 bodies | Arena overflow risk |
| Total budget (256 MB) | ~175,000 bodies | Hard limit (memory) |

With a 256 MB physics pool on PC/server, the practical limit is CPU time,
not memory. Even at 50,000 bodies the memory footprint is only ~72 MB.
The frame arena is sized at 64 MB (25% of pool), leaving 192 MB for
persistent structures, static BVH, and manifold cache.

---

## 2. Performance Scaling Analysis

### 2.1 Stage Complexity

| Stage | Time % | Complexity | Primary Scaling Factor |
|-------|--------|------------|------------------------|
| Step Plan | 0.5% | O(1) | Constant |
| Tier Classify | 3.0% | O(n) | Body count |
| Spatial Update | 3.0% | O(n) | Body count |
| Halo Closure | 4.0% | O(T0 × k) | T0 count × neighbor density |
| AABB Update | 3.0% | O(active) | Active body count |
| **Broadphase** | **8.0%** | O(n × d) | Bodies × spatial density |
| **Narrowphase** | **14.0%** | O(pairs) | Collision pair count |
| Manifold Build | 5.0% | O(pairs) | Pair count |
| Stabilization | 4.0% | O(manifolds) | Manifold count |
| Constraint Build | 5.0% | O(contacts) | Contact point count |
| Island Build | 5.0% | O(c × α(n)) | Constraint count |
| **TGS Solve** | **30.0%** | O(I × k × c/I) | Iterations × constraints |
| Integrate | 5.0% | O(n) | Body count |
| Cache Commit | 2.0% | O(manifolds) | Manifold count |
| Buffer Swap | 0.5% | O(1) | Constant |
| Network | 8.0% | O(changed) | Changed body count |

### 2.2 Scaling Regimes

**Linear Regime (< 1000 bodies):**
- All stages scale linearly
- Single-threaded sufficient
- Target: < 3.0 ms/tick

**Sublinear Regime (1000-5000 bodies):**
- Spatial structures provide acceleration
- Parallel jobs become beneficial
- Target: < 5.0 ms/tick

**Superlinear Regime (> 5000 bodies):**
- Pair count grows faster than body count
- Island size becomes critical
- Tiered simulation essential
- Target: < 8.0 ms/tick (with aggressive tiering)

### 2.3 The Primary Bottleneck: Hybrid Solver (TGS + XPBD)

The solver stage consumes ~30% of the physics tick. With the hybrid architecture,
TGS applies only to T0/T1 (near-field) and Jacobi XPBD handles T2–T4 (far-field).
This dramatically reduces the TGS bottleneck for large scenes.

**Why TGS was problematic (now mitigated):**

1. **Sequential within islands** — Gauss-Seidel iteration requires each
   constraint to see the result of the previous. No parallelism within
   an island. *But islands are now limited to T0/T1 bodies, which are
   spatially near the player and typically small.*

2. **Scales with largest island** — 100 bodies in 1 island is 100× slower
   than 100 bodies in 100 islands. *Worst case is now bounded by T0/T1
   body count (typically 20–80 near the player).*

3. **Iteration multiplier** — Each constraint processed 20-24 times per
   substep. With 2-3 substeps, that's 40-72 passes. *But only for the
   small near-field island set.*

**TGS time formula (T0/T1 only):**
```
T_tgs ≈ max(T0/T1 island_sizes) × iterations × constraint_time
      ≈ max_island × 22 × 0.5 µs
      ≈ max_island × 11 µs
```

| Max T0/T1 Island Size | TGS Time | % of 3.0ms Budget |
|------------------------|----------|-------------------|
| 10 constraints | 110 µs | 4% |
| 50 constraints | 550 µs | 18% |
| 100 constraints | 1.1 ms | 37% |
| 150 constraints | 1.65 ms | 55% |

**Jacobi XPBD (T2–T4): embarrassingly parallel**

XPBD does not use islands. Bodies are processed independently (Jacobi pattern).
Scaling is linear in body count and parallelizes across all available cores.

```
T_xpbd ≈ (T2-T4 body_count / num_jobs) × iterations × constraint_time
       ≈ (bodies / 8) × 6 × 0.3 µs  (cheaper per-iteration: position-level)
```

| T2–T4 Bodies | Jobs (8 threads) | XPBD Time | % of 3.0ms Budget |
|--------------|------------------|-----------|-------------------|
| 200 | 2 | 225 µs | 8% |
| 1000 | 8 | 225 µs | 8% |
| 5000 | 40 | 1.1 ms | 37% |
| 10000 | 80 | 2.25 ms | 75% |

**Combined solver budget:**  
T_solve = max(T_tgs, T_xpbd) because they run concurrently.  
Typical case: 100 near-field constraints + 1000 far-field bodies → max(1.1ms, 225µs) ≈ 1.1 ms (37%).

**Conclusion:** TGS is no longer the global bottleneck. The limiting factor shifts
to XPBD body count at ~8000+ far-field bodies, or TGS at ~130+ near-field island
constraints. Both are well within the 3 ms budget for rich gameplay.

### 2.4 Secondary Bottleneck: Narrowphase

Narrowphase scales with collision pair count, which can grow quadratically
in dense scenarios.

**Pair count estimation:**
```
Sparse scene:    pairs ≈ 2 × bodies
Average scene:   pairs ≈ 5 × bodies
Dense scene:     pairs ≈ 20 × bodies
Pathological:    pairs ≈ bodies² / 2
```

| Scenario | 100 Bodies | 500 Bodies | 1000 Bodies |
|----------|------------|------------|-------------|
| Sparse | 200 pairs | 1,000 pairs | 2,000 pairs |
| Average | 500 pairs | 2,500 pairs | 5,000 pairs |
| Dense | 2,000 pairs | 10,000 pairs | 20,000 pairs |
| Pathological | 5,000 pairs | 125,000 pairs ❌ | 500,000 pairs ❌ |

At ~7 µs per pair (including AABB + narrowphase), budget limits:
- 3.0 ms budget × 14% = 420 µs for narrowphase
- 420 µs / 7 µs = ~60 pairs per body maximum

---

## 3. Gameplay Scenario Analysis

### 3.1 Scenarios That Cause Performance Blow-ups

#### 3.1.1 The Collapsing Tower

**Setup:** 50 stacked boxes forming a tower, player knocks out base.

**Problem:**
- All 50 boxes become one island (connected through resting contacts)
- Each box has ~4 contacts = 200 manifolds
- If near player (T0/T1): 200 manifolds × 3 constraints = 600 constraints in TGS
- TGS time: 600 × 22 iterations × 0.5 µs = 6.6 ms ❌

**Mitigation with hybrid solver:**
- Only boxes within 15m (T0/T1) use TGS — maybe 20–25 boxes = 100 constraints
- Remaining boxes at T2+ use Jacobi XPBD: 30 bodies / 8 jobs ≈ 70 µs
- TGS: 100 × 22 × 0.5 µs ≈ 1.1 ms ✓
- Combined: max(1100, 70) ≈ 1.1 ms ✓
- Position-based stabilization for resting stacks
- Aggressive sleep detection
- Island splitting when velocity disparity detected

#### 3.1.2 The Explosion

**Setup:** Grenade explodes near 100 physics props.

**Problem:**
- All 100 props receive impulse simultaneously
- All wake from sleep at once
- Massive spike in active body count
- Broadphase/narrowphase spike as all AABBs overlap

**Hybrid solver advantage:**
- Most of the 100 props are at T2+ (only a few near the player)
- Far-field props go through Jacobi XPBD: parallelizes perfectly
- No giant-island problem — XPBD doesn't use islands
- 80 T2+ bodies / 8 threads ≈ 180 µs (XPBD)
- 20 T0/T1 bodies, small islands ≈ 300 µs (TGS)
- Combined: max(300, 180) ≈ 300 µs ✓ (90% headroom)

**Additional mitigation:**
- Stagger wake impulses over 2-3 frames
- Radial wake: center bodies first, edge bodies delayed
- XPBD compliance absorbs some explosion energy naturally

#### 3.1.3 The Ragdoll Pile

**Setup:** 15 ragdolls (15 bodies each) land in a pile.

**Problem:**
- 225 bodies in one location
- Each ragdoll has internal joints + inter-ragdoll contacts
- Single island with ~600 constraints if all in T0/T1
- Ragdoll joint constraints are stiff (need high iterations)

**Hybrid solver advantage:**
- Only ragdolls near player (T0/T1) get TGS — maybe 3-4 ragdolls = 60 bodies
- Remaining 11-12 ragdolls at T2+ use XPBD: 165 bodies / 8 jobs ≈ 375 µs
- XPBD is unconditionally stable, so distant ragdolls won't explode
- Distant ragdolls look slightly softer (compliance) — acceptable

**Additional mitigation:**
- Ragdoll simplification at distance (reduce to 5 bodies)
- Limit ragdoll count in pile (oldest despawn first)
- LOD: distant ragdolls become static meshes

#### 3.1.4 The Conveyor Belt

**Setup:** 400 boxes on a moving conveyor, all touching.

**Problem:**
- Continuous chain of contacts = one giant island
- Never sleeps (constant motion)
- 400 bodies × 2 contacts each = 800 manifolds = 2400 constraints
- Sustained high load every frame

**Hybrid solver advantage:**
- Conveyor belt is mostly far from player → T2–T4 XPBD
- XPBD doesn't build islands, so the "giant island" problem vanishes
- 400 XPBD bodies / 8 threads ≈ 900 µs
- Only boxes near player (maybe 20–30) go through TGS

**Mitigation:**
- Kinematic conveyor (not simulated, applies velocity directly)
- Reduced iteration count for conveyor-touching objects at T2+
- XPBD compliance naturally absorbs chain instability

#### 3.1.5 The Rain of Debris

**Setup:** Ceiling collapses, 800 small debris pieces fall.

**Problem:**
- 800 simultaneous spawns
- All in freefall = minimal contacts initially
- But spatial index update for 800 bodies = slow
- As they land: massive contact spike

**Hybrid solver advantage:**
- Most debris is at T2–T4: XPBD handles 750+ bodies in parallel
- No island explosion — Jacobi processes each body independently
- Only debris near player (T0/T1) needs TGS: ~30 bodies

**Additional mitigation:**
- Visual-only debris (particles) for most pieces
- Only 20-50 pieces get physics simulation
- Debris pooling with aggressive despawn
- Background tier (T4) with 10 Hz update rate

### 3.2 Ideal Performance Scenarios

#### 3.2.1 Sparse Open World

**Setup:** Rolling hills with scattered boulders, player vehicle.

**Characteristics:**
- 50-100 dynamic bodies total
- Most are far apart (no contacts)
- 5-10 bodies near player get T0/T1 treatment
- Many small islands (1-3 bodies each)

**Performance:**
- ~20 collision pairs
- ~10 small islands
- TGS parallelizes across islands
- **Result: 0.3-0.5 ms/tick** ✓

#### 3.2.2 Sparse Indoor

**Setup:** Office with desks, chairs, scattered props.

**Characteristics:**
- 200 dynamic bodies total
- Most sleeping (resting on surfaces)
- 10-20 active at any time
- Player interactions wake small clusters

**Performance:**
- Most bodies skip simulation (sleeping)
- Wake propagation is local
- Islands are small (furniture clusters)
- **Result: 0.5-0.8 ms/tick** ✓

#### 3.2.3 Vehicle Simulation

**Setup:** Player car with 4 wheel constraints, hitting traffic cones.

**Characteristics:**
- 1 vehicle body + 4 wheel bodies
- Vehicle is its own island (5 bodies, 8 constraints)
- Traffic cones are separate islands (1 body each)
- Collision with cone briefly merges islands, then separates

**Performance:**
- Vehicle island: 8 constraints × 22 iter = 176 solves
- Each cone: 3 constraints × 22 iter = 66 solves
- **Result: 0.4-0.6 ms/tick** ✓ (80% headroom)

### 3.3 Average-Case Analysis: Rich Game World

#### 3.3.1 Reference World: "Ruined City Block"

A representative open-world game area with:

**Static geometry:**
- 50 buildings (convex decomposed, in static BVH)
- 200 static props (benches, mailboxes, barriers)
- Terrain mesh

**Dynamic objects:**
| Category | Count | Typical State |
|----------|-------|---------------|
| Vehicles (parked) | 40 | Sleeping |
| Vehicles (traffic) | 10 | Active, T2 |
| Player vehicle | 1 | Active, T0 |
| Ragdolls | 0-10 | Active/Sleeping |
| Debris piles | 20 | Sleeping |
| Props (crates, barrels) | 200 | Mostly sleeping |
| Doors/hatches | 50 | Sleeping |
| Destructibles | 80 | Sleeping |
| **Total dynamic** | **~420** | |

**Typical frame state:**
| Tier | Body Count | Constraints | Notes |
|------|------------|-------------|-------|
| T0 (Direct) | 3-10 | 20-50 | Player + held objects |
| T1 (Near) | 15-30 | 50-100 | Within reach |
| T2 (Visible) | 40-80 | 100-200 | On-screen |
| T3 (World) | 20-50 | 40-100 | Active but distant |
| T4 (Background) | 50-80 | 0 | Minimal simulation |
| T5 (Sleeping) | 200-300 | 0 | No simulation |

**Island structure (T0/T1 only — T2+ use XPBD, no islands):**
- Player cluster: 1 island, 10-25 bodies, 30-80 constraints (TGS)
- Active ragdolls near player: 2-3 islands, 15 bodies each, 40 constraints (TGS)

**XPBD body set (T2–T4):**
- Traffic vehicles: 10 bodies, 0 constraints
- Distant ragdolls: 30-60 bodies, 80-160 constraints
- Scattered debris: 20-50 bodies, 0-30 constraints

**Performance breakdown:**
```
Tier classification:  420 bodies × 0.1 µs = 42 µs
Spatial update:       420 bodies × 0.2 µs = 84 µs
Halo closure:         10 T0 bodies × 10 µs = 100 µs
AABB update:          130 active × 0.3 µs = 39 µs
Broadphase:           130 active × 1.5 µs = 195 µs
Narrowphase:          300 pairs × 3.0 µs = 900 µs
Manifold build:       160 manifolds × 1.0 µs = 160 µs
Stabilization:        160 manifolds × 0.5 µs = 80 µs
Constraint build:     400 constraints × 0.5 µs = 200 µs
Island build:         160 T0/T1 constraints × 0.3 µs = 48 µs
Solver:               See below
Integrate:            130 active × 0.5 µs = 65 µs
Cache commit:         160 manifolds × 0.3 µs = 48 µs

TGS solve (T0/T1, per island):
  Player cluster:     80 constraints × 22 iter × 0.5 µs = 880 µs
  Near ragdoll 1:     40 constraints × 20 iter × 0.5 µs = 400 µs
  Near ragdoll 2:     40 constraints × 20 iter × 0.5 µs = 400 µs
  Subtotal TGS (parallel): max(880, 400+400) ≈ 880 µs with 3+ workers

XPBD solve (T2–T4, parallel over bodies):
  110 T2–T4 bodies, ~220 constraints × 6 iter × 0.3 µs ≈ 400 µs
  8 threads → ~50 µs per thread

Solver total: max(880, 50) ≈ 880 µs (TGS dominates, but bounded)
Stages 11a+11b run concurrently.

Substep total: ~1.4 ms (parallel)
Per-tick (2 substeps): ~2.8 ms (single-threaded), ~1.8 ms (parallel)
```

**Conclusion for rich world:**
- Single-threaded: ~2.8 ms/tick (within 3.0 ms budget) ✓
- Parallel (4+ workers): ~1.8 ms/tick (40% headroom) ✓
- Headroom for spikes: ~1.2 ms
- The 3 ms budget allows single-threaded operation in most scenarios

#### 3.3.2 Maximum Object Counts (Within Budget)

For a 3.0 ms physics tick budget:

| Scenario | Max Active Bodies | Max Constraints | Notes |
|----------|-------------------|-----------------|-------|
| Sparse outdoor | 5,000 | 10,000 | Most at T2–T4, XPBD parallel |
| Dense indoor | 1,200 | 6,000 | Tight clusters, more TGS near player |
| Combat (explosions) | 2,500 | 8,000 | XPBD absorbs far-field blast |
| Ragdoll heavy | 600 | 8,000 | 40 full ragdolls, most at T2+ |
| Vehicle physics | 1,500 | 4,000 | Stiff wheel constraints near player |
| Puzzle physics | 400 | 6,000 | Large connected mechanism (TGS) |

**Hard limits for stability:**
- Maximum island size: 250 constraints (TGS bottleneck — T0/T1 only)
- Maximum pair count: 20,000 pairs (narrowphase CPU budget)
- Maximum T0 bodies: 30 (high-fidelity budget)
- Maximum active bodies: 10,000 (CPU time budget with tiering + XPBD)
- Maximum total bodies: 175,000 (256 MB pool capacity)

---

## 4. Optimization Recommendations

### 4.1 Architectural Mitigations

| Problem | Mitigation | Implementation |
|---------|------------|----------------|
| Large islands | Island splitting | Detect velocity clusters, break weak contacts |
| TGS sequential | Per-island parallelism | Job per island, batch small islands |
| Dense broadphase | Spatial hashing | Tune cell size to average body size |
| Pair explosion | Pair budget cap | Skip low-priority pairs when over budget |
| Ragdoll piles | LOD simplification | Reduce body count at distance |
| Wake storms | Staggered wake | Spread impulses over multiple frames |

### 4.2 Tier System Tuning

| Tier | Max Bodies | Solver | Iterations | Substeps | Notes |
|------|------------|--------|------------|----------|-------|
| T0 | 30 | TGS | 24 | 3 | Player interaction only |
| T1 | 100 | TGS | 20 | 2 | Within arm's reach |
| T2 | 1,000 | XPBD | 8 | 1 | Visible, parallel |
| T3 | 4,000 | XPBD | 6 | 1 | Far but consequential |
| T4 | 20,000 | XPBD | 4 | 0.5 (amortized) | Background, aggressive sleep |
| T5 | ∞ | — | 0 | 0 | Sleeping, event-driven wake |

### 4.3 Budget Allocation by Game Type

| Game Type | Physics Budget | Focus Area |
|-----------|----------------|------------|
| Racing | 2.0 ms | Vehicle constraints, simple collisions |
| FPS | 3.0 ms | Ragdolls, destruction debris |
| Puzzle | 3.0 ms | Complex mechanisms, precision |
| Open World | 3.0 ms | Tiering, LOD, aggressive culling |
| Fighting | 2.0 ms | Character physics, low prop count |

---

## 5. Profiling Checklist

When performance issues occur, check in order:

1. **Island sizes** - Is there one huge island? (TGS bottleneck, T0/T1 only)
2. **Pair count** - Pair count vs body count ratio > 10? (Broadphase/narrowphase)
3. **T0 count** - More than 30 T0 bodies? (High-fidelity overload)
4. **Active count** - More than 10,000 active bodies? (CPU time pressure)
5. **Wake rate** - Bodies waking faster than sleeping? (Stability issue)
6. **Cache hit rate** - Manifold cache misses > 20%? (Warmstarting failing)

**Tracy zones to watch:**
```
Phys.Solve.IteratingTGS      > 1.2 ms → Island size problem
Phys.Narrow.TestingCollisions > 600 µs → Pair count problem
Phys.Broad.FindingPairs      > 300 µs → Spatial index problem
Phys.Barrier.*               > 200 µs → Job scheduling problem
```

---

## 6. Game-Specific Analysis: "The March of Glaciers"

The design document (see `design/the_march_of_glaciers.md`) describes a
physics-first immersive sim with barricade building, ragdolls, debris,
environmental destruction, and freeform stacking. Every gameplay system
routes through the physics engine. This section analyzes expected CPU usage
across representative level archetypes at different clutter densities.

### 6.1 Level Archetypes

The game world is underground: tunnels, maintenance corridors, abandoned
infrastructure. Geometry is modular and industrial. Five representative
archetypes capture the range of physics scenarios:

| Archetype | Description | Static Geo | Typical Dynamic |
|-----------|-------------|-----------|-----------------|
| **Narrow Tunnel** | Single-corridor passage, low ceiling | Low | Low |
| **Hub Chamber** | Multi-entrance room, vertical space | Medium | Medium–High |
| **Trash Drop Zone** | Open area under drop chute | Medium | Extreme (spike) |
| **Barricaded Holdout** | Player-fortified room with fire | Low–Medium | High (persistent) |
| **Creature Nest** | Organic geometry, bodies, skull piles | Medium | Medium |

### 6.2 Clutter Density Levels

Three density tiers model the range of level dressing and player-created
clutter. Each tier includes both placed-by-designer and accumulated-by-player
objects.

**Low Clutter** — freshly entered, minimal player modification:
- 30–60 dynamic bodies (pipes, loose panels, small debris)
- 0–3 ragdolls
- No player-built structures
- 8–15 bodies active at any time (most sleeping)

**Medium Clutter** — explored area with some player fortification:
- 120–250 dynamic bodies (scrap piles, crates, barricade pieces, skulls)
- 3–8 ragdolls (killed enemies, creatures)
- 1–2 active fires (heat source, light source)
- 30–60 bodies active

**High Clutter** — heavily fortified holdout or post-combat aftermath:
- 300–600 dynamic bodies (dense barricade, debris fields, scattered props)
- 8–15 ragdolls
- 2–4 fires
- 80–200 bodies active (barricade under stress, settling debris)

### 6.3 Per-Archetype CPU Analysis

All numbers assume 30 Hz tick rate, 2 substeps, 8-thread parallel.
Budget target: 3.0 ms/tick (with 1.0 ms spike headroom).

#### 6.3.1 Narrow Tunnel — Low Clutter

```
Static BVH nodes traversed:   ~50 (simple corridor)
Active bodies:                 12 (player kicks debris, loose pipes)
T0/T1:                        5 (player + nearby objects)
T2–T4:                        7 (scattered pipes, panels)
Collision pairs:               20
TGS constraints:               10 (1 small island)
XPBD constraints:              8

TGS:   10 × 22 iter × 0.5 µs  = 110 µs
XPBD:  8 × 6 iter × 0.3 µs    =  14 µs
Narrow: 20 × 3.0 µs            =  60 µs
Other stages:                   ~100 µs
                               ─────────
Total:                          ~0.3 ms/tick ✓✓✓ (90% headroom)
```

**Risk:** None. Trivial load.

#### 6.3.2 Hub Chamber — Medium Clutter

```
Static BVH nodes traversed:   ~200 (multi-level room)
Active bodies:                 55 (barricade pieces, debris, 4 ragdolls)
T0/T1:                        18 (player area: fire, held object, nearby scrap)
T2–T4:                        37 (visible crates, distant ragdolls)
Collision pairs:               150
TGS constraints:               70 (player cluster + 2 ragdolls near player)
XPBD constraints:              90

TGS:   70 × 22 × 0.5 µs       = 770 µs
XPBD:  90 × 6 × 0.3 µs / 4    =  40 µs (parallel)
Narrow: 150 × 3.0 µs           = 450 µs
Other stages:                   ~350 µs
                               ─────────
Total:                          ~1.6 ms/tick ✓ (47% headroom)
```

**Risk:** Moderate. TGS dominates if player is near a ragdoll pile.
Monitor `Phys.Solve.IteratingTGS` — if > 1.0 ms, a near-field ragdoll
has too many constraints. Mitigate with ragdoll LOD (see §6.5).

#### 6.3.3 Trash Drop Zone — High Clutter (Spike Event)

A trash drop is the worst-case physics spike: 80–150 new objects fall from
above simultaneously, bounce off geometry and each other, then settle.

```
SPIKE FRAME (Frame 0 of drop):
Active bodies:                 200 (130 new drop + 70 existing)
T0/T1:                        20 (player + nearby falling objects)
T2–T4:                        180 (most drop objects are far-field)
Collision pairs:               1000 (dense overlap during fall)
TGS constraints:               80 (near-field contacts)
XPBD constraints:              700 (far-field pile-up)

TGS:   80 × 22 × 0.5 µs       = 880 µs
XPBD:  700 × 6 × 0.3 µs / 8   = 158 µs (parallel, 8 threads)
Narrow: 1000 × 3.0 µs / 8      = 375 µs (parallel)
Broad:  200 × 1.5 µs           = 300 µs
Other stages:                   ~300 µs
                               ─────────
Total:                          ~2.0 ms/tick ✓ (33% headroom)

SETTLING (Frames 5–30):
Active bodies:                 130 → 30 (progressive sleep)
Pair count:                    600 → 80 (objects separate and rest)
Total:                         ~1.2 ms → 0.4 ms (recovers over 1 second)
```

**Risk:** High pair count during initial contact spike. XPBD handles the
far-field pile-up cleanly — no giant-island problem. The 3 ms budget absorbs
the spike comfortably even without staggering.

**Without hybrid solver:** All 700 constraints would form one TGS island.
700 × 22 × 0.5 µs = 7.7 ms ❌. The hybrid solver is essential for this scenario.

#### 6.3.4 Barricaded Holdout — High Clutter (Persistent)

Player has fortified a room with stacked scrap, wedged doors, skull anchors,
and a fire. Enemies are pushing against the barricade.

```
Active bodies:                 140 (60 barricade, 50 loose props, 15 enemy, 15 ragdoll)
T0/T1:                        30 (barricade face + player + fire + enemy bodies)
T2–T4:                        110 (back of barricade, distant debris, ragdolls)
Collision pairs:               450
TGS constraints:               140 (dense barricade stack near player)
XPBD constraints:              200

TGS:   140 × 22 × 0.5 µs      = 1.54 ms  (51% of budget)
XPBD:  200 × 6 × 0.3 µs / 8   =  45 µs
Narrow: 450 × 3.0 µs / 8       = 169 µs
Other stages:                   ~350 µs
                               ─────────
Total:                          ~2.1 ms/tick ✓ (30% headroom)
```

**Risk:** TGS is at 51% of total budget. The barricade stack is a dense
near-field island — many resting contacts, all at T0/T1 because the player
is standing right next to it. This is the hardest steady-state scenario,
but fits comfortably within the 3 ms budget.

**If barricade is being attacked:** Enemy pushes wake sleeping contacts,
island grows temporarily. Could spike to 180 TGS constraints = 1.98 ms TGS.
Total: ~2.5 ms. Still within budget ✓.

#### 6.3.5 Creature Nest — Medium Clutter

Organic geometry with skull piles, creature bodies, and irregular shapes.

```
Active bodies:                 70 (skull pile, creature ragdolls, loose bones)
T0/T1:                        12 (player + nearby skulls being kicked/thrown)
T2–T4:                        58 (distant skull piles, creature bodies)
Collision pairs:               180
TGS constraints:               40 (small near-field contacts)
XPBD constraints:              130

TGS:   40 × 22 × 0.5 µs       = 440 µs
XPBD:  130 × 6 × 0.3 µs / 8   =  29 µs
Narrow: 180 × 3.0 µs           = 540 µs
Other stages:                   ~300 µs
                               ─────────
Total:                          ~1.3 ms/tick ✓ (57% headroom)
```

**Risk:** Low. Skull colliders may be complex (horns, cavities), increasing
per-pair narrowphase cost. Convex decomposition quality matters here.

### 6.4 Clutter Scaling Summary

| Archetype | Low | Medium | High | Spike |
|-----------|-----|--------|------|-------|
| Narrow Tunnel | 0.3 ms ✓ | 0.6 ms ✓ | 1.2 ms ✓ | — |
| Hub Chamber | 0.4 ms ✓ | 1.6 ms ✓ | 2.5 ms ✓ | — |
| Trash Drop Zone | 0.4 ms ✓ | 0.9 ms ✓ | 1.6 ms ✓ | 2.0 ms ✓ |
| Barricaded Holdout | 0.4 ms ✓ | 1.3 ms ✓ | 2.1 ms ✓ | 2.5 ms ✓ |
| Creature Nest | 0.4 ms ✓ | 1.3 ms ✓ | 2.0 ms ✓ | — |

⚠️ = within budget but limited headroom  
❌ = over budget, requires mitigation

### 6.5 Technical Mitigations

These are engine-level solutions for physics-heavy scenarios.

| Problem | Fix | Effect |
|---------|-----|--------|
| **Dense barricade island** | Sleep-stabilize resting contacts after 0.5s of low velocity | Barricade pieces sleep → leave TGS island |
| **Barricade under attack** | Island splitting: detect velocity gradient, break island at weak contacts | Reduce max island to ~50 constraints |
| **Ragdoll pile near player** | Ragdoll LOD: reduce to 5 bodies at T1, 3 bodies at T2+ | Cut ragdoll constraints by 60% |
| **Trash drop spike** | Staggered spawn: spread 80 objects over 3–5 frames (16–26/frame) | Peak active count halved |
| **Skull convex hull cost** | Precomputed convex decomposition, max 4 hulls per skull type | Bound narrowphase cost |
| **Post-combat debris** | Aggressive sleep timeout: 0.3s for T3+ debris | Rapid return to baseline |
| **Fire particle bodies** | Use trigger volumes for heat, not physics bodies for embers | Remove 10–20 bodies per fire |
| **Persistent clutter growth** | Hard cap: max 500 dynamic bodies per loaded area | Designer and player budget |

### 6.6 Artistic / Level-Design Mitigations

These are design and content-level solutions that reduce physics load without
changing the engine. These are arguably more important than technical fixes
because they prevent the problem at the source.

| Principle | Implementation | Why It Works |
|-----------|----------------|--------------|
| **Merge static clutter** | Rubble piles, pipe bundles, and debris fields that look dynamic but are baked static meshes | 80% of environmental detail needs no simulation |
| **Breakable → debris LOD** | When a structure breaks, spawn 3–5 physics pieces + a particle effect, not 20 individual shards | Controls spike object count |
| **Barricade piece granularity** | Make barricade materials medium-sized (crate, shelf, pipe) not small (individual bricks, bolts) | Fewer bodies per barricade for same visual density |
| **Skull size variety** | Larger skulls = fewer needed for same barricade weight | Reduces body count in fortifications |
| **Chokepoint geometry** | Tunnels and doorways naturally limit contact surface area | Barricades need fewer pieces to be effective |
| **Fire radius design** | Fire warmth radius is small enough that the "hot zone" contains ≤ 25 dynamic bodies | Bounds T0/T1 body count |
| **Trash drop funnel** | Drop zone geometry funnels falling objects outward, not into a single pile | Prevents O(n²) pair counts |
| **Creature body despawn** | Dead creatures begin dissolving after 60s (visual fade + body removal) | Caps persistent ragdoll count |
| **Sleeping visual trick** | Sleeping objects get a subtle "settled dust" particle, making freeze less obvious | Allows aggressive sleep thresholds |
| **Island-breaking gaps** | Leave 5cm gaps in pre-placed debris piles so resting stacks form multiple small islands instead of one large one | Directly reduces TGS island size |

### 6.7 Worst-Case Scenario: Full Holdout Under Siege

The absolute worst case combines every problem: dense barricade, active fire,
player at the barricade, enemies pushing through, ragdolls accumulating,
and debris flying.

```
Active bodies:                 220
T0/T1:                        35 (barricade face, fire, player, enemy ragdolls)
T2–T4:                        185 (back wall, far debris, old ragdolls)
Collision pairs:               800
TGS constraints:               170 (barricade + ragdolls + enemy contacts)
XPBD constraints:              350

TGS:   170 × 22 × 0.5 µs      = 1.87 ms
XPBD:  350 × 6 × 0.3 µs / 8   =  79 µs
Narrow: 800 × 3.0 µs / 8       = 300 µs
Other stages:                   ~400 µs
                               ─────────
Total:                          ~2.7 ms/tick ✓ (within 3 ms budget)
```

This scenario fits within the 3 ms budget without any automatic mitigations.
However, applying mitigations provides headroom for even worse spikes:

**Recovery plan (applied in order, each reduces TGS load):**

1. **Sleep-stabilize barricade** (−40 constraints): pieces resting > 0.5s
   become sleeping contacts. TGS: 130 constraints → 1.43 ms.
2. **Ragdoll LOD** (−30 constraints): enemy ragdolls at T0 drop from 15
   to 5 bodies. TGS: 100 constraints → 1.1 ms.
3. **Island split** (−concurrent): barricade island broken at velocity
   boundary. Two islands of ~50 each, solved in parallel. TGS: 550 µs.
4. **Result:** ~1.3 ms/tick ✓ (57% headroom restored)

All four mitigations are automatic (no player-visible quality change).

---

## 7. Summary

**Memory:** ~1.4 KB per active rigid body (248 B persistent + 1.2 KB transient).
256 MB pool supports 175k total bodies; CPU time is the limiting factor.

**Primary bottleneck:** TGS Solve (12% of tick CPU, but dominates wall-clock
for dense near-field islands). Only applies to T0/T1 bodies.

**Secondary bottleneck:** Narrowphase (14% of tick, scales with pair count).

**Tertiary bottleneck:** XPBD Solve (18% of tick CPU, but parallelizes
perfectly and rarely dominates wall-clock).

**Safe operating envelope (3 ms budget):**
- Active bodies: ≤ 10,000 (with tiering)
- T0/T1 bodies: ≤ 35
- Largest TGS island: ≤ 200 constraints
- Collision pairs: ≤ 20,000
- Near-field constraint density: ≤ 6 constraints/body

**For March of Glaciers:** Typical gameplay runs at 0.5–1.6 ms/tick. Dense
barricade holdouts reach 2.1 ms. The worst-case siege scenario hits 2.7 ms
and fits within budget without mitigations. With mitigations, it drops to
1.3 ms. The 3 ms budget eliminates all ⚠️ and ❌ scenarios from the 1.5 ms
variant — every archetype at every clutter level fits comfortably.
