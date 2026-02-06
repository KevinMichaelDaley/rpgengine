# Physics Performance Analysis

This document analyzes memory usage, performance scaling, bottlenecks, and
gameplay scenarios for the Ferrum physics engine.

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

**Linear Regime (< 500 bodies):**
- All stages scale linearly
- Single-threaded sufficient
- Target: < 1.5 ms/tick

**Sublinear Regime (500-2000 bodies):**
- Spatial structures provide acceleration
- Parallel jobs become beneficial
- Target: < 3.0 ms/tick

**Superlinear Regime (> 2000 bodies):**
- Pair count grows faster than body count
- Island size becomes critical
- Tiered simulation essential
- Target: < 5.0 ms/tick (with aggressive tiering)

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

| Max T0/T1 Island Size | TGS Time | % of 1.5ms Budget |
|------------------------|----------|-------------------|
| 10 constraints | 110 µs | 7% |
| 30 constraints | 330 µs | 22% |
| 50 constraints | 550 µs | 37% |
| 80 constraints | 880 µs | 59% |

**Jacobi XPBD (T2–T4): embarrassingly parallel**

XPBD does not use islands. Bodies are processed independently (Jacobi pattern).
Scaling is linear in body count and parallelizes across all available cores.

```
T_xpbd ≈ (T2-T4 body_count / num_jobs) × iterations × constraint_time
       ≈ (bodies / 8) × 6 × 0.3 µs  (cheaper per-iteration: position-level)
```

| T2–T4 Bodies | Jobs (8 threads) | XPBD Time | % of 1.5ms Budget |
|--------------|------------------|-----------|-------------------|
| 100 | 1 | 180 µs | 12% |
| 500 | 4 | 225 µs | 15% |
| 2000 | 16 | 450 µs | 30% |
| 5000 | 40 | 1.1 ms | 73% |

**Combined solver budget:**  
T_solve = max(T_tgs, T_xpbd) because they run concurrently.  
Typical case: 50 near-field constraints + 500 far-field bodies → max(550µs, 225µs) ≈ 550 µs (37%).

**Conclusion:** TGS is no longer the global bottleneck. The limiting factor shifts
to XPBD body count at ~3000+ far-field bodies, or TGS at ~60+ near-field island
constraints. Both are well within budget for typical gameplay.

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
- 1.5 ms budget × 14% = 210 µs for narrowphase
- 210 µs / 7 µs = ~30 pairs per body maximum

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
- Only boxes within 15m (T0/T1) use TGS — maybe 10–15 boxes = 60 constraints
- Remaining boxes at T2+ use Jacobi XPBD: 40 bodies / 8 jobs ≈ 90 µs
- TGS: 60 × 22 × 0.5 µs ≈ 660 µs ✓
- Combined: max(660, 90) ≈ 660 µs ✓
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
- 20 T0/T1 bodies, small islands ≈ 200 µs (TGS)
- Combined: max(200, 180) ≈ 200 µs ✓

**Additional mitigation:**
- Stagger wake impulses over 2-3 frames
- Radial wake: center bodies first, edge bodies delayed
- XPBD compliance absorbs some explosion energy naturally

#### 3.1.3 The Ragdoll Pile

**Setup:** 10 ragdolls (15 bodies each) land in a pile.

**Problem:**
- 150 bodies in one location
- Each ragdoll has internal joints + inter-ragdoll contacts
- Single island with ~400 constraints if all in T0/T1
- Ragdoll joint constraints are stiff (need high iterations)

**Hybrid solver advantage:**
- Only ragdolls near player (T0/T1) get TGS — maybe 2-3 ragdolls = 45 bodies
- Remaining 7-8 ragdolls at T2+ use XPBD: 105 bodies / 8 jobs ≈ 240 µs
- XPBD is unconditionally stable, so distant ragdolls won't explode
- Distant ragdolls look slightly softer (compliance) — acceptable

**Additional mitigation:**
- Ragdoll simplification at distance (reduce to 5 bodies)
- Limit ragdoll count in pile (oldest despawn first)
- LOD: distant ragdolls become static meshes

#### 3.1.4 The Conveyor Belt

**Setup:** 200 boxes on a moving conveyor, all touching.

**Problem:**
- Continuous chain of contacts = one giant island
- Never sleeps (constant motion)
- 200 bodies × 2 contacts each = 400 manifolds = 1200 constraints
- Sustained high load every frame

**Hybrid solver advantage:**
- Conveyor belt is mostly far from player → T2–T4 XPBD
- XPBD doesn't build islands, so the "giant island" problem vanishes
- 200 XPBD bodies / 8 threads ≈ 450 µs
- Only boxes near player (maybe 10–20) go through TGS

**Mitigation:**
- Kinematic conveyor (not simulated, applies velocity directly)
- Reduced iteration count for conveyor-touching objects at T2+
- XPBD compliance naturally absorbs chain instability

#### 3.1.5 The Rain of Debris

**Setup:** Ceiling collapses, 500 small debris pieces fall.

**Problem:**
- 500 simultaneous spawns
- All in freefall = minimal contacts initially
- But spatial index update for 500 bodies = slow
- As they land: massive contact spike

**Hybrid solver advantage:**
- Most debris is at T2–T4: XPBD handles 450+ bodies in parallel
- No island explosion — Jacobi processes each body independently
- Only debris near player (T0/T1) needs TGS: ~20 bodies

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
- Vehicle island: 8 constraints × 16 iter = 128 solves
- Each cone: 3 constraints × 16 iter = 48 solves
- **Result: 0.4-0.6 ms/tick** ✓

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
| Vehicles (parked) | 20 | Sleeping |
| Vehicles (traffic) | 5 | Active, T2 |
| Player vehicle | 1 | Active, T0 |
| Ragdolls | 0-5 | Active/Sleeping |
| Debris piles | 10 | Sleeping |
| Props (crates, barrels) | 100 | Mostly sleeping |
| Doors/hatches | 30 | Sleeping |
| Destructibles | 50 | Sleeping |
| **Total dynamic** | **~220** | |

**Typical frame state:**
| Tier | Body Count | Constraints | Notes |
|------|------------|-------------|-------|
| T0 (Direct) | 1-5 | 10-30 | Player + held objects |
| T1 (Near) | 10-20 | 30-60 | Within reach |
| T2 (Visible) | 20-40 | 50-100 | On-screen |
| T3 (World) | 10-30 | 20-50 | Active but distant |
| T4 (Background) | 30-50 | 0 | Minimal simulation |
| T5 (Sleeping) | 100-150 | 0 | No simulation |

**Island structure (T0/T1 only — T2+ use XPBD, no islands):**
- Player cluster: 1 island, 5-15 bodies, 20-50 constraints (TGS)
- Active ragdolls near player: 1-2 islands, 15 bodies each, 40 constraints (TGS)

**XPBD body set (T2–T4):**
- Traffic vehicles: 5 bodies, 0 constraints
- Distant ragdolls: 15-30 bodies, 40-80 constraints
- Scattered debris: 10-30 bodies, 0-15 constraints

**Performance breakdown:**
```
Tier classification:  220 bodies × 0.1 µs = 22 µs
Spatial update:       220 bodies × 0.2 µs = 44 µs
Halo closure:         5 T0 bodies × 10 µs = 50 µs
AABB update:          70 active × 0.3 µs = 21 µs
Broadphase:           70 active × 1.5 µs = 105 µs
Narrowphase:          150 pairs × 3.0 µs = 450 µs
Manifold build:       80 manifolds × 1.0 µs = 80 µs
Stabilization:        80 manifolds × 0.5 µs = 40 µs
Constraint build:     200 constraints × 0.5 µs = 100 µs
Island build:         90 T0/T1 constraints × 0.3 µs = 27 µs
Solver:               See below
Integrate:            70 active × 0.5 µs = 35 µs
Cache commit:         80 manifolds × 0.3 µs = 24 µs

TGS solve (T0/T1, per island):
  Player cluster:     50 constraints × 22 iter × 0.5 µs = 550 µs
  Near ragdoll:       40 constraints × 20 iter × 0.5 µs = 400 µs
  Subtotal TGS (parallel): max(550, 400) ≈ 550 µs with 2+ workers

XPBD solve (T2–T4, parallel over bodies):
  55 T2–T4 bodies, ~110 constraints × 6 iter × 0.3 µs ≈ 200 µs
  8 threads → ~25 µs per thread

Solver total: max(550, 25) ≈ 550 µs (TGS dominates, but bounded)
Stages 11a+11b run concurrently.

Substep total: ~0.85 ms (parallel)
Per-tick (2 substeps): ~1.7 ms (single-threaded), ~1.1 ms (parallel)
```

**Conclusion for rich world:**
- Single-threaded: ~1.7 ms/tick (slightly above 1.5 ms budget)
- Parallel (4+ workers): ~1.1 ms/tick (well within budget) ✓
- Headroom for spikes: ~0.9 ms
- The hybrid solver provides ~30% more headroom vs pure TGS

#### 3.3.2 Maximum Object Counts (Within Budget)

For a 1.5 ms physics tick budget:

| Scenario | Max Active Bodies | Max Constraints | Notes |
|----------|-------------------|-----------------|-------|
| Sparse outdoor | 2,000 | 4,000 | Most at T2–T4, XPBD parallel |
| Dense indoor | 500 | 2,500 | Tight clusters, more TGS near player |
| Combat (explosions) | 1,000 | 3,000 | XPBD absorbs far-field blast |
| Ragdoll heavy | 300 | 4,000 | 20 full ragdolls, most at T2+ |
| Vehicle physics | 800 | 2,000 | Stiff wheel constraints near player |
| Puzzle physics | 200 | 3,000 | Large connected mechanism (TGS) |

**Hard limits for stability:**
- Maximum island size: 150 constraints (TGS bottleneck — T0/T1 only)
- Maximum pair count: 10,000 pairs (narrowphase CPU budget)
- Maximum T0 bodies: 20 (high-fidelity budget)
- Maximum active bodies: 5,000 (CPU time budget with tiering + XPBD)
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
| T0 | 20 | TGS | 24 | 3 | Player interaction only |
| T1 | 60 | TGS | 20 | 2 | Within arm's reach |
| T2 | 500 | XPBD | 8 | 1 | Visible, parallel |
| T3 | 2,000 | XPBD | 6 | 1 | Far but consequential |
| T4 | 10,000 | XPBD | 4 | 0.5 (amortized) | Background, aggressive sleep |
| T5 | ∞ | — | 0 | 0 | Sleeping, event-driven wake |

### 4.3 Budget Allocation by Game Type

| Game Type | Physics Budget | Focus Area |
|-----------|----------------|------------|
| Racing | 1.0 ms | Vehicle constraints, simple collisions |
| FPS | 1.5 ms | Ragdolls, destruction debris |
| Puzzle | 2.0 ms | Complex mechanisms, precision |
| Open World | 1.5 ms | Tiering, LOD, aggressive culling |
| Fighting | 1.0 ms | Character physics, low prop count |

---

## 5. Profiling Checklist

When performance issues occur, check in order:

1. **Island sizes** - Is there one huge island? (TGS bottleneck, T0/T1 only)
2. **Pair count** - Pair count vs body count ratio > 10? (Broadphase/narrowphase)
3. **T0 count** - More than 20 T0 bodies? (High-fidelity overload)
4. **Active count** - More than 5,000 active bodies? (CPU time pressure)
5. **Wake rate** - Bodies waking faster than sleeping? (Stability issue)
6. **Cache hit rate** - Manifold cache misses > 20%? (Warmstarting failing)

**Tracy zones to watch:**
```
Phys.Solve.IteratingTGS      > 600 µs → Island size problem
Phys.Narrow.TestingCollisions > 300 µs → Pair count problem
Phys.Broad.FindingPairs      > 150 µs → Spatial index problem
Phys.Barrier.*               > 100 µs → Job scheduling problem
```

---

## 6. Summary

**Memory:** ~1.4 KB per active rigid body (248 B persistent + 1.2 KB transient)

**Primary bottleneck:** TGS Solve (30% of tick, scales with largest island size)

**Secondary bottleneck:** Narrowphase (14% of tick, scales with pair count)

**Safe operating envelope:**
- Active bodies: ≤ 150
- Largest island: ≤ 100 constraints
- Collision pairs: ≤ 1500
- T0 bodies: ≤ 15

**For rich open world:** Expect ~1.4-2.0 ms per tick with proper tiering and
parallelization. Budget 1.5 ms, reserve 0.5 ms for spikes.
