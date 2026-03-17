/**
 * @file snap_mesh_retain_convex.c
 * @brief Generate snap meshes from convex hull and compound collider data.
 *
 * Triangulates convex hull polygon faces (fan triangulation from first
 * vertex of each face) and inserts the result into the snap mesh cache.
 * For compounds, merges all child hulls into a single snap mesh.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_mesh_retain_convex_hull
 *   snap_mesh_retain_compound
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/convex_compound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Count the total triangles produced by fan-triangulating all faces.
 *
 * A convex polygon with N vertices produces N-2 triangles via fan
 * triangulation from vertex 0.
 */
static uint32_t count_hull_triangles_(const phys_convex_hull_t *hull) {
    uint32_t tri_count = 0;
    for (uint32_t f = 0; f < hull->face_count; ++f) {
        uint32_t n = hull->faces[f].index_count;
        if (n >= 3) tri_count += n - 2;
    }
    return tri_count;
}

/**
 * @brief Write fan-triangulated indices for one hull into output arrays.
 *
 * @param hull         Source convex hull.
 * @param vert_offset  Vertex index offset (for compound merging).
 * @param out_indices  Output index array.
 * @param idx_offset   Current write position in out_indices.
 * @return Number of indices written.
 */
static uint32_t triangulate_hull_faces_(const phys_convex_hull_t *hull,
                                          uint32_t vert_offset,
                                          uint32_t *out_indices,
                                          uint32_t idx_offset) {
    uint32_t written = 0;
    for (uint32_t f = 0; f < hull->face_count; ++f) {
        const phys_convex_face_t *face = &hull->faces[f];
        if (face->index_count < 3) continue;

        /* Fan triangulation: (v0, v1, v2), (v0, v2, v3), ... */
        uint16_t v0 = hull->indices[face->index_start];
        for (uint32_t i = 1; i + 1 < face->index_count; ++i) {
            uint16_t v1 = hull->indices[face->index_start + i];
            uint16_t v2 = hull->indices[face->index_start + i + 1];
            out_indices[idx_offset + written + 0] = vert_offset + v0;
            out_indices[idx_offset + written + 1] = vert_offset + v1;
            out_indices[idx_offset + written + 2] = vert_offset + v2;
            written += 3;
        }
    }
    return written;
}

void snap_mesh_retain_convex_hull(snap_mesh_cache_t *cache,
                                    uint32_t entity_id,
                                    const phys_convex_hull_t *hull) {
    if (!cache || !hull) return;
    if (hull->vertex_count < 4 || hull->face_count < 4) return;

    uint32_t tri_count = count_hull_triangles_(hull);
    uint32_t idx_count = tri_count * 3;
    uint32_t vert_count = hull->vertex_count;

    float *positions = malloc(vert_count * 3 * sizeof(float));
    float *normals   = malloc(vert_count * 3 * sizeof(float));
    uint32_t *indices = malloc(idx_count * sizeof(uint32_t));
    if (!positions || !normals || !indices) {
        free(positions); free(normals); free(indices);
        return;
    }

    /* Copy vertices (phys_vec3_t → float[3]). */
    for (uint32_t v = 0; v < vert_count; ++v) {
        positions[v * 3 + 0] = hull->vertices[v].x;
        positions[v * 3 + 1] = hull->vertices[v].y;
        positions[v * 3 + 2] = hull->vertices[v].z;
    }

    /* Compute per-vertex normals by averaging adjacent face normals.
     * Zero-initialize, accumulate, then normalize. */
    memset(normals, 0, vert_count * 3 * sizeof(float));
    for (uint32_t f = 0; f < hull->face_count; ++f) {
        const phys_convex_face_t *face = &hull->faces[f];
        for (uint32_t i = 0; i < face->index_count; ++i) {
            uint32_t vi = hull->indices[face->index_start + i];
            normals[vi * 3 + 0] += face->normal.x;
            normals[vi * 3 + 1] += face->normal.y;
            normals[vi * 3 + 2] += face->normal.z;
        }
    }
    for (uint32_t v = 0; v < vert_count; ++v) {
        float nx = normals[v * 3 + 0];
        float ny = normals[v * 3 + 1];
        float nz = normals[v * 3 + 2];
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            normals[v * 3 + 0] *= inv;
            normals[v * 3 + 1] *= inv;
            normals[v * 3 + 2] *= inv;
        }
    }

    /* Fan-triangulate faces. */
    triangulate_hull_faces_(hull, 0, indices, 0);

    snap_mesh_cache_insert(cache, entity_id,
                            positions, normals, indices, vert_count, idx_count);

    free(positions);
    free(normals);
    free(indices);
}

void snap_mesh_retain_compound(snap_mesh_cache_t *cache,
                                 uint32_t entity_id,
                                 const phys_convex_hull_t *hulls,
                                 const phys_convex_compound_t *compound) {
    if (!cache || !hulls || !compound) return;
    if (compound->child_count == 0) return;

    /* First pass: count total vertices and triangles across all children. */
    uint32_t total_verts = 0;
    uint32_t total_tris  = 0;
    for (uint32_t c = 0; c < compound->child_count; ++c) {
        const phys_convex_hull_t *h = &hulls[compound->child_hull_indices[c]];
        total_verts += h->vertex_count;
        total_tris  += count_hull_triangles_(h);
    }

    if (total_verts == 0 || total_tris == 0) return;

    uint32_t total_idx = total_tris * 3;
    float *positions = malloc(total_verts * 3 * sizeof(float));
    float *normals   = malloc(total_verts * 3 * sizeof(float));
    uint32_t *indices = malloc(total_idx * sizeof(uint32_t));
    if (!positions || !normals || !indices) {
        free(positions); free(normals); free(indices);
        return;
    }

    /* Second pass: merge all hull vertices and triangulated faces. */
    uint32_t vert_off = 0;
    uint32_t idx_off  = 0;
    for (uint32_t c = 0; c < compound->child_count; ++c) {
        const phys_convex_hull_t *h = &hulls[compound->child_hull_indices[c]];

        /* Copy vertices. */
        for (uint32_t v = 0; v < h->vertex_count; ++v) {
            positions[(vert_off + v) * 3 + 0] = h->vertices[v].x;
            positions[(vert_off + v) * 3 + 1] = h->vertices[v].y;
            positions[(vert_off + v) * 3 + 2] = h->vertices[v].z;
        }

        /* Compute per-vertex normals for this hull. */
        memset(&normals[vert_off * 3], 0, h->vertex_count * 3 * sizeof(float));
        for (uint32_t f = 0; f < h->face_count; ++f) {
            const phys_convex_face_t *face = &h->faces[f];
            for (uint32_t i = 0; i < face->index_count; ++i) {
                uint32_t vi = vert_off + h->indices[face->index_start + i];
                normals[vi * 3 + 0] += face->normal.x;
                normals[vi * 3 + 1] += face->normal.y;
                normals[vi * 3 + 2] += face->normal.z;
            }
        }
        for (uint32_t v = 0; v < h->vertex_count; ++v) {
            uint32_t vi = vert_off + v;
            float nx = normals[vi * 3 + 0];
            float ny = normals[vi * 3 + 1];
            float nz = normals[vi * 3 + 2];
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) {
                float inv = 1.0f / len;
                normals[vi * 3 + 0] *= inv;
                normals[vi * 3 + 1] *= inv;
                normals[vi * 3 + 2] *= inv;
            }
        }

        /* Fan-triangulate faces with vertex offset. */
        uint32_t written = triangulate_hull_faces_(h, vert_off, indices, idx_off);
        vert_off += h->vertex_count;
        idx_off  += written;
    }

    snap_mesh_cache_insert(cache, entity_id,
                            positions, normals, indices, total_verts, total_idx);

    free(positions);
    free(normals);
    free(indices);
}
