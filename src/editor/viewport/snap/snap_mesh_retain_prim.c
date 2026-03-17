/**
 * @file snap_mesh_retain_prim.c
 * @brief Generate snap meshes for primitive entity types (sphere, capsule).
 *
 * Non-static functions (2 / 4 limit):
 *   snap_mesh_retain_sphere
 *   snap_mesh_retain_capsule
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- Sphere ---- */

/** Number of longitude/latitude segments for the unit sphere. */
#define SPHERE_RINGS  16
#define SPHERE_SECTORS 16

void snap_mesh_retain_sphere(snap_mesh_cache_t *cache, uint32_t entity_id) {
    if (!cache) return;

    /* UV sphere: (RINGS+1) * (SECTORS+1) vertices,
     * RINGS * SECTORS * 2 triangles (6 indices each quad). */
    uint32_t vert_count = (SPHERE_RINGS + 1) * (SPHERE_SECTORS + 1);
    uint32_t idx_count  = SPHERE_RINGS * SPHERE_SECTORS * 6;

    float *positions = malloc(vert_count * 3 * sizeof(float));
    float *normals   = malloc(vert_count * 3 * sizeof(float));
    uint32_t *indices = malloc(idx_count * sizeof(uint32_t));
    if (!positions || !normals || !indices) {
        free(positions);
        free(normals);
        free(indices);
        return;
    }

    /* Generate vertices. Radius = 0.5 (unit sphere diameter = 1). */
    const float radius = 0.5f;
    const float PI = 3.14159265358979323846f;
    uint32_t vi = 0;
    for (uint32_t r = 0; r <= SPHERE_RINGS; ++r) {
        float phi = PI * (float)r / (float)SPHERE_RINGS;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);
        for (uint32_t s = 0; s <= SPHERE_SECTORS; ++s) {
            float theta = 2.0f * PI * (float)s / (float)SPHERE_SECTORS;
            float nx = sin_phi * cosf(theta);
            float ny = cos_phi;
            float nz = sin_phi * sinf(theta);

            positions[vi * 3 + 0] = nx * radius;
            positions[vi * 3 + 1] = ny * radius;
            positions[vi * 3 + 2] = nz * radius;
            normals[vi * 3 + 0] = nx;
            normals[vi * 3 + 1] = ny;
            normals[vi * 3 + 2] = nz;
            vi++;
        }
    }

    /* Generate indices (quads → 2 triangles each). */
    uint32_t ii = 0;
    for (uint32_t r = 0; r < SPHERE_RINGS; ++r) {
        for (uint32_t s = 0; s < SPHERE_SECTORS; ++s) {
            uint32_t cur  = r * (SPHERE_SECTORS + 1) + s;
            uint32_t next = cur + (SPHERE_SECTORS + 1);

            indices[ii++] = cur;
            indices[ii++] = next;
            indices[ii++] = cur + 1;

            indices[ii++] = cur + 1;
            indices[ii++] = next;
            indices[ii++] = next + 1;
        }
    }

    snap_mesh_cache_insert(cache, entity_id,
                            positions, normals, indices, vert_count, idx_count);

    free(positions);
    free(normals);
    free(indices);
}

/* ---- Capsule ---- */

/** Segments for capsule hemisphere and cylinder. */
#define CAP_RINGS   8
#define CAP_SECTORS 16

void snap_mesh_retain_capsule(snap_mesh_cache_t *cache, uint32_t entity_id) {
    if (!cache) return;

    /* Capsule: cylinder with hemisphere caps.
     * Must match the rendering capsule in scene_viewport_shaders.c:
     *   static_mesh_create_capsule(loader, 0.3f, 0.5f, 16, 4, out)
     * radius = 0.3, half_height = 0.5.
     * Top pole at y = half_height + radius = 0.8.
     * Bottom pole at y = -(half_height + radius) = -0.8.
     * Total height = 1.6, max cross-section radius = 0.3. */
    const float radius = 0.3f;
    const float half_cyl = 0.5f;
    const float PI = 3.14159265358979323846f;

    /* Top hemisphere + cylinder rings + bottom hemisphere.
     * Simplify: reuse sphere generation with Y offset. */
    uint32_t rings_top  = CAP_RINGS / 2;
    uint32_t rings_bot  = CAP_RINGS / 2;
    uint32_t cyl_rings  = 2;  /* Top and bottom of cylinder. */
    uint32_t total_rings = rings_top + cyl_rings + rings_bot;
    uint32_t vert_count = (total_rings + 1) * (CAP_SECTORS + 1);
    uint32_t idx_count  = total_rings * CAP_SECTORS * 6;

    float *positions = malloc(vert_count * 3 * sizeof(float));
    float *normals   = malloc(vert_count * 3 * sizeof(float));
    uint32_t *indices = malloc(idx_count * sizeof(uint32_t));
    if (!positions || !normals || !indices) {
        free(positions);
        free(normals);
        free(indices);
        return;
    }

    uint32_t vi = 0;

    /* Top hemisphere (phi from 0 to PI/2). */
    for (uint32_t r = 0; r <= rings_top; ++r) {
        float phi = (PI / 2.0f) * (float)r / (float)rings_top;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);
        for (uint32_t s = 0; s <= CAP_SECTORS; ++s) {
            float theta = 2.0f * PI * (float)s / (float)CAP_SECTORS;
            float nx = sin_phi * cosf(theta);
            float ny = cos_phi;
            float nz = sin_phi * sinf(theta);
            positions[vi * 3 + 0] = nx * radius;
            positions[vi * 3 + 1] = ny * radius + half_cyl;
            positions[vi * 3 + 2] = nz * radius;
            normals[vi * 3 + 0] = nx;
            normals[vi * 3 + 1] = ny;
            normals[vi * 3 + 2] = nz;
            vi++;
        }
    }

    /* Cylinder body (2 rings: top edge at half_cyl, bottom at -half_cyl). */
    for (uint32_t r = 1; r <= cyl_rings; ++r) {
        float y_off = half_cyl - (2.0f * half_cyl) * (float)r / (float)cyl_rings;
        for (uint32_t s = 0; s <= CAP_SECTORS; ++s) {
            float theta = 2.0f * PI * (float)s / (float)CAP_SECTORS;
            float nx = cosf(theta);
            float nz = sinf(theta);
            positions[vi * 3 + 0] = nx * radius;
            positions[vi * 3 + 1] = y_off;
            positions[vi * 3 + 2] = nz * radius;
            normals[vi * 3 + 0] = nx;
            normals[vi * 3 + 1] = 0;
            normals[vi * 3 + 2] = nz;
            vi++;
        }
    }

    /* Bottom hemisphere (phi from PI/2 to PI). */
    for (uint32_t r = 1; r <= rings_bot; ++r) {
        float phi = (PI / 2.0f) + (PI / 2.0f) * (float)r / (float)rings_bot;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);
        for (uint32_t s = 0; s <= CAP_SECTORS; ++s) {
            float theta = 2.0f * PI * (float)s / (float)CAP_SECTORS;
            float nx = sin_phi * cosf(theta);
            float ny = cos_phi;
            float nz = sin_phi * sinf(theta);
            positions[vi * 3 + 0] = nx * radius;
            positions[vi * 3 + 1] = ny * radius - half_cyl;
            positions[vi * 3 + 2] = nz * radius;
            normals[vi * 3 + 0] = nx;
            normals[vi * 3 + 1] = ny;
            normals[vi * 3 + 2] = nz;
            vi++;
        }
    }

    /* Generate indices. */
    uint32_t ii = 0;
    for (uint32_t r = 0; r < total_rings; ++r) {
        for (uint32_t s = 0; s < CAP_SECTORS; ++s) {
            uint32_t cur  = r * (CAP_SECTORS + 1) + s;
            uint32_t next = cur + (CAP_SECTORS + 1);

            indices[ii++] = cur;
            indices[ii++] = next;
            indices[ii++] = cur + 1;

            indices[ii++] = cur + 1;
            indices[ii++] = next;
            indices[ii++] = next + 1;
        }
    }

    snap_mesh_cache_insert(cache, entity_id,
                            positions, normals, indices, vi, ii);

    free(positions);
    free(normals);
    free(indices);
}
