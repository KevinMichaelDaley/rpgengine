/**
 * @file snap_surface_apply.c
 * @brief Apply face and vertex snap hit results to entity transforms.
 *
 * Face snap: set position to hit point, orient entity local +Y to
 * face normal. Vertex snap: find nearest vertex of hit triangle,
 * set position and orient to vertex normal.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_apply_face
 *   snap_apply_vertex
 */

#include "ferrum/editor/viewport/snap/snap_surface_apply.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/viewport/snap/snap_raycast.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

#include <math.h>

/**
 * @brief Build an orientation quaternion that maps local +Y to the
 *        given world-space normal.
 *
 * Constructs a right-handed basis where:
 *   - new_y = normal (the desired up)
 *   - new_x = cross(up_hint, normal), normalized
 *   - new_z = cross(new_x, normal)
 *
 * If normal is nearly parallel to world +Y, uses world +Z as the
 * hint vector instead.
 */
static quat_t orient_y_to_normal_(vec3_t normal) {
    /* Choose a hint vector that's not parallel to the normal. */
    vec3_t hint = {0, 1, 0};
    float dot = fabsf(normal.x * hint.x + normal.y * hint.y +
                       normal.z * hint.z);
    if (dot > 0.99f) {
        hint = (vec3_t){0, 0, 1};
    }

    /* new_x = cross(hint, normal) */
    vec3_t new_x = vec3_cross(hint, normal);
    float len_x = sqrtf(new_x.x * new_x.x + new_x.y * new_x.y +
                          new_x.z * new_x.z);
    if (len_x < 1e-6f) {
        return (quat_t){0, 0, 0, 1};
    }
    new_x.x /= len_x;
    new_x.y /= len_x;
    new_x.z /= len_x;

    /* new_z = cross(new_x, normal) for right-handed basis (X × Y = Z). */
    vec3_t new_z = vec3_cross(new_x, normal);

    /* Build rotation matrix from basis vectors (column-major). */
    mat4_t rot_mat;
    rot_mat.m[0]  = new_x.x;  rot_mat.m[1]  = new_x.y;  rot_mat.m[2]  = new_x.z;  rot_mat.m[3]  = 0;
    rot_mat.m[4]  = normal.x;  rot_mat.m[5]  = normal.y;  rot_mat.m[6]  = normal.z;  rot_mat.m[7]  = 0;
    rot_mat.m[8]  = new_z.x;  rot_mat.m[9]  = new_z.y;  rot_mat.m[10] = new_z.z;  rot_mat.m[11] = 0;
    rot_mat.m[12] = 0;        rot_mat.m[13] = 0;         rot_mat.m[14] = 0;         rot_mat.m[15] = 1;

    return quat_from_mat4(&rot_mat);
}

/**
 * @brief Transform a point by a 4x4 model matrix (w=1).
 */
static vec3_t transform_point_(const mat4_t *m, vec3_t p) {
    vec3_t out;
    out.x = m->m[0] * p.x + m->m[4] * p.y + m->m[8]  * p.z + m->m[12];
    out.y = m->m[1] * p.x + m->m[5] * p.y + m->m[9]  * p.z + m->m[13];
    out.z = m->m[2] * p.x + m->m[6] * p.y + m->m[10] * p.z + m->m[14];
    return out;
}

/**
 * @brief Transform a normal by the upper-left 3x3 of a model matrix.
 */
static vec3_t transform_normal_(const mat4_t *m, vec3_t n) {
    vec3_t out;
    out.x = m->m[0] * n.x + m->m[4] * n.y + m->m[8]  * n.z;
    out.y = m->m[1] * n.x + m->m[5] * n.y + m->m[9]  * n.z;
    out.z = m->m[2] * n.x + m->m[6] * n.y + m->m[10] * n.z;
    float len = sqrtf(out.x * out.x + out.y * out.y + out.z * out.z);
    if (len > 1e-6f) {
        out.x /= len;
        out.y /= len;
        out.z /= len;
    }
    return out;
}

void snap_apply_face(struct edit_entity *ent, const struct snap_hit *hit) {
    if (!ent || !hit || !hit->valid) return;

    ent->pos[0] = hit->position.x;
    ent->pos[1] = hit->position.y;
    ent->pos[2] = hit->position.z;

    ent->orientation = orient_y_to_normal_(hit->normal);
}

void snap_apply_vertex(struct edit_entity *ent, const struct snap_hit *hit,
                         const struct snap_mesh *mesh, const mat4_t *model) {
    if (!ent || !hit || !hit->valid || !mesh || !model) return;

    uint32_t tri_start = hit->face_index * 3;
    if (tri_start + 2 >= mesh->index_count) return;

    /* Get the 3 vertex indices of the hit triangle. */
    uint32_t i0 = mesh->indices[tri_start];
    uint32_t i1 = mesh->indices[tri_start + 1];
    uint32_t i2 = mesh->indices[tri_start + 2];

    /* Transform vertices to world space. */
    vec3_t v0 = {mesh->positions[i0*3], mesh->positions[i0*3+1], mesh->positions[i0*3+2]};
    vec3_t v1 = {mesh->positions[i1*3], mesh->positions[i1*3+1], mesh->positions[i1*3+2]};
    vec3_t v2 = {mesh->positions[i2*3], mesh->positions[i2*3+1], mesh->positions[i2*3+2]};

    vec3_t w0 = transform_point_(model, v0);
    vec3_t w1 = transform_point_(model, v1);
    vec3_t w2 = transform_point_(model, v2);

    /* Find nearest vertex to hit position. */
    vec3_t hp = hit->position;
    float d0 = (w0.x-hp.x)*(w0.x-hp.x) + (w0.y-hp.y)*(w0.y-hp.y) + (w0.z-hp.z)*(w0.z-hp.z);
    float d1 = (w1.x-hp.x)*(w1.x-hp.x) + (w1.y-hp.y)*(w1.y-hp.y) + (w1.z-hp.z)*(w1.z-hp.z);
    float d2 = (w2.x-hp.x)*(w2.x-hp.x) + (w2.y-hp.y)*(w2.y-hp.y) + (w2.z-hp.z)*(w2.z-hp.z);

    uint32_t nearest_idx;
    vec3_t nearest_pos;
    if (d0 <= d1 && d0 <= d2) {
        nearest_idx = i0;
        nearest_pos = w0;
    } else if (d1 <= d2) {
        nearest_idx = i1;
        nearest_pos = w1;
    } else {
        nearest_idx = i2;
        nearest_pos = w2;
    }

    /* Set position to nearest vertex. */
    ent->pos[0] = nearest_pos.x;
    ent->pos[1] = nearest_pos.y;
    ent->pos[2] = nearest_pos.z;

    /* Get vertex normal and transform to world space. */
    vec3_t vn = {
        mesh->normals[nearest_idx*3],
        mesh->normals[nearest_idx*3+1],
        mesh->normals[nearest_idx*3+2]
    };
    vec3_t world_normal = transform_normal_(model, vn);

    ent->orientation = orient_y_to_normal_(world_normal);
}
