#ifndef FERRUM_MATH_MAT4_H
#define FERRUM_MATH_MAT4_H

#include "ferrum/math/vec3.h"
#include "ferrum/math/vec4.h"

/** @file
 * @brief Column-major 4x4 matrices (OpenGL layout).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Column-major 4x4 matrix. */
typedef struct mat4 {
    float m[16];
} mat4_t;

/**
 * @brief Build identity matrix.
 * @return Identity matrix.
 */
mat4_t mat4_identity(void);
/**
 * @brief Build translation matrix.
 * @param x Translation X.
 * @param y Translation Y.
 * @param z Translation Z.
 * @return Translation matrix.
 */
mat4_t mat4_translation(float x, float y, float z);
/**
 * @brief Build scaling matrix.
 * @param x Scale X.
 * @param y Scale Y.
 * @param z Scale Z.
 * @return Scaling matrix.
 */
mat4_t mat4_scaling(float x, float y, float z);
/**
 * @brief Build rotation matrix around X axis.
 * @param radians Angle in radians.
 * @return Rotation matrix.
 */
mat4_t mat4_rotation_x(float radians);
/**
 * @brief Build rotation matrix around Y axis.
 * @param radians Angle in radians.
 * @return Rotation matrix.
 */
mat4_t mat4_rotation_y(float radians);
/**
 * @brief Build rotation matrix around Z axis.
 * @param radians Angle in radians.
 * @return Rotation matrix.
 */
mat4_t mat4_rotation_z(float radians);

/**
 * @brief Build a right-handed look-at matrix.
 * @param eye Camera position.
 * @param target Target position.
 * @param up Up direction.
 * @param out Output matrix (non-NULL).
 * @return 0 on success, -1 on invalid input (degenerate vectors).
 */
int mat4_look_at(vec3_t eye, vec3_t target, vec3_t up, mat4_t *out);
/**
 * @brief Build a perspective projection matrix (OpenGL clip space).
 * @param fov_radians Vertical field of view in radians.
 * @param aspect Aspect ratio (width/height).
 * @param near_plane Positive near plane distance.
 * @param far_plane Far plane distance (> near).
 * @param out Output matrix (non-NULL).
 * @return 0 on success, -1 on invalid parameters.
 */
int mat4_perspective(float fov_radians, float aspect, float near_plane, float far_plane, mat4_t *out);
/**
 * @brief Build an orthographic projection matrix.
 * @param left Left plane.
 * @param right Right plane.
 * @param bottom Bottom plane.
 * @param top Top plane.
 * @param near_plane Near plane.
 * @param far_plane Far plane.
 * @return Orthographic projection matrix.
 */
mat4_t mat4_ortho(float left, float right, float bottom, float top, float near_plane, float far_plane);

/**
 * @brief Multiply two matrices (a * b).
 * @param a Left matrix.
 * @param b Right matrix.
 * @return Product matrix.
 */
mat4_t mat4_mul(mat4_t a, mat4_t b);
/**
 * @brief Multiply matrix by vector.
 * @param m Matrix.
 * @param v Vector.
 * @return Transformed vector.
 */
vec4_t mat4_mul_vec4(mat4_t m, vec4_t v);
/**
 * @brief Transpose a matrix.
 * @param m Input matrix.
 * @return Transposed matrix.
 */
mat4_t mat4_transpose(mat4_t m);
/**
 * @brief Invert a matrix.
 * @param m Input matrix.
 * @param out Output matrix (non-NULL).
 * @return 0 on success, -1 on singular or invalid output.
 */
int mat4_inverse(mat4_t m, mat4_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MATH_MAT4_H */
