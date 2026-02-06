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

**Note:** Transient memory is reused each tick (arena reset), so it doesn't
accumulate. The "working set" is what must fit in cache for good performance.

### 1.4 Memory Pressure Thresholds

| Threshold | Body Count | Implication |
|-----------|------------|-------------|
| L2 cache (256 KB) | ~200 bodies | Hot path stays in L2 |
| L3 cache (8 MB) | ~5,500 bodies | Hot path stays in L3 |
| Frame arena (4 MB) | ~3,300 bodies | Arena overflow risk |
| Total budget (12 MB) | ~8,000 bodies | Hard limit |

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

### 2.3 The Primary Bottleneck: TGS Solve

The TGS solver consumes 30% of the physics tick and has the worst scaling
characteristics in dense scenarios.

**Why TGS is problematic:**

1. **Sequential within islands** - Gauss-Seidel iteration requires each
   constraint to see the result of the previous. No parallelism within
   an island.

2. **Scales with largest island** - 100 bodies in 1 island is 100× slower
   than 100 bodies in 100 islands.

3. **Iteration multiplier** - Each constraint processed 8-24 times per
   substep. With 2 substeps, that's 16-48 passes over the constraint set.

**TGS time formula:**
```
T_solve ≈ max(island_sizes) × iterations × constraint_time
        ≈ max_island × 16 × 0.5 µs
        ≈ max_island × 8 µs
```

| Max Island Size | TGS Time | % of 1.5ms Budget |
|-----------------|----------|-------------------|
| 10 constraints | 80 µs | 5% |
| 50 constraints | 400 µs | 27% |
| 100 constraints | 800 µs | 53% |
| 200 constraints | 1.6 ms | 107% ❌ |
| 500 constraints | 4.0 ms | 267% ❌ |

**Conclusion:** Keep maximum island size under 150 constraints to stay in budget.

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
- 200 manifolds × 3 constraints = 600 constraints in one island
- TGS time: 600 × 16 iterations × 0.5 µs = 4.8 ms ❌

**Mitigation:**
- Position-based stabilization for resting stacks
- Aggressive sleep detection
- Island splitting when velocity disparity detected
- Tier demotion: only T0 bodies (player-touched) get full solve

#### 3.1.2 The Explosion

**Setup:** Grenade explodes near 100 physics props.

**Problem:**
- All 100 props receive impulse simultaneously
- All wake from sleep at once
- Massive spike in active body count
- Broadphase/narrowphase spike as all AABBs overlap

**Timeline:**
- Frame 0: 100 bodies wake, 500+ pairs detected
- Frame 1-5: Bodies separating, pair count still high
- Frame 6+: Bodies spread out, performance recovers

**Mitigation:**
- Stagger wake impulses over 2-3 frames
- Radial wake: center bodies first, edge bodies delayed
- Temporary iteration reduction during explosion
- Budget cap: skip low-priority pairs if over budget

#### 3.1.3 The Ragdoll Pile

**Setup:** 10 ragdolls (15 bodies each) land in a pile.

**Problem:**
- 150 bodies in one location
- Each ragdoll has internal joints + inter-ragdoll contacts
- Single island with ~400 constraints
- Ragdoll joint constraints are stiff (need high iterations)

**Mitigation:**
- Ragdoll simplification at distance (reduce to 5 bodies)
- Separate "ragdoll tier" with reduced iterations
- Limit ragdoll count in pile (oldest despawn first)
- LOD: distant ragdolls become static meshes

#### 3.1.4 The Conveyor Belt

**Setup:** 200 boxes on a moving conveyor, all touching.

**Problem:**
- Continuous chain of contacts = one giant island
- Never sleeps (constant motion)
- 200 bodies × 2 contacts each = 400 manifolds = 1200 constraints
- Sustained high load every frame

**Mitigation:**
- Kinematic conveyor (not simulated, applies velocity directly)
- Island breaking at conveyor segment boundaries
- Reduced iteration count for conveyor-touching objects
- Hybrid: simulate only boxes near player, fake the rest

#### 3.1.5 The Rain of Debris

**Setup:** Ceiling collapses, 500 small debris pieces fall.

**Problem:**
- 500 simultaneous spawns
- All in freefall = minimal contacts initially
- But spatial index update for 500 bodies = slow
- As they land: massive contact spike

**Mitigation:**
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

**Island structure:**
- Player cluster: 1 island, 5-15 bodies, 20-50 constraints
- Traffic vehicles: 5 islands, 1 body each, 0 constraints
- Active ragdolls: 3 islands, 15 bodies each, 40 constraints each
- Scattered debris: 10 islands, 1-3 bodies each, 0-5 constraints each

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
Island build:         200 constraints × 0.3 µs = 60 µs
TGS solve:            See below
Integrate:            70 active × 0.5 µs = 35 µs
Cache commit:         80 manifolds × 0.3 µs = 24 µs

TGS solve (per island):
  Player cluster:     50 constraints × 16 iter × 0.5 µs = 400 µs
  Ragdoll 1:          40 constraints × 12 iter × 0.5 µs = 240 µs
  Ragdoll 2:          40 constraints × 12 iter × 0.5 µs = 240 µs
  Ragdoll 3:          40 constraints × 12 iter × 0.5 µs = 240 µs
  Small islands (×10): 5 constraints × 8 iter × 0.5 µs × 10 = 200 µs
  Subtotal TGS (parallel): max(400, 240+240+240, 200) ≈ 720 µs sequential
                           ≈ 400 µs with 4 workers (islands parallel)

Substep total: ~1.0 ms (single-threaded), ~0.7 ms (parallel)
Per-tick (2 substeps): ~2.0 ms (single-threaded), ~1.4 ms (parallel)
```

**Conclusion for rich world:**
- Single-threaded: ~2.0 ms/tick (above 1.5 ms budget, needs optimization)
- Parallel (4 workers): ~1.4 ms/tick (within budget) ✓
- Headroom for spikes: ~0.5 ms

#### 3.3.2 Maximum Object Counts (Within Budget)

For a 1.5 ms physics tick budget:

| Scenario | Max Active Bodies | Max Constraints | Notes |
|----------|-------------------|-----------------|-------|
| Sparse outdoor | 150 | 300 | Many small islands |
| Dense indoor | 80 | 400 | Fewer bodies, more contacts |
| Combat (explosions) | 60 | 350 | High pair count |
| Ragdoll heavy | 45 | 500 | 3 full ragdolls max |
| Vehicle physics | 100 | 250 | Stiff wheel constraints |
| Puzzle physics | 50 | 600 | Large connected mechanism |

**Hard limits for stability:**
- Maximum island size: 150 constraints (TGS bottleneck)
- Maximum pair count: 2000 pairs (narrowphase bottleneck)
- Maximum T0 bodies: 20 (high-fidelity budget)
- Maximum active bodies: 200 (memory + iteration budget)
- Maximum total bodies: 8000 (pool capacity)

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

| Tier | Max Bodies | Iterations | Substeps | Notes |
|------|------------|------------|----------|-------|
| T0 | 20 | 24 | 3 | Player interaction only |
| T1 | 40 | 16 | 2 | Within arm's reach |
| T2 | 60 | 12 | 2 | Visible, potentially hazardous |
| T3 | 80 | 8 | 1 | Far but consequential |
| T4 | 200 | 4 | 0.5 (amortized) | Background, aggressive sleep |
| T5 | ∞ | 0 | 0 | Sleeping, event-driven wake |

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

1. **Island sizes** - Is there one huge island? (TGS bottleneck)
2. **Pair count** - Pair count vs body count ratio > 10? (Broadphase/narrowphase)
3. **T0 count** - More than 20 T0 bodies? (High-fidelity overload)
4. **Active count** - More than 200 active bodies? (Memory pressure)
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
