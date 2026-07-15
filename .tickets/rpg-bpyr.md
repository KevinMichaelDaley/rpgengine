---
id: rpg-bpyr
status: closed
deps: []
links: []
created: 2026-07-15T08:41:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-tjtp
---
# SVO->SDF via Jump Flood Algorithm for sphere-traced gather


## Notes

**2026-07-15T08:41:06Z**

Augment the packed SVO scene (rpg-8je5) with a dense signed distance field built by the Jump Flood Algorithm, so the gather ray-marcher (rpg-8sv9) SPHERE-traces (big empty-space steps) instead of DDA voxel-stepping -- trading a few JFA passes for a massively faster trace.

## Algorithm (JFA -> SDF)
- Derive a dense occupancy grid at the SVO's finest level (dims = ceil(world_bounds / voxel_size)) from the SVO leaves (solid = leaf with SOLID/material).
- Seed cells = the surface: a solid cell with an empty 6-neighbour OR an empty cell with a solid 6-neighbour, storing their own linear index.
- Ping-pong JFA: step = 2^ceil(log2(maxdim))/2 down to 1 (halving). Each cell scans the 26 neighbours at +/-step, adopting the seed nearest to the cell. O(log N) passes.
- Distance = |cell - nearest_seed| * voxel_size; SIGN negative inside solid, positive outside. Store as a dense R16F/R32F field (a 3D texture on GPU).

## Deliverables
- Host-side CPU JFA->SDF reference (TDD): occupancy grid in -> signed distance grid out, caller-provided ping-pong scratch (no allocation). This is the correctness spec + offline fallback.
- SVO-leaves -> dense occupancy helper.
- GPU port: the same as a compute kernel (ping-pong SSBO/3D-texture, one invocation per cell per pass); a few passes total. Notes for rpg-8sv9: the gather then sphere-traces the SDF 3D texture, only dropping to exact voxel/material reads near the surface.

## Acceptance
CPU reference produces a correct signed distance field (verified on known configs: single voxel, plane, box -- signs + monotone distances). Compute port matches within tolerance. The sphere-traced gather visibly speeds up vs DDA with identical bake results (validated once rpg-8sv9/rpg-k4lk land).

**2026-07-15T08:42:16Z**

TEST STRATEGY: validate JFA against a known composition of ANALYTIC SDFs (sphere, box, union=min). Rasterise occupancy from analytic_sdf(cell_centre) < 0, run JFA, and assert the JFA distance field matches the analytic SDF within a discretisation tolerance (~1-2 voxels max, mean < ~1 voxel), with correct sign. Ground truth beats hand-computed cells.

**2026-07-15T08:47:28Z**

DONE. GPU JFA->SDF compute kernel validated against analytic SDF composition (sphere U box) on Iris Xe: mean err 0.465 / max 1.414 voxels (= sqrt(2) discretisation limit) / 0 sign mismatches over 64^3. Three compute shaders (surface-seed init -> ping-pong JFA -> signed finalise) + the offline GL 4.3 compute context (shimmed on 3.3 glad). The GLSL currently lives in tests/visual/lm_jfa_sdf.c; it lifts into the baker's compute context during integration (rpg-k4lk), and the gather (rpg-8sv9) then sphere-traces this SDF field, dropping to exact voxel/material reads only near the surface.
