/**
 * @file aabb_shapes.c
 * @brief AABB construction from primitive shapes (sphere, box, capsule).
 *
 * 3 non-static functions.
 */

#include "ferrum/physics/aabb.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

void phys_aabb_from_sphere(phys_aabb_t *aabb, phys_vec3_t center, float radius)
{
    if (!aabb) { return; }

    phys_vec3_t r = {radius, radius, radius};
    aabb->min = vec3_sub(center, r);
    aabb->max = vec3_add(center, r);
}

void phys_aabb_from_box(phys_aabb_t *aabb, phys_vec3_t center,
                        phys_quat_t rotation, phys_vec3_t half_extents)
{
    if (!aabb) { return; }

    /* Compute the three rotated local axes in world space. */
    phys_vec3_t ax = quat_rotate_vec3(rotation, (phys_vec3_t){1.0f, 0.0f, 0.0f});
    phys_vec3_t ay = quat_rotate_vec3(rotation, (phys_vec3_t){0.0f, 1.0f, 0.0f});
    phys_vec3_t az = quat_rotate_vec3(rotation, (phys_vec3_t){0.0f, 0.0f, 1.0f});

    /* For each world axis i, the half-extent in world space is:
     *   sum_j |R[i][j]| * half_extents[j]
     * where R[i][j] is the i-th component of the j-th rotated axis. */
    float he_arr[3] = {half_extents.x, half_extents.y, half_extents.z};
    float ax_arr[3] = {ax.x, ax.y, ax.z};
    float ay_arr[3] = {ay.x, ay.y, ay.z};
    float az_arr[3] = {az.x, az.y, az.z};

    float world_he[3];
    for (int i = 0; i < 3; i++) {
        world_he[i] = fabsf(ax_arr[i]) * he_arr[0]
                    + fabsf(ay_arr[i]) * he_arr[1]
                    + fabsf(az_arr[i]) * he_arr[2];
    }

    phys_vec3_t extent = {world_he[0], world_he[1], world_he[2]};
    aabb->min = vec3_sub(center, extent);
    aabb->max = vec3_add(center, extent);
}

void phys_aabb_from_capsule(phys_aabb_t *aabb, phys_vec3_t center,
                            phys_quat_t rotation, float radius,
                            float half_height)
{
    if (!aabb) { return; }

    /* Capsule axis is local +Y. Rotate to world space. */
    phys_vec3_t axis = quat_rotate_vec3(rotation, (phys_vec3_t){0.0f, 1.0f, 0.0f});

    /* Endpoints of the capsule line segment. */
    phys_vec3_t half_seg = vec3_scale(axis, half_height);
    phys_vec3_t p0 = vec3_sub(center, half_seg);
    phys_vec3_t p1 = vec3_add(center, half_seg);

    /* AABB of the two endpoints, then expand by radius. */
    float min_x = (p0.x < p1.x) ? p0.x : p1.x;
    float min_y = (p0.y < p1.y) ? p0.y : p1.y;
    float min_z = (p0.z < p1.z) ? p0.z : p1.z;
    float max_x = (p0.x > p1.x) ? p0.x : p1.x;
    float max_y = (p0.y > p1.y) ? p0.y : p1.y;
    float max_z = (p0.z > p1.z) ? p0.z : p1.z;

    aabb->min = (phys_vec3_t){min_x - radius, min_y - radius, min_z - radius};
    aabb->max = (phys_vec3_t){max_x + radius, max_y + radius, max_z + radius};
}
