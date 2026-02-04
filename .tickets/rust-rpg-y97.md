---
id: rust-rpg-y97
status: open
deps: [rust-rpg-o4i]
links: []
created: 2026-01-17T18:02:49.382960615-08:00
type: epic
priority: 2
---
# P_005 — Geometry Clipmaps (Terrain)

## P_005 — Geometry Clipmaps (Terrain)

### Design Intent
Render large terrain with constant geometry memory by using nested grids whose offsets “snap” in a toroidal fashion around the camera, pushing complexity to shaders.

### Specification
- One shared `MxM` grid mesh.
- `clipmap_layer_t { float scale; vec2_t offset; }`
- Offset snapping:
  - `offset = floor(camera_pos / (base_spacing * scale)) * (base_spacing * scale)`
- Shader uses scale+offset to compute world pos; samples heightmap.

### Implementation Steps
1. Grid mesh generation (positions + indices).
2. Layer definitions and update logic.
3. Uniform upload per layer.
4. Blending factor computation (distance-based).

### Architectural Considerations
- CPU only updates offsets; geometry static.
- Avoid per-frame allocations; layer arrays fixed.
- Tests focus on CPU offset math (shader tested by invariants).

### Unit Tests (RED-first)
**Happy Path**
1. **Grid mesh generation counts**
   - for `M`, verify vertex count is `M*M` and index count matches the chosen topology.
2. **Offset snapping per LOD**
   - simulate camera moves; layer0 snaps at `base_spacing`; layer1 snaps at `base_spacing*2`.
3. **Layer uniform packing**
   - verify `scale` and `offset` upload values match CPU state exactly.

**Edge Cases**
4. **Negative coordinates snapping**
   - camera at negative positions must snap consistently (no oscillation around 0).
5. **Large world coordinates**
   - camera far from origin (large magnitude) must not overflow integer snapping and must remain stable.
6. **Texture coordinate wrapping**
   - ensure computed UV wraps into [0,1) for both positive and negative offsets.
7. **Blend factor monotonicity**
   - blending factor increases/decreases monotonically with distance (as defined) and stays clamped.

**Failure Modes**
8. **Invalid parameters**
   - `M < 2`, `base_spacing <= 0`, or `scale <= 0` must return explicit failure.

### Regression Tests (RED-first)
1. **No shimmer at snap boundaries**
   - moving camera by tiny deltas across a snap boundary must not cause offset to oscillate between two values.
2. **UV wrap for negative values**
   - ensure wrap logic does not produce negative UVs due to C `fmodf` semantics.

### Cumulative Integration Tests (RED-first, cumulative through P_005)
1. **Terrain layer update in jobs (P_000..P_005)**
   - dispatch a job to update clipmap offsets and a job to upload uniforms via mocked GL.
   - verify layer offsets/uniforms are consistent and deterministic per “frame”.

---



