---
id: rpg-uch2
status: closed
deps: [rpg-rh6r]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-0lyi
tags: [aegis, vm, math]
---
# Aegis vector and quaternion math instructions

Implement vector and quaternion math instructions per ref/aegis_bytecode_spec.md §3.3.

Instructions:
- vec3_add, vec3_sub, vec3_mul r_dst, r_a, r_b: component-wise operations
- vec3_scale r_dst, r_vec, r_scalar: multiply vec3 by scalar
- vec3_dot r_dst, r_a, r_b: dot product → f32 in r_dst
- vec3_cross r_dst, r_a, r_b: cross product → vec3 in r_dst
- vec3_len r_dst, r_a: length → f32 in r_dst
- vec3_norm r_dst, r_a: normalize → unit vec3 in r_dst (zero vector → zero result, no crash)
- quat_mul r_dst, r_a, r_b: quaternion multiplication
- quat_rotate r_dst, r_quat, r_vec: rotate vec3 by quaternion

Use the engine's existing math functions from ferrum/math/ where applicable (quat_rotate_vec3, etc.).

Files:
- include/ferrum/aegis/aegis_ops_math.h
- src/aegis/ops/aegis_ops_vec3.c (add, sub, mul, scale — 4 funcs)
- src/aegis/ops/aegis_ops_vec3b.c (dot, cross, len, norm — 4 funcs)
- src/aegis/ops/aegis_ops_quat.c (quat_mul, quat_rotate)
- tests/aegis/aegis_ops_math_tests.c

Acceptance criteria:
- [ ] vec3 operations match engine math library results
- [ ] vec3_norm of zero vector produces zero (no NaN/crash)
- [ ] quat_mul is non-commutative and correct
- [ ] quat_rotate matches engine's quat_rotate_vec3()
- [ ] Tests: identity cases, orthogonal vectors, unit quaternion, zero inputs

## Acceptance Criteria

Vector/quat ops match engine math, zero-vector safe, tested against known values

