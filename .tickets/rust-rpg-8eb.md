---
id: rust-rpg-8eb
status: closed
deps: []
links: []
created: 2026-01-17T18:02:48.802875129-08:00
type: task
priority: 2
---
# P_001 — Core Math Library (OpenGL-friendly)

## P_001 — Core Math Library (OpenGL-friendly)

### Design Intent
Provide a minimal but robust math foundation with GPU-native layouts (column-major matrices), explicit C APIs (no operator overloading), safe normalization, and quaternion support for stable rotation/interpolation.

### Specification
#### Public Types (split across headers to satisfy 2-Type Rule)
- `vec2_t`, `vec3_t`, `vec4_t`
- `quat_t`
- `mat4_t` (column-major `[16]`)

#### Required Operations
- Vectors:
  - `add/sub/scale`, `dot`, `magnitude`, `normalize_safe(epsilon)`, `lerp`
  - `cross` for `vec3_t`
- Matrices:
  - constructors: identity, translation, scaling, rotation_x/y/z
  - camera: look_at, perspective, ortho
  - ops: mul_mat4, mul_vec4, transpose, inverse (general or affine-fastpath)
- Quaternions:
  - from_axis_angle, normalize_safe, conjugate, mul_quat, slerp
  - to_mat4

#### Numeric Rules
- Use `float` everywhere.
- `normalize_safe`: if length < epsilon, return zero vector (or identity quat for quats).

### Implementation Steps
1. Define POD structs and storage.
2. Implement scalar ops and dot/cross.
3. Normalize + lerp with epsilon.
4. Quaternion math + slerp.
5. Matrix constructors + multiplication.
6. Implement `perspective` matching OpenGL clip space (document conventions).
7. Inverse: start with affine inverse (TRS) then add general inverse if needed.

### Architectural Considerations
- **No heap allocation.**
- **No hidden global constants** beyond explicit `const`.
- **Layout guarantees:** document and static-assert sizes where possible.
- **Determinism:** tests should tolerate tiny float error; use epsilon comparisons.

### Unit Tests (RED-first)
**Happy Path**
1. **Translation mat4 × vec4**
   - `T = translation(10,0,0)`
   - `p=(0,0,0,1)` → result `(10,0,0,1)`
2. **Identity invariants**
   - `I * v == v`, `I * I == I`, `transpose(I) == I`.
3. **Vector dot and magnitude**
   - `dot(v,v)` equals `magnitude(v)^2` within epsilon.
4. **Normalize_safe (non-zero)**
   - Normalizing `(3,4,0)` yields approx `(0.6,0.8,0)`; magnitude ~1.
5. **Quaternion to_mat4 preserves axis-angle**
   - `quat_from_axis_angle(Z, 90°)` rotates `(1,0,0)` to `(0,1,0)` within epsilon.
6. **Matrix multiply associativity (tolerant)**
   - `(A*B)*C` approximately equals `A*(B*C)` for a small set of deterministic matrices.

**Edge Cases**
7. **Normalize_safe zero vector**
   - `normalize_safe((0,0,0), eps)` returns `(0,0,0)`.
8. **Normalize_safe epsilon corner cases**
   - `eps == 0` must not divide by zero; function returns a finite vector (document the chosen behavior).
9. **Cross product basis**
   - `X×Y = Z`, `Y×X = -Z`, and `X×X = 0`.
10. **Look-at degeneracy handling**
   - When `eye == target` (or up parallel to view), input must return an explicit failure (or a documented fallback) without NaNs.

**Failure Modes**
11. **Inverse failure on singular matrix**
   - Inverting a singular matrix returns explicit failure (or produces a documented identity fallback) and never produces NaNs.
12. **Perspective parameter validation**
   - `near <= 0`, `far <= near`, `aspect <= 0`, or `fov <= 0` must return explicit failure.

### Regression Tests (RED-first)
1. **Perspective regression (index-level)**
   - fov 90°, aspect 1, near 0.1, far 100
   - assert specific indices are within epsilon of expected values.
2. **Quaternion slerp endpoints and normalization**
   - `t=0` returns source; `t=1` returns target; intermediate stays ~unit length.
3. **Handedness / convention lock-in**
   - `look_at` + `perspective` combination places a known point in expected clip-space (within epsilon).

### Cumulative Integration Tests (RED-first, cumulative through P_001)
1. **Math under scheduler load (P_000 + P_001)**
   - Dispatch many jobs that compute deterministic matrix/quaternion results into an output array.
   - Verify results match a single-thread reference run.

---



