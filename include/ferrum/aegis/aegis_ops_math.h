/**
 * @file aegis_ops_math.h
 * @brief Aegis vector and quaternion math instruction handlers.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 *
 * All operations work on the vec3[3] or vec4[4] fields
 * of the register union.
 *
 * Ownership: callers own all pointers.
 * Nullability: all pointers must be non-NULL.
 */

#ifndef FERRUM_AEGIS_OPS_MATH_H
#define FERRUM_AEGIS_OPS_MATH_H

#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- Vec3 component-wise -- */

/** dst.vec3 = a.vec3 + b.vec3 */
void aegis_op_vec3_add(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b);

/** dst.vec3 = a.vec3 - b.vec3 */
void aegis_op_vec3_sub(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b);

/** dst.vec3 = a.vec3 * b.vec3 (component-wise) */
void aegis_op_vec3_mul(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b);

/** dst.vec3 = vec.vec3 * scalar.f32 */
void aegis_op_vec3_scale(aegis_register_t *dst,
                         const aegis_register_t *vec,
                         const aegis_register_t *scalar);

/* -- Vec3 reduction -- */

/** dst.f32 = dot(a.vec3, b.vec3) */
void aegis_op_vec3_dot(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b);

/** dst.vec3 = cross(a.vec3, b.vec3) */
void aegis_op_vec3_cross(aegis_register_t *dst,
                         const aegis_register_t *a, const aegis_register_t *b);

/** dst.f32 = length(a.vec3) */
void aegis_op_vec3_len(aegis_register_t *dst, const aegis_register_t *a);

/** dst.vec3 = normalize(a.vec3). Zero vector → zero result. */
void aegis_op_vec3_norm(aegis_register_t *dst, const aegis_register_t *a);

/* -- Quaternion -- */

/** dst.vec4 = a.vec4 * b.vec4 (Hamilton product). */
void aegis_op_quat_mul(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b);

/** dst.vec3 = rotate(vec.vec3, quat.vec4). */
void aegis_op_quat_rotate(aegis_register_t *dst,
                           const aegis_register_t *quat,
                           const aegis_register_t *vec);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_MATH_H */
