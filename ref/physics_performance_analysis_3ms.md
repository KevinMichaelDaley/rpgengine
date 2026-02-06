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

**Note:** Transient memory is reused each tick (arena reset), so it doesn't
accumulate. The "working set" is what must fit in cache for good performance.
With a 3 ms budget the CPU-time hard limit is ~10,000 active bodies, which
requires only ~14.5 MB of working set.

### 1.4 Memory Pressure Thresholds

| Threshold | Body Count | Implication |
|-----------|------------|-------------|
| L2 cache (256 KB) | ~200 bodies | Hot path stays in L2 |
| L3 cache (8 MB) | ~5,500 bodies | Hot path stays in L3 |
| Frame arena (10 MB) | ~8,300 bodies | Arena overflow risk |
| Total budget (32 MB) | ~10,000 bodies | Hard limit (CPU + memory) |

With a 32 MB physics pool, the pool is right-sized for the 10,000 body
CPU-time ceiling. A 10,000-body scene needs ~21.5 MB (persistent + transient),
leaving ~10 MB headroom for large static BVH and manifold cache spikes.
This is 8× smaller than 256 MB, freeing memory for rendering, audio, and AI.

---

## 2. Performance Scaling Analysis

### 2.1 Stage Complexity

Percentages are of total CPU-core-time (not wall-clock). TGS (12%) and XPBD
(18%) run concurrently, so their combined wall-clock cost is max(12, 18) = 18%.
See `physics_time_budget.txt` for the canonical breakdown.

| Stage | CPU % | Complexity | Primary Scaling Factor |
|-------|-------|------------|------------------------|
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
| Island Build | 4.0% | O(c × α(n)) | T0/T1 constraint count |
| **TGS Solve (11a)** | **12.0%** | O(I × k × c/I) | Iterations × T0/T1 constraints |
| **XPBD Solve (11b)** | **18.0%** | O(n × k) | T2–T4 body count × iterations |
| Integrate | 5.0% | O(n) | Body count |
| Cache Commit | 2.0% | O(manifolds) | Manifold count |
| Buffer Swap | 0.5% | O(1) | Constant |
| Network | 8.0% | O(changed) | Changed body count |

*11a and 11b run concurrently on disjoint body sets. Wall-clock solve cost =
max(TGS, XPBD). CPU total sums to 100% across all cores.*

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

Per-operation cost assumes a ~2015-era 4 GHz CPU (i7-6700K class). Each
constraint iteration requires computing J·v (12 FMA), a lambda update, and
applying impulses to 2 bodies (12 stores). With L1-warm sequential access
(Gauss-Seidel pattern), this costs ~0.3–0.5 µs. We budget **0.5 µs** (worst
case: box-box with friction rows, cold cache line on island transition).
With constraint specialization (§4.4), the blended average drops to
**~0.32 µs** for typical gameplay mixes (30% planar, 20% sphere, 50% generic).

```
T_tgs ≈ max(T0/T1 island_sizes) × iterations × constraint_time
      ≈ max_island × 22 × 0.5 µs       (generic, conservative)
      ≈ max_island × 22 × 0.32 µs      (blended, with specialization)
```

| Max T0/T1 Island Size | TGS (generic) | TGS (blended) | % of 3.0ms |
|------------------------|---------------|---------------|------------|
| 10 constraints | 110 µs | 70 µs | 2% |
| 50 constraints | 550 µs | 352 µs | 12% |
| 100 constraints | 1.1 ms | 704 µs | 23% |
| 150 constraints | 1.65 ms | 1.06 ms | 35% |

Note: scenarios below use the **generic 0.5 µs** cost for conservative
budgeting. The blended cost represents achievable savings after
specialization is implemented.

**Jacobi XPBD (T2–T4): embarrassingly parallel**

XPBD does not use islands. Bodies are processed independently (Jacobi pattern).
Scaling is linear in body count and parallelizes across all available cores.
Per-iteration cost is ~0.3 µs for generic constraints (position-level
evaluation, gradient, Δλ, and correction accumulation). With sphere
simplification at T2+ (§4.4), qualifying bodies drop to ~0.1 µs/iteration.

Iteration counts vary by tier: T2=8, T3=4, T4=2. For budgeting, we use
a weighted average based on expected tier mix. In typical scenarios, T2
bodies dominate the active XPBD set, so we use 6 iterations as a
conservative blend (less than T2's 8, accounting for cheap T3/T4 bodies).

```
T_xpbd ≈ total_constraints × avg_iterations × cost_per / threads
       ≈ constraints × 6 × 0.3 µs / 8      (generic blend)
       ≈ constraints × 6 × 0.22 µs / 8     (with ~40% sphere simplification)
```

XPBD parallelizes per-body, not per-island. For small body counts, job
dispatch overhead (~5 µs) means single-threading is faster. The table uses
min(serial, parallel) for each row.

| T2–T4 Bodies | Constraints (est) | XPBD (generic) | XPBD (sphere blend) | % of 3.0ms |
|--------------|-------------------|-----------------|---------------------|------------|
| 200 | ~400 | 90 µs | 66 µs | 2% |
| 1000 | ~2,000 | 450 µs | 330 µs | 11% |
| 5000 | ~10,000 | 2.25 ms | 1.65 ms | 55% |
| 8000 | ~16,000 | 3.6 ms ❌ | 2.64 ms ✓ | 88% |
| 10000 | ~20,000 | 4.5 ms ❌ | 3.3 ms ❌ | 110% ❌ |

**Combined solver budget:**  
T_solve = max(T_tgs, T_xpbd) because they run concurrently.  
Typical case: 100 near-field constraints + 1000 far-field bodies (~2000 constraints)
→ max(1.1ms TGS, 330µs XPBD) ≈ 1.1 ms (37%).

**Conclusion:** With sphere simplification and reduced T3/T4 iterations, the
XPBD ceiling rises from ~5000 to ~8000 active far-field bodies before hitting
budget. TGS on the largest island remains the dominant cost in near-field-heavy
scenarios, but constraint specialization (§4.4) and occlusion demotion (§4.5)
reduce the effective island size dramatically. The practical ceiling for
simultaneous active bodies is ~8000–8500 with the full optimization stack.

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

At ~2.0 µs per pair (including AABB + narrowphase, on a ~2015-era 4 GHz CPU;
sphere-sphere ~0.3 µs, box-box SAT+clip ~1.5 µs, average ~1.0 µs with
~1.0 µs for cache misses and dispatch overhead), budget limits:
- 3.0 ms budget × 14% = 420 µs for narrowphase
- 420 µs / 2.0 µs = ~210 pairs per substep (serial)
- Parallel (8 threads): ~1,680 pairs per substep

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
| **Background props (T4)** | **150** | **Slow-ticked: loose trash, distant shutters, swaying pipes** |
| **Total dynamic** | **~570** | |

**Typical frame state:**
| Tier | Body Count | Constraints | Notes |
|------|------------|-------------|-------|
| T0 (Direct) | 3-10 | 20-50 | Player + held objects |
| T1 (Near) | 15-30 | 50-100 | Same room / nearby |
| T2 (Visible) | 40-80 | 100-200 | On-screen, next room |
| T3 (World) | 20-50 | 40-100 | Active but distant |
| T4 (Background) | 100-150 | 0-20 | Minimal sim: wind-sway, settling debris |
| T5 (Sleeping) | 200-300 | 0 | No simulation |

T4 bodies are the key to visual richness at low cost: a pipe that rocks in
the wind, a shutter that bangs against its frame, trash that slowly slides
down a slope. They run at amortized 10 Hz (every 3rd tick), use XPBD with
2 iterations and sphere colliders where possible, with high compliance
(1e-4), and typically have 0 contacts (freefall or single resting contact).
They cost ~0.05 µs/body/tick amortized.

**Island structure (T0/T1 only — T2+ use XPBD, no islands):**
- Player cluster: 1 island, 10-25 bodies, 30-80 constraints (TGS)
- Active ragdolls near player: 2-3 islands, 15 bodies each, 40 constraints (TGS)

**XPBD body set (T2–T4):**
- Traffic vehicles: 10 bodies, 0 constraints
- Distant ragdolls: 30-60 bodies, 80-160 constraints
- Scattered debris: 20-50 bodies, 0-30 constraints
- Background props (T4, amortized): 100-150 bodies, 0-20 constraints

**Performance breakdown (per tick, 2 substeps, 8-thread parallel):**

All costs below are per-tick totals. Stages 3–13 run per substep but the
per-body costs shown are amortized over the full tick (cost × 2 substeps ÷ N
threads for parallelizable stages). Target CPU: ~2015-era 4 GHz (i7-6700K).

```
Tier classification:  570 bodies × 0.1 µs = 57 µs       (once/tick)
Spatial update:       570 bodies × 0.2 µs = 114 µs      (once/tick)
Halo closure:         10 T0 bodies × 10 µs × 2 = 200 µs (per substep)
AABB update:          180 active × 0.3 µs × 2 = 108 µs  (per substep)
Broadphase:           180 active × 0.5 µs × 2 = 180 µs  (per substep)
Narrowphase:          300 pairs × 2.0 µs × 2 / 8 = 150 µs (parallel)
Manifold build:       160 manifolds × 1.0 µs × 2 / 4 = 80 µs (parallel)
Stabilization:        160 manifolds × 0.5 µs × 2 / 4 = 40 µs (parallel)
Constraint build:     400 constraints × 0.5 µs × 2 / 4 = 100 µs (parallel)
Island build:         160 T0/T1 constraints × 0.3 µs × 2 = 96 µs (sync)
Solver:               See below
Integrate:            180 active × 0.5 µs × 2 / 4 = 45 µs (parallel)
Cache commit:         160 manifolds × 0.3 µs × 2 = 96 µs (sync)

TGS solve (T0/T1, per island, 2 substeps):
  Player cluster:     80 constraints × 22 iter × 0.5 µs × 2 = 1.76 ms
  Near ragdoll 1:     40 constraints × 20 iter × 0.5 µs × 2 = 0.80 ms
  Near ragdoll 2:     40 constraints × 20 iter × 0.5 µs × 2 = 0.80 ms
  Subtotal TGS (parallel across islands): max(1.76, 0.80+0.80) ≈ 1.76 ms

XPBD solve (T2–T4, parallel over bodies, 2 substeps):
  160 T2–T4 bodies, ~320 constraints × 6 iter × 0.3 µs × 2 / 8 = 144 µs

Solver total: max(1.76 ms, 144 µs) ≈ 1.76 ms (TGS dominates, but bounded)
Stages 11a+11b run concurrently.

Non-solver total: ~1.27 ms
Per-tick total: ~1.27 + 1.76 = ~3.0 ms (just at budget)
```

**Conclusion for rich world:**

The 570-body reference scene is tight at 3.0 ms with 80 T0/T1 constraints.
This represents a "player standing next to a big pile" worst case for the
reference world. Mitigations:
- Sleep-stabilize resting barricade contacts → 50 TGS constraints → 1.1 ms TGS
- That brings total to ~2.4 ms (20% headroom) ✓
- The 150 background props (T4) add only ~8 µs amortized — essentially free
- Parallel (4+ workers): ~2.0 ms/tick (33% headroom) ✓

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
- Maximum total bodies: 10,000 (32 MB pool capacity, CPU-matched)

---

## 4. Optimization Recommendations

### 4.1 Architectural Mitigations

| Problem | Mitigation | Implementation |
|---------|------------|----------------|
| Large islands | Island splitting | Detect velocity clusters, break weak contacts |
| Single large island dominates | Island splitting + sleep-stabilize | Break monolithic island at weak contacts, sleep resting pairs |
| Dense broadphase | Spatial hashing | Tune cell size to average body size |
| Pair explosion | Pair budget cap | Skip low-priority pairs when over budget |
| Ragdoll piles | LOD simplification | Reduce body count at distance |
| Wake storms | Staggered wake | Spread impulses over multiple frames |
| Generic constraint overhead | Constraint specialization | Planar + sphere fast-paths (§4.4) |
| Occluded objects at full fidelity | Occlusion-based tier demotion | Visibility-driven T3+ demotion (§4.5) |

### 4.2 Tier System Tuning

| Tier | Max Bodies | Solver | Iterations | Substeps | Collider | Notes |
|------|------------|--------|------------|----------|----------|-------|
| T0 | 30 | TGS | 24 | 3 | Full | Player interaction only |
| T1 | 100 | TGS | 20 | 2 | Full | Same room / few seconds' walk; visible through windows |
| T2 | 1,000 | XPBD | 8 | 1 | Full (sphere if ratio<1.3) | Visible but not immediate; next room if door open |
| T3 | 4,000 | XPBD | 4 | 1 | Sphere if ratio<1.3 | Far or occluded but consequential |
| T4 | 20,000 | XPBD | 2 | 0.5 (amortized) | Sphere preferred | Background, aggressive sleep |
| T5 | ∞ | — | 0 | 0 | — | Sleeping, event-driven wake |

**T1 boundary clarification:** T1 is not "arm's reach" — it is the set of
objects the player could plausibly interact with within a few seconds. This
includes everything in the same smallish room, large objects visible through
a window or doorway, and objects the player is walking toward. Objects in the
next room (behind a wall/corner) are T2 at closest. This means T1 can contain
15–40 bodies in a cluttered room, but occlusion culling (§4.5) aggressively
keeps hidden objects out of T1 even if they are physically close.

### 4.3 Budget Allocation by Game Type

| Game Type | Physics Budget | Focus Area |
|-----------|----------------|------------|
| Racing | 2.0 ms | Vehicle constraints, simple collisions |
| FPS | 3.0 ms | Ragdolls, destruction debris |
| Puzzle | 3.0 ms | Complex mechanisms, precision |
| Open World | 3.0 ms | Tiering, LOD, aggressive culling |
| Fighting | 2.0 ms | Character physics, low prop count |

### 4.4 Constraint Specialization

The generic TGS constraint processes 3 Jacobian rows per contact point
(1 normal + 2 friction tangents), each requiring J·v evaluation (12 FMA),
lambda update, and impulse application (12 stores). At 0.5 µs per constraint
iteration, this is the correct cost for the general case (arbitrary
convex-convex with friction). But many common cases are much cheaper:

**Planar contacts (coplanar manifold points):**

When all manifold points on a pair share the same face normal — which is the
common case for box-on-box, box-on-floor, and any flat-surface stacking —
the constraint can be specialized:

- Normal Jacobian J_v = ±n is identical for all contact points. Only J_w
  differs (different lever arms r × n per point).
- Friction tangent basis is computed once, not per-point.
- The normal solve can be batched: one aggregate normal impulse for the
  entire manifold, plus per-point angular residual corrections.

A 4-point box-on-box contact drops from 12 Jacobian rows to ~4 effective
rows (1 shared normal, 2 shared friction, ~1 aggregate angular correction):

| Contact type | Generic rows | Specialized rows | Cost/iter | Speedup |
|-------------|-------------|-----------------|-----------|---------|
| 4-pt box-box planar | 12 | ~4 | ~0.17 µs | 3× |
| 2-pt edge-edge | 6 | 6 (no specialization) | 0.50 µs | 1× |
| 1-pt vertex | 3 | 3 (no specialization) | 0.50 µs | 1× |

**Planar sleep propagation:** The bigger win is causal. If a planar contact
is below a velocity threshold, the entire island subtree below it in the
contact graph can remain sleeping. A stack of 5 boxes: the bottom 4 can
sleep if the planar contact between box 4 and box 5 (the disturbed one) is
low-velocity. This removes constraints from the solve entirely, not just
cheapening them.

A 5-box stack disturbed at the top:
- Generic: 5 bodies × 4 contacts × 3 rows = 60 rows per iteration
- Planar + sleep: 1 body active + 1 planar contact = ~4 rows per iteration
- **15× reduction** in the best case (deep stable stacks)

**Sphere-sphere contacts:**

When both colliders are spheres (or dynamically simplified to spheres at T2+,
see below), the constraint simplifies dramatically:

- 1 manifold point per intersection (guaranteed for sphere-sphere)
- Jacobian is trivial: J_v = ±n, J_w = ±(radius × n) — no cross products
  needed because the lever arm is always radial
- Effective mass is scalar (diagonal inertia for sphere)
- Friction simplifies to rolling friction (1 scalar row, not 2 tangent rows)

| Contact type | Generic rows | Specialized rows | Cost/iter | Speedup |
|-------------|-------------|-----------------|-----------|---------|
| Sphere-sphere | 3 | 1–2 | ~0.08 µs | 6× |

**Dynamic sphere simplification at T2+:**

Small objects with a bounding-sphere ratio (circumradius / inradius) below
~1.3 can be dynamically treated as sphere colliders when demoted to T2 or
below. This switches both the narrowphase (sphere-sphere instead of GJK/SAT)
and the constraint solver to the fast path.

Good candidates: rocks, skulls, potions, rubble chunks, small crates, bones.
Bad candidates: pipes (ratio ~2.0), planks, bottles (ratio ~1.5+).

The ratio can be precomputed at asset load time and stored as a flag on
the collider. The tier classifier checks the flag when demoting to T2.

**Blended constraint cost:**

For typical March of Glaciers scenarios, the constraint mix is approximately:
- 30% planar contacts (barricade stacks, floor resting, wall leans)
- 20% sphere-simplifiable (skulls, rocks, small debris at T2+)
- 50% generic (ragdolls, capsule contacts, mixed convex pairs)

Blended cost per constraint iteration:
`0.30 × 0.17 + 0.20 × 0.08 + 0.50 × 0.50 = 0.051 + 0.016 + 0.25 = **0.32 µs**`

This is a **36% reduction** from the generic 0.5 µs baseline, before
planar sleep propagation. With sleep propagation on stable stacks, effective
constraint counts drop further (see scenario rework in §6).

### 4.5 Occlusion-Based Tier Demotion

Objects that are physically nearby but not visible to the player can be
aggressively demoted to T3+ with reduced collider fidelity and iteration
counts. They only need to:

1. **Sound right** — collision events still fire for audio triggers
2. **Fall at the right speed** — gravity integration is tier-independent
3. **Land where they should** — post-settlement nudge corrects final position

The key insight: if the player can't see it, the solver only needs to produce
a *plausible* result, not a *precise* one. By the time the player rounds
the corner or looks through the window, the objects have settled and can
receive a small position correction to match the "correct" resting state.

**What the renderer already knows:**

The rendering pipeline performs occlusion culling every frame. This produces
a visibility set that physics can consume directly in Stage 1 (tier
classification). The classification algorithm becomes:

```
base_tier = classify_by_distance(body, player)
if (base_tier <= T1 && !visibility_set.contains(body)):
    effective_tier = max(base_tier, T3)  // demote to at least T3
    // Keep T3 (not T4) so collision events still fire for audio
```

The hysteresis rule still applies: a body demoted by occlusion stays demoted
for K frames (e.g., 5 frames) to prevent flapping when the player's view
sweeps across occluders.

**What changes at T3 for occluded objects:**

| Parameter | T1 (visible) | T3 (occluded) | Savings |
|-----------|-------------|---------------|---------|
| Solver | TGS (island) | XPBD (parallel) | Island overhead eliminated |
| Iterations | 20 | 6 | 3.3× fewer iterations |
| Substeps | 2 | 1 | 2× fewer substeps |
| Collider | Full (box/capsule) | Sphere (if ratio < 1.3) | ~6× cheaper narrowphase |
| Manifold points | 4 | 2 | Fewer constraint rows |
| Constraint cost | 0.50 µs/iter | 0.08–0.17 µs/iter | 3–6× cheaper |

**Net effect:** A body that would cost `20 iter × 0.5 µs × 2 substeps = 20 µs`
at T1 costs `6 iter × 0.08 µs × 1 substep = 0.48 µs` at occluded-T3 with
sphere simplification. That's a **42× reduction per body**.

**Post-settlement position correction:**

When an occluded body becomes visible (player rounds corner), it may be in
a slightly wrong resting position (XPBD with 6 iterations is less precise
than TGS with 20). The correction is:

1. On promotion to T1, compare current position against manifold contact
   points. If penetration > 2mm, apply a gentle positional nudge over 3–5
   frames (lerp toward the contact surface at 1mm/frame).
2. The nudge is invisible because:
   - The player is just arriving — no prior visual reference
   - The nudge is < 5mm total, below perceptual threshold
   - Objects are settling anyway (post-combat debris)

**Scenario impact (Gang War Phase 1 — player observing from entrance):**

The player is looking down a tunnel at the battle. Most combatants and
barricade pieces are behind partial cover, pillars, or at oblique angles.
With occlusion culling:

- T1 bodies: 8 → 3 (only objects in direct line of sight)
- Occluded-to-T3: 5 bodies that were T1 by distance, now T3 by visibility
- T2 bodies: 60 → 40 (20 demoted to T3 by occlusion)
- T3 (including demoted): 100 + 5 + 20 = 125

TGS constraints: 30 → 12 (only 3 T1 bodies form islands)
TGS cost: 12 × 22 × 0.5 × 2 = 264 µs (was 660 µs, **60% reduction**)

**Combined with constraint specialization:** If those 12 TGS constraints
are planar (barricade face), cost drops to 12 × 22 × 0.17 × 2 = 90 µs.
Total tick: ~1.1 ms (was 1.9 ms) — **63% headroom**.

**Level design synergy:**

The subterranean setting of March of Glaciers is ideal for occlusion:
- Tunnel geometry naturally occludes the next room
- Corners and doorways create hard occlusion boundaries
- Multi-level chambers hide objects above/below
- Barricades themselves are occluders (objects behind a barricade the
  player hasn't reached yet are invisible)

Level designers can place occluders intentionally to create physics budget
boundaries — a heavy door or a corner becomes a "physics LOD gate" where
everything behind it drops to T3+ until the player approaches.

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
- 50–80 background props (T4): dripping pipes, swaying cables, loose grating

**Medium Clutter** — explored area with some player fortification:
- 120–250 dynamic bodies (scrap piles, crates, barricade pieces, skulls)
- 3–8 ragdolls (killed enemies, creatures)
- 1–2 active fires (heat source, light source)
- 30–60 bodies active
- 80–120 background props (T4): rattling vents, rocking loose tiles, swinging chains

**High Clutter** — heavily fortified holdout or post-combat aftermath:
- 300–600 dynamic bodies (dense barricade, debris fields, scattered props)
- 8–15 ragdolls
- 2–4 fires
- 80–200 bodies active (barricade under stress, settling debris)
- 100–150 background props (T4): stress-creaking supports, settling rubble, dripping condensation

### 6.3 Per-Archetype CPU Analysis

All numbers assume 30 Hz tick rate, 2 substeps, 8-thread parallel.
Budget target: 3.0 ms/tick (with 1.0 ms spike headroom).
Per-operation costs target a ~2015-era 4 GHz CPU (i7-6700K / Ryzen 1600).
All costs are per-tick totals (substep stages ×2, parallel stages ÷threads).

#### 6.3.1 Narrow Tunnel — Low Clutter

```
Static BVH nodes traversed:   ~50 (simple corridor)
Active bodies:                 12 (player kicks debris, loose pipes)
Background props (T4):         60 (dripping, swaying, amortized 10Hz)
T0/T1:                        5 (player + nearby objects)
T2–T4:                        67 (scattered pipes, panels, background)
Collision pairs:               20
TGS constraints:               10 (1 small island)
XPBD constraints:              8

TGS:   10 × 22 × 0.5 µs × 2  = 220 µs
XPBD:  8 × 6 × 0.3 µs × 2/8  =   4 µs
Narrow: 20 × 2.0 µs × 2 / 8  =  10 µs
Broad:  72 × 0.5 µs × 2       =  72 µs
T4 amortized: 60 × 0.05 µs    =   3 µs
Other stages:                   ~100 µs
                               ─────────
Total:                          ~0.4 ms/tick ✓✓✓ (87% headroom)
```

**Risk:** None. Trivial load. Background props add ~6 µs — invisible.

#### 6.3.2 Hub Chamber — Medium Clutter

```
Static BVH nodes traversed:   ~200 (multi-level room)
Active bodies:                 55 (barricade pieces, debris, 4 ragdolls)
Background props (T4):         100 (rattling vents, swinging chains)
T0/T1:                        18 (player area: fire, held object, nearby scrap)
T2–T4:                        137 (visible crates, distant ragdolls, background)
Collision pairs:               150
TGS constraints:               70 (player cluster + 2 ragdolls near player)
XPBD constraints:              90

TGS:   70 × 22 × 0.5 µs × 2  = 1.54 ms
XPBD:  90 × 6 × 0.3 µs × 2/8 =  40 µs (parallel)
Narrow: 150 × 2.0 µs × 2 / 8 =  75 µs (parallel)
Broad:  155 × 0.5 µs × 2      = 155 µs
T4 amortized: 100 × 0.05 µs   =   5 µs
Other stages:                   ~350 µs
                               ─────────
Total:                          ~2.2 ms/tick ✓ (27% headroom)
```

**Risk:** TGS at 1.54 ms is the dominant cost — one large near-field island
(player + ragdoll pile). Monitor `Phys.Solve.IteratingTGS`. With sleep-stabilize
on the barricade pieces (−30 constraints), TGS drops to ~0.88 ms → 1.5 ms total ✓.
Background props (100 T4 bodies) add only ~10 µs — trivial.

#### 6.3.3 Trash Drop Zone — High Clutter (Spike Event)

A trash drop is the worst-case physics spike: 80–150 new objects fall from
above simultaneously, bounce off geometry and each other, then settle.

```
SPIKE FRAME (Frame 0 of drop):
Active bodies:                 200 (130 new drop + 70 existing)
Background props (T4):         80 (ambient, unaffected by drop)
T0/T1:                        20 (player + nearby falling objects)
T2–T4:                        260 (most drop objects are far-field + background)
Collision pairs:               1000 (dense overlap during fall)
TGS constraints:               80 (near-field contacts)
XPBD constraints:              700 (far-field pile-up)

TGS:   80 × 22 × 0.5 µs × 2    = 1.76 ms
XPBD:  700 × 6 × 0.3 µs × 2/8  = 315 µs (parallel, 8 threads)
Narrow: 1000 × 2.0 µs × 2 / 8  = 500 µs (parallel)
Broad:  280 × 0.5 µs × 2        = 280 µs
T4 amortized: 80 × 0.05 µs      =   4 µs
Other stages:                    ~350 µs
                                ─────────
Total:                           ~3.2 ms/tick ⚠️ (7% over budget)

WITH MITIGATION:
  Stagger spawn over 3 frames → peak 70 active drop objects
  TGS: 50 × 22 × 0.5 × 2 = 1.1 ms
  XPBD: 400 × 6 × 0.3 × 2/8 = 180 µs
                                ─────────
Total (mitigated):               ~2.4 ms/tick ✓ (20% headroom)

SETTLING (Frames 5–30):
Active bodies:                 130 → 30 (progressive sleep)
Pair count:                    600 → 80 (objects separate and rest)
Total:                         ~1.2 ms → 0.4 ms (recovers over 1 second)
```

**Risk:** Trash drop spike is 7% over budget without mitigation. Stagger spawn
(the simplest mitigation) brings it well under. XPBD handles the far-field
pile-up cleanly — no giant-island problem.

**Without hybrid solver:** All 700 constraints would form one TGS island.
700 × 22 × 0.5 µs × 2 = 15.4 ms ❌. The hybrid solver is essential.

#### 6.3.4 Barricaded Holdout — High Clutter (Persistent)

Player has fortified a room with stacked scrap, wedged doors, skull anchors,
and a fire. Enemies are pushing against the barricade.

```
Active bodies:                 140 (60 barricade, 50 loose props, 15 enemy, 15 ragdoll)
Background props (T4):         60 (creaking beams, loose ceiling tiles)
T0/T1:                        30 (barricade face + player + fire + enemy bodies)
T2–T4:                        170 (back of barricade, distant debris, ragdolls, background)
Collision pairs:               450
TGS constraints:               140 (dense barricade stack near player)
XPBD constraints:              200

TGS:   140 × 22 × 0.5 µs × 2    = 3.08 ms  ❌ (over budget alone!)
XPBD:  200 × 6 × 0.3 µs × 2/8   =  90 µs
Narrow: 450 × 2.0 µs × 2 / 8    = 225 µs
Broad:  200 × 0.5 µs × 2         = 200 µs
T4 amortized: 60 × 0.05 µs       =   3 µs
Other stages:                     ~400 µs
                                 ─────────
Total (no mitigation):            ~3.9 ms/tick ❌

WITH SLEEP-STABILIZE (barricade contacts resting > 0.5s → sleep):
  TGS: 80 × 22 × 0.5 × 2 = 1.76 ms
                                 ─────────
Total (mitigated):                ~2.7 ms/tick ✓ (10% headroom)
```

**Risk:** This is the hardest steady-state scenario. 140 TGS constraints
blows the budget — sleep-stabilize is **mandatory**, not optional. The
barricade stack has many resting contacts that can safely sleep. With
stabilization, TGS drops to 80 constraints (1.76 ms) and total fits.

**If barricade is being attacked:** Enemy pushes wake sleeping contacts,
island grows temporarily. Could spike to 120 TGS constraints = 2.64 ms TGS.
Combined with other stages: ~3.6 ms ❌. Requires island splitting (see §6.5).

#### 6.3.5 Creature Nest — Medium Clutter

Organic geometry with skull piles, creature bodies, and irregular shapes.

```
Active bodies:                 70 (skull pile, creature ragdolls, loose bones)
Background props (T4):         80 (dangling stalactites, slow-dripping fluids)
T0/T1:                        12 (player + nearby skulls being kicked/thrown)
T2–T4:                        138 (distant skull piles, creature bodies, background)
Collision pairs:               180
TGS constraints:               40 (small near-field contacts)
XPBD constraints:              130

TGS:   40 × 22 × 0.5 µs × 2  = 0.88 ms
XPBD:  130 × 6 × 0.3 µs × 2/8=  58 µs
Narrow: 180 × 2.0 µs × 2 / 8 =  90 µs
Broad:  150 × 0.5 µs × 2      = 150 µs
T4 amortized: 80 × 0.05 µs    =   4 µs
Other stages:                   ~300 µs
                               ─────────
Total:                          ~1.5 ms/tick ✓ (50% headroom)
```

**Risk:** Low. Skull colliders may be complex (horns, cavities), increasing
per-pair narrowphase cost. Convex decomposition quality matters here.

### 6.4 Clutter Scaling Summary

| Archetype | Low | Medium | High | Spike |
|-----------|-----|--------|------|-------|
| Narrow Tunnel | 0.4 ms ✓ | 0.8 ms ✓ | 1.4 ms ✓ | — |
| Hub Chamber | 0.5 ms ✓ | 2.2 ms ✓ | 2.8 ms ✓ | — |
| Trash Drop Zone | 0.5 ms ✓ | 1.2 ms ✓ | 2.0 ms ✓ | 3.2→2.4 ms ⚠️ |
| Barricaded Holdout | 0.5 ms ✓ | 1.6 ms ✓ | 3.9→2.7 ms ⚠️ | 3.6 ms ❌ |
| Creature Nest | 0.5 ms ✓ | 1.5 ms ✓ | 2.2 ms ✓ | — |

⚠️ = requires automatic mitigation (sleep-stabilize or stagger)
❌ = requires island splitting (barricade under active attack)

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
Background props (T4):         80
T0/T1:                        35 (barricade face, fire, player, enemy ragdolls)
T2–T4:                        265 (back wall, far debris, old ragdolls, background)
Collision pairs:               800
TGS constraints:               170 (barricade + ragdolls + enemy contacts)
XPBD constraints:              350

TGS:   170 × 22 × 0.5 µs × 2    = 3.74 ms  ❌ (over budget alone)
XPBD:  350 × 6 × 0.3 µs × 2/8   = 158 µs
Narrow: 800 × 2.0 µs × 2 / 8    = 400 µs
Broad:  300 × 0.5 µs × 2         = 300 µs
T4 amortized: 80 × 0.05 µs       =   4 µs
Other stages:                     ~450 µs
                                 ─────────
Total (no mitigation):            ~5.1 ms/tick ❌❌ (way over budget)
```

This worst case REQUIRES automatic mitigations — it cannot ship unmitigated.

**Recovery plan (applied in order, each reduces TGS load):**

1. **Sleep-stabilize barricade** (−50 constraints): pieces resting > 0.5s
   become sleeping contacts. TGS: 120 constraints × 22 × 0.5 × 2 = 2.64 ms.
2. **Ragdoll LOD** (−30 constraints): enemy ragdolls at T0 drop from 15
   to 5 bodies. TGS: 90 constraints × 22 × 0.5 × 2 = 1.98 ms.
3. **Island split** (−concurrent): barricade island broken at velocity
   boundary. Two islands of ~45 each, solved in parallel.
   TGS: 45 × 22 × 0.5 × 2 = 0.99 ms per island, parallel → 0.99 ms total.
4. **Result:** ~0.99 + 1.0 (other stages) = ~2.0 ms/tick ✓ (33% headroom)

All mitigations are automatic (no player-visible quality change).
The island split (step 3) is the most important — without it, TGS on the
largest island dominates even after sleep-stabilize and ragdoll LOD.

### 6.8 Maximum-Scale Scenario: Gang War at the Trash Chute

This scenario pushes the engine to its design ceiling. A scripted gang war
erupts at a major trash drop site. The player approaches after the trash has
already landed (it settles a few frames before the player arrives, so no
T0/T1 spike from falling objects). The gang war is in full swing: enemies
fighting each other, barricades being destroyed, bodies accumulating.

**Setup:**
- Large multi-level chamber under a trash chute
- A trash drop of 200 objects has already settled (mostly sleeping)
- 2 rival factions fighting: 8 active combatants per side (16 total)
- Each combatant is a full 15-body ragdoll on death → bodies accumulate
- Barricades on both sides (40 pieces each, 80 total)
- Improvised explosives and thrown debris
- Player observes from a tunnel entrance, then engages

**Phase 1: Player arrives, observing from entrance (worst-case steady state)**

The player is at T1 distance (~10-15m) from the nearest combat.
Most of the battlefield is T2–T4.

```
Total bodies in scene:         600
  Trash pile (sleeping):       180 (settled, T5)
  Barricade pieces:            80 (40 per side, mixed sleeping/active)
  Active combatants:           16 × 1 body = 16 active ragdoll-ready NPCs
  Dead combatants:             6 ragdolls × 15 bodies = 90 ragdoll bodies
  Thrown debris / explosives:  20 in flight
  Loose props (pipes, crates): 50 (scattered, mostly sleeping)
  Structural debris:           40 (from barricade destruction)
  Fire/heat sources:           4 (2 per faction)

Active bodies:                 250 (everything not sleeping)
T0:                            0 (player observing)
T1:                            8 (nearest barricade, nearest combatant)
T2:                            60 (visible combatants, near ragdolls, debris)
T3:                            100 (far side of battle, distant ragdolls)
T4:                            82 (edge debris, far barricade remnants)
T5:                            350 (settled trash, sleeping props)

Collision pairs:               800
TGS constraints:               30 (small T1 cluster: nearest barricade face)
XPBD constraints:              500 (combat zone, ragdolls, flying debris)

TGS:   30 × 22 × 0.5 µs × 2     = 660 µs
XPBD:  500 × 6 × 0.3 µs × 2/8   = 225 µs (parallel)
Narrow: 800 × 2.0 µs × 2 / 8    = 400 µs (parallel)
Broad:  250 × 0.5 µs × 2         = 250 µs
Other stages:                     ~400 µs
                                 ─────────
Total:                            ~1.9 ms/tick ✓ (37% headroom)
```

**Phase 2: Player engages, enters combat zone**

Player pushes into the fight. Nearby objects shift to T0/T1.

```
Active bodies:                 280 (more wakes from player proximity)
T0:                            10 (player + held weapon + nearby debris)
T1:                            25 (surrounding barricade, 2 ragdolls, combatants)
T2:                            80 (wider combat zone)
T3:                            90 (far side)
T4:                            75
T5:                            320

Collision pairs:               1000
TGS constraints:               100 (player cluster + ragdolls + barricade)
XPBD constraints:              600

TGS:   100 × 22 × 0.5 µs × 2    = 2.2 ms
XPBD:  600 × 6 × 0.3 µs × 2/8   = 270 µs (parallel)
Narrow: 1000 × 2.0 µs × 2 / 8   = 500 µs (parallel)
Broad:  280 × 0.5 µs × 2         = 280 µs
Other stages:                     ~500 µs
                                 ─────────
Total (no mitigation):            ~3.8 ms/tick ❌

WITH SLEEP-STABILIZE (barricade contacts):
  TGS: 60 × 22 × 0.5 × 2 = 1.32 ms
                                 ─────────
Total (mitigated):                ~2.9 ms/tick ✓ (3% headroom — tight!)
```

**Phase 3: Explosion — player detonates improvised explosive**

The player throws a gas canister into a barricade. 30 objects fly outward,
3 combatants ragdoll simultaneously, fire spreads.

```
SPIKE FRAME:
Active bodies:                 350 (mass wake from explosion)
T0:                            12 (player + blast debris)
T1:                            30 (blast zone, 3 fresh ragdolls)
T2:                            120 (secondary debris, shrapnel)
T3:                            100 (far combatants react)
T4:                            88
T5:                            250 (distant trash still sleeping)

Collision pairs:               1500 (dense overlap in blast zone)
TGS constraints:               130 (player area: 3 ragdolls + blast debris)
XPBD constraints:              900 (entire visible battle)

TGS:   130 × 22 × 0.5 µs × 2    = 2.86 ms  ❌
XPBD:  900 × 6 × 0.3 µs × 2/8   = 405 µs (parallel)
Narrow: 1500 × 2.0 µs × 2 / 8   = 750 µs (parallel)
Broad:  350 × 0.5 µs × 2         = 350 µs
Other stages:                     ~550 µs
                                 ─────────
Total (no mitigation):            ~4.9 ms/tick ❌❌

WITH AUTO-MITIGATION (all three):
  Ragdoll LOD (3 ragdolls → 5 bodies each): −60 TGS constraints
  Sleep-stabilize blast debris (>0.3s): −20 TGS constraints (after 10 frames)
  Island split (2 islands of 25 each, parallel):
    TGS: 25 × 22 × 0.5 × 2 = 550 µs per island
                                 ─────────
Total (fully mitigated):          ~2.6 ms/tick ✓ (13% headroom)
```

**Phase 4: Aftermath — combat winds down**

Combatants are dead or fled. Debris settling. Player looting.

```
Active bodies:                 150 → 60 (progressive sleep over 2-3 seconds)
T0/T1:                        10 (player + loot interaction)
T2–T4:                        50 → 20
T5:                           440 → 540

Total:                         ~0.8 ms → 0.4 ms (recovers within 3 seconds)
```

**Full timeline:**

| Phase | Duration | Active | TGS | Total | Status |
|-------|----------|--------|-----|-------|--------|
| 1. Observing | 5-10s | 250 | 660 µs | 1.9 ms | ✓ 37% headroom |
| 2. Engaging | 10-30s | 280 | 2.2 ms | 3.8→2.9 ms | ⚠️ needs sleep-stab. |
| 3. Explosion spike | 0.3s | 350 | 2.86 ms | 4.9→2.6 ms | ❌→✓ full mitigation |
| 4. Aftermath | 3-5s | 150→60 | 200 µs | 0.8→0.4 ms | ✓✓ recovering |

**Body count over time:**

```
Bodies
  600 ┤·····················
      │ ╭──────╮ Phase 2    ╭─ Phase 3 spike
  400 ┤─╯      ╰────────────╯╲
      │                       ╲
  200 ┤                        ╲───── Phase 4 settling
      │                              ╲
   60 ┤                               ╰────── steady state
      └──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──
        0s  5s 10s 15s 20s 25s 30s 35s 40s
             observe  engage  boom  loot
```

**Key takeaways:**
- 600 total bodies in scene, 350 peak active: well within 10,000 hard limit
- Peak memory: ~5 MB working set (16% of 32 MB pool)
- **TGS ×2 substep is the dominant cost** — the engaging phase (Phase 2)
  needs sleep-stabilize to fit at all, and the explosion (Phase 3) needs
  all three mitigations (sleep-stab, ragdoll LOD, island split)
- Mitigations are all automatic (no player-visible quality change) but
  **are mandatory, not optional** — without them Phase 2 and 3 fail
- Trash drop settling before player arrival is essential: it means 180 bodies
  are sleeping (T5) and cost zero CPU when the player enters
- The gang war itself is the physics showcase — 16 combatants, ragdolls piling
  up, barricades shattering, explosions — and it fits in budget WITH mitigations

---

## 7. Summary

**Memory:** ~1.4 KB per active rigid body (248 B persistent + 1.2 KB transient).
32 MB pool right-sized for the 10,000 body CPU ceiling. A 600-body gang war
scene uses ~5 MB working set (16% of pool).

**Primary bottleneck:** TGS Solve — the ×2 substep multiplier makes this the
dominant cost in every near-field-heavy scenario. Only applies to T0/T1 bodies
but constraints × 22 iterations × 0.5 µs × 2 substeps accumulates fast.
With constraint specialization (§4.4), the blended cost drops to ~0.32 µs
(36% reduction). With occlusion-based demotion (§4.5), the T1 body count
shrinks further — occluded nearby bodies run at T3 XPBD instead of T1 TGS.

**Secondary bottleneck:** Narrowphase (14% of tick, scales with pair count).
At 2.0 µs/pair (conservative for 2015-era CPU), this is manageable up to
~1500 pairs before it competes with TGS. Sphere simplification at T2+ (§4.4)
reduces narrowphase cost for qualifying small objects by ~6×.

**Tertiary bottleneck:** XPBD Solve (18% of tick CPU, but parallelizes
perfectly across 8 threads and rarely dominates wall-clock even at 5000 bodies).

**Safe operating envelope (3 ms budget, 32 MB pool):**
- Active bodies: ≤ 10,000 (with tiering)
- T0/T1 bodies: ≤ 25 without specialization, ~40 with specialization + occlusion
- Largest TGS island: ≤ 60 constraints without mitigation, ≤ 130 with island split
- Collision pairs: ≤ 20,000
- Near-field constraint density: ≤ 6 constraints/body
- Total pool: 32 MB (right-sized for CPU ceiling)
- Background T4 props: up to 500 at negligible cost (~25 µs at amortized 10Hz, 2 iterations, sphere collider)

**For March of Glaciers:** Typical gameplay runs at 0.5–1.5 ms/tick. Dense
barricade holdouts reach 2.7–3.9 ms without mitigation, requiring mandatory
sleep-stabilize. With constraint specialization, the barricade holdout drops
to ~2.0–2.5 ms (planar contacts on stacked boxes are 3× cheaper). With
occlusion culling, objects behind barricades the player hasn't reached drop
to T3, further reducing TGS load. The subterranean tunnel setting is ideal
for occlusion — every corner and doorway is a natural physics LOD gate.

**Optimization stack (cumulative):**
1. Base mitigations (mandatory): sleep-stabilize, ragdoll LOD, island split
2. Constraint specialization: planar fast-path (3×), sphere fast-path (6×)
3. Occlusion demotion: nearby-but-hidden objects run at T3 XPBD (42× cheaper)
4. Level design: island-breaking gaps, merge static clutter, debris LOD

With the full stack, worst-case scenarios that currently show ❌ become
comfortably within budget. The gang war explosion spike drops from 4.9 ms
to ~2.0 ms (mitigations + specialization); with occlusion, the engaging
phase drops from 3.8 ms to ~1.8 ms (many barricade pieces occluded).

---

## 8. Tier Transition Effects

When a body crosses the T1↔T2 boundary, its constraint representation
must be converted between TGS (impulse-based) and XPBD (position-based).
This section analyzes the cost and quality impact of transitions.

### 8.1 Conversion Cost Per Body

From the call graph (Stage 9, Solver Transition box):
- **TGS → XPBD (demotion):** `λ_xpbd = λ_impulse * dt`
- **XPBD → TGS (promotion):** `λ_impulse = clamp(λ_xpbd / dt, λ_min, λ_max)`

Per-body conversion cost:
- Read current constraint state (λ, constraint Jacobian)
- Multiply/divide by dt + clamp
- Write new λ into constraint
- **Estimated: 2–5 µs per transitioning body** (includes cache line access
  for constraint + body data, ~10 FLOPs for conversion, L1/L2 hit likely
  since the body was just reclassified in Stage 1)

### 8.2 Warm-Start Quality Loss

The conversion formulas are mathematically exact under ideal conditions.
In practice, quality degrades because:

1. **TGS → XPBD:** TGS accumulates impulses over iterations; XPBD uses
   positional λ. The conversion `λ_xpbd = λ_impulse * dt` is exact for
   a single contact, but multi-body islands lose coupling information.
   **Effect:** 1–3 frames of slightly softer contacts until XPBD converges.
   This is invisible at T2+ distances.

2. **XPBD → TGS (more problematic):** XPBD λ represents positional correction.
   Converting to impulse via `λ_impulse = λ_xpbd / dt` can produce large
   warm-start values that cause TGS to overshoot on the first frame.
   The `clamp(λ_min, λ_max)` prevents explosions but wastes the warm-start.
   **Effect:** 3–5 frames of reduced solver quality (TGS reconverges from
   scratch or from clamped estimate). Visible as slight jitter if the body
   is in active contact. This matters because promotion means the body is
   now near the player.

3. **Mitigation for XPBD→TGS:** Apply a warm-start ramp factor:
   frame 0: use 50% of converted λ, frame 1: 75%, frame 2: 100%.
   This prevents overshoot while preserving directional information.
   Cost: one extra multiply per constraint per transitioning body (~negligible).

### 8.3 Transition Frequency Scenarios

| Scenario | Transitions/tick | Added cost | Quality impact |
|----------|-----------------|------------|----------------|
| Player walking through open area | 0-2 bodies/tick | 5-10 µs | Negligible |
| Player circling T1/T2 boundary | 5-10 bodies/tick | 25-50 µs | Mild jitter on edge objects |
| Explosion pushing debris outward | 10-20 bodies demoted | 50-100 µs | Invisible (demoted = far away) |
| Player charging into combat zone | 8-15 bodies promoted | 40-75 µs | 3-5 frame jitter on promoted bodies |

**Worst case: boundary flapping.** If a body oscillates across the T1/T2
boundary every tick (e.g., player circling at exactly T1 range), conversion
happens every frame and warm-start never stabilizes. Fix: hysteresis band.
A body promoted to T1 stays T1 until it exceeds T1+2m; demoted to T2 stays
T2 until it enters T1−2m. This eliminates flapping at the cost of slightly
larger T1 sets near the boundary (1–3 extra bodies, ~30 µs TGS overhead).

### 8.4 Budget Impact

Transition costs are negligible in all scenarios — even the worst case of
20 simultaneous transitions adds ~100 µs, which is 3% of the 3ms budget.
The quality impact of XPBD→TGS promotion is the real concern, and the
warm-start ramp mitigates it effectively.

---

## 9. Background Object Budget (T4 Props)

Background T4 objects provide visual richness at near-zero CPU cost.
This section quantifies the budget explicitly.

### 9.1 Per-Object Cost

T4 bodies tick at amortized 10Hz (every 3rd physics tick at 33Hz).
Each tick: XPBD with 2 iterations, sphere collider preferred, high compliance.

```
Per T4 body per physics tick (amortized):
  XPBD:  1 constraint × 2 iterations × 0.1 µs / 3 ticks = 0.067 µs
  Broad: 1 body × 0.5 µs / 3 = 0.17 µs
  Narrow: sphere-sphere ~0.3 µs × 20% active / 3 = 0.02 µs
                                                    ─────
  Total per T4 body per tick:                       ~0.05 µs (amortized)
  (varies: sleeping T4 ≈ 0, active T4 with contact ≈ 0.15 µs)
```

### 9.2 Budget Envelope

At 0.05 µs/body amortized, within a 100 µs budget slice for background:

| T4 count | Cost/tick | % of 3ms budget | Visual impact |
|----------|-----------|-----------------|---------------|
| 50 | 2.5 µs | 0.1% | Sparse — a few dangling pipes |
| 100 | 5 µs | 0.2% | Moderate — debris, stalactites, loose panels |
| 200 | 10 µs | 0.3% | Rich — every surface has loose detail |
| 500 | 25 µs | 0.8% | Lavish — full environmental simulation |
| 1000 | 50 µs | 1.7% | Extreme — densely populated world |

**Recommendation:** 200–500 T4 props per loaded area. With 2-iteration sphere
XPBD, even 500 background props cost only 25 µs (0.8% of budget). The
broadphase is now the limiting factor for T4 count: 1000 bodies × 0.5 µs
= 500 µs broadphase contribution, amortized ~170 µs/tick.

### 9.3 What Makes Good T4 Props

Good candidates for T4 background physics:
- Dangling chains, pipes, stalactites (single-body pendulums)
- Loose rubble that shifts when nearby explosions wake it
- Floating debris in flooded chambers (buoyancy = high-compliance XPBD spring)
- Cloth/tarp segments (simplified 2-4 body chain, not full cloth sim)
- Small creatures (rats, insects) — 1-body with simple collision

Bad candidates (should be static or particle effects instead):
- Dense piles of small objects (high pair count, mutual contacts)
- Anything the player might interact with (needs T0/T1 quality)
- Persistent physics puzzles (need deterministic solving → TGS)

---

## 10. Call Graph Cross-Reference

This section verifies that every stage in the performance analysis maps
correctly to a stage in the call graph (`physics_tick_callgraph.md`), and
that the timing annotations are consistent with the revised per-operation costs.

### 10.1 Stage Mapping

| Perf Analysis Stage | Call Graph Stage | CG Timing | PA Timing | Match? |
|--------------------|--------------------|-----------|-----------|--------|
| Step Plan | Stage 0: STEP PLAN [SYNC] | ~10 µs | (in "other") | ✓ |
| Tier Classification | Stage 1: BASE TIER [PARALLEL] | 20-120 µs | (in "other") | ✓ |
| Spatial Index Update | Stage 2: SPATIAL INDEX [PARALLEL] | 20-80 µs | (in "other") | ✓ |
| Halo Closure | Stage 3: HALO CLOSURE [PARALLEL] | 20-120 µs | (in "other") | ✓ |
| AABB Update | Stage 4: AABB UPDATE [PARALLEL] | 20-80 µs | (in broadphase) | ✓ |
| Broadphase | Stage 5: BROADPHASE [PARALLEL] | 40-200 µs | 0.5 µs/body | ✓ (a) |
| Narrowphase | Stage 6: NARROWPHASE [PARALLEL] | 80-350 µs | 2.0 µs/pair | ✓ (b) |
| Manifold Build | Stage 7: MANIFOLD BUILD [PARALLEL] | 25-120 µs | (in "other") | ✓ |
| Stabilization | Stage 8: STABILIZATION [PARALLEL] | 15-120 µs | (in "other") | ✓ |
| Constraint Build | Stage 9: CONSTRAINT BUILD [PARALLEL] | ~100 µs | (in "other") | ✓ |
| Island Build | Stage 10: ISLAND BUILD [SYNC] | 20-150 µs | (in "other") | ✓ |
| TGS Solve | Stage 11a: TGS [PARALLEL] | 100-400 µs | 0.5 µs/iter | ✓ (c) |
| XPBD Solve | Stage 11b: XPBD [PARALLEL, CONCURRENT] | 80-350 µs | 0.3 µs/iter | ✓ (d) |
| Integrate + Sleep | Stage 12: INTEGRATE [PARALLEL] | 20-120 µs | (in "other") | ✓ |
| Cache Commit | Stage 13: CACHE COMMIT [SYNC] | ~20 µs | (in "other") | ✓ |

**Notes:**
- (a) CG says 40-200 µs. At 0.5 µs/body: 80 bodies=40µs, 400 bodies=200µs. ✓ Matches.
- (b) CG says 80-350 µs. At 2.0 µs/pair/8 threads: 320 pairs=80µs, 1400 pairs=350µs. ✓ Matches.
- (c) CG says 100-400 µs. At 22 iter × 0.5 µs × 2 substeps: 5 constraints=110µs,
  18 constraints=396µs. ✓ Matches range (small island baseline is ~100µs).
- (d) CG says 80-350 µs. At 6 iter × 0.3 µs × 2/8: 180 constraints=81µs,
  780 constraints=351µs. ✓ Matches.

### 10.2 "Other Stages" Budget Verification

The scenarios list an "Other stages" catch-all of 300–550 µs. This should
account for Stages 0–4, 7–10, 12–13 (everything except broadphase, narrowphase,
TGS, and XPBD which are itemized separately).

Call graph timing ranges for "other" stages:
```
Stage 0  (Step Plan):         ~10 µs
Stage 1  (Tier Classification): 20-120 µs
Stage 2  (Spatial Index):       20-80 µs
Stage 3  (Halo Closure):        20-120 µs
Stage 4  (AABB Update):         20-80 µs    (partially in broadphase line)
Stage 7  (Manifold Build):      25-120 µs
Stage 8  (Stabilization):       15-120 µs
Stage 9  (Constraint Build):    ~100 µs
Stage 10 (Island Build):        20-150 µs
Stage 12 (Integrate+Sleep):     20-120 µs
Stage 13 (Cache Commit):        ~20 µs
                                ──────────
Low estimate:                   ~190 µs
High estimate:                  ~1040 µs
Typical (mid-range):            ~400 µs
```

The scenarios use 300–550 µs for "other stages," which is reasonable for
typical-to-moderate load. The high estimate (~1040 µs) only occurs when
ALL stages are at maximum simultaneously, which requires both large body
counts AND dense contacts AND large islands — i.e., the worst-case siege
scenario where we already account for the high cost explicitly.

### 10.3 Missing from Performance Analysis

All 14 call graph stages are accounted for. The Solver Transition box
(between Stages 9 and 11) is now covered in §8 above.

One item in the call graph NOT explicitly budgeted: **Stage 3 Halo Closure**
can spike to 120 µs if many bodies are near tier boundaries. This is folded
into "other stages" and is acceptable, but in the worst-case boundary-flapping
scenario (§8.3), halo closure + transition conversion could add ~170 µs
combined. Still within the "other stages" envelope.
