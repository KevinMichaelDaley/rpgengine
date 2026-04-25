# Code Review: Ferrum Math Subsystem

## Overview
The Ferrum math subsystem provides a comprehensive set of linear algebra utilities (vec2, vec3, vec4, mat4, quat). However, there is significant redundancy and lack of standardization across the codebase, particularly in the physics solver and editor components.

## 1. Redundant Functionality

### 1.1 Vector Magnitude and Normalization
- `vec3_magnitude` is implemented in `src/math/vec3_extra.c`.
- Many files recompute magnitude manually using `sqrtf(vec3_dot(delta, delta))` (e.g., `joint_distance.c`, `joint_ik.c`, `narrowphase_sphere_tri.c`).
- Some files implement local `clamp_vec3_magnitude` helpers (e.g., `xpbd_solve.c`).

**Recommendation:**
- Standardize on `vec3_magnitude(v)` and `vec3_magnitude_sq(v)`.
- Add `vec3_magnitude_sq` to `vec3.h` to avoid unnecessary `sqrtf` calls in distance comparisons.

### 1.2 Quaternion Integration
- `fr_quat_integrate_angular_velocity` is defined in `math/quat_angle.h`.
- `quat_integrate_expmap` is defined locally in `physics/solver/tgs_solve.c`.
- `ang_proj_quat_integrate` is defined locally in `physics/solver/joint_angular_projection.c`.
- `tgs_quat_integrate` is defined locally in `physics/solver/tgs_solve.c` (using a simpler Euler-like update).

**Recommendation:**
- Standardize on a single exponential map integration function in `math/quat.h`.
- The `quat_integrate_expmap` from `tgs_solve.c` is the most robust (handles hemisphere consistency).

### 1.3 Dot Product Inlining
- `vec3_dot` is used everywhere but is a non-static function call in `math/vec3_basic.c`.
- For performance-critical loops (physics solver, collision), this should be a macro or an inline function in the header.

## 2. Inconsistent Type Usage

### 2.1 `phys_vec3_t` vs `vec3_t`
- `include/ferrum/physics/phys_vec3.h` defines `phys_vec3_t` as a typedef of `vec3_t`.
- This adds unnecessary cognitive load and conversion macros (`PHYS_VEC3_FROM_VEC3`).
- Since they are bit-compatible and conceptually identical (3D float vectors), this distinction should be removed unless a higher-precision type (e.g., `double`) is planned for physics.

## 3. Math Module Organization

### 3.1 Function Distribution
- `vec3_basic.c` contains `add`, `sub`, `scale`.
- `vec3_extra.c` contains `magnitude`, `normalize`, `lerp`, `cross`.
- This split seems arbitrary and forces multiple object files for basic vector math.

**Recommendation:**
- Consolidate basic vector operations into single files (`vec3.c`, `mat4.c`, etc.).
- Move performance-critical operations (dot, add, sub) to inline functions in headers.

## 4. Missing Utilities
- No `mat3_t` for 3x3 matrices (inertia tensors, rotation-only transforms).
- `phys_mat3_t` exists in `physics/` but lacks a corresponding math-module equivalent.
- No `vec3_distance` or `vec3_distance_sq` helpers.

## Next Steps
1. **Refactor Headers:** Move core math operations to `static inline` in headers.
2. **Consolidate Implementation:** Merge `basic` and `extra` source files.
3. **Standardize Integration:** Move robust `quat_integrate_expmap` to `math/quat.h`.
4. **Cleanup Physics:** Replace local math helpers with standardized math module calls.
5. **Remove Type Aliases:** Deprecate `phys_vec3_t` and `phys_quat_t` in favor of base types.
