/**
 * @file phys_mat3_ops.c
 * @brief 3x3 matrix operations for world-space inertia tensors.
 *
 * Non-static functions (2): phys_mat3_mul_vec3, phys_mat3_inv_inertia_world.
 */

#include "ferrum/physics/phys_mat3.h"

phys_vec3_t phys_mat3_mul_vec3(const phys_mat3_t *M, phys_vec3_t v) {
    /* Column-major: M[col*3 + row].
     * result.x = M[0]*v.x + M[3]*v.y + M[6]*v.z
     * result.y = M[1]*v.x + M[4]*v.y + M[7]*v.z
     * result.z = M[2]*v.x + M[5]*v.y + M[8]*v.z */
    return (phys_vec3_t){
        M->m[0] * v.x + M->m[3] * v.y + M->m[6] * v.z,
        M->m[1] * v.x + M->m[4] * v.y + M->m[7] * v.z,
        M->m[2] * v.x + M->m[5] * v.y + M->m[8] * v.z,
    };
}

phys_mat3_t phys_mat3_inv_inertia_world(phys_quat_t q, phys_vec3_t d) {
    /* Compute R * diag(d) * R^T from quaternion q.
     *
     * Rotation matrix R from unit quaternion (column-major):
     *   R00 = 1 - 2(yy+zz)   R01 = 2(xy-wz)     R02 = 2(xz+wy)
     *   R10 = 2(xy+wz)       R11 = 1 - 2(xx+zz)  R12 = 2(yz-wx)
     *   R20 = 2(xz-wy)       R21 = 2(yz+wx)       R22 = 1 - 2(xx+yy)
     *
     * For R*diag(d)*R^T, element (i,j) = sum_k R_ik * d_k * R_jk.
     * We compute each R column scaled by d_k, then form the symmetric
     * product.  The result is symmetric so we compute 6 unique elements. */
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    /* Rotation matrix columns. */
    float r00 = 1.0f - 2.0f * (yy + zz);
    float r10 = 2.0f * (xy + wz);
    float r20 = 2.0f * (xz - wy);

    float r01 = 2.0f * (xy - wz);
    float r11 = 1.0f - 2.0f * (xx + zz);
    float r21 = 2.0f * (yz + wx);

    float r02 = 2.0f * (xz + wy);
    float r12 = 2.0f * (yz - wx);
    float r22 = 1.0f - 2.0f * (xx + yy);

    /* Scaled columns: s_ik = R_ik * d_k. */
    float s00 = r00 * d.x, s10 = r10 * d.x, s20 = r20 * d.x;
    float s01 = r01 * d.y, s11 = r11 * d.y, s21 = r21 * d.y;
    float s02 = r02 * d.z, s12 = r12 * d.z, s22 = r22 * d.z;

    /* Result (i,j) = sum_k s_ik * R_jk.  Symmetric, so M01=M10 etc. */
    phys_mat3_t M;
    M.m[0] = s00 * r00 + s01 * r01 + s02 * r02; /* (0,0) */
    M.m[1] = s10 * r00 + s11 * r01 + s12 * r02; /* (1,0) */
    M.m[2] = s20 * r00 + s21 * r01 + s22 * r02; /* (2,0) */
    M.m[3] = M.m[1];                              /* (0,1) = (1,0) */
    M.m[4] = s10 * r10 + s11 * r11 + s12 * r12; /* (1,1) */
    M.m[5] = s20 * r10 + s21 * r11 + s22 * r12; /* (2,1) */
    M.m[6] = M.m[2];                              /* (0,2) = (2,0) */
    M.m[7] = M.m[5];                              /* (1,2) = (2,1) */
    M.m[8] = s20 * r20 + s21 * r21 + s22 * r22; /* (2,2) */
    return M;
}
