/**
 * @file mesh_brush.c
 * @brief Create convex mesh from half-plane intersection (brush workflow).
 *
 * Non-static functions (1 of 4): mesh_create_from_brush.
 *
 * Algorithm: compute all triple-plane intersection vertices, keep those
 * inside all half-spaces, then build convex hull faces from coplanar
 * vertex sets per plane.
 */
#include "ferrum/editor/mesh/mesh_brush.h"

#include <string.h>
#include <math.h>

#define BRUSH_EPS 1e-5f
#define BRUSH_MAX_VERTS 256

/* ------------------------------------------------------------------ */
/* Static: 3-plane intersection                                        */
/* ------------------------------------------------------------------ */

/** Solve intersection of 3 planes. Returns false if degenerate. */
static bool intersect3_(const mesh_brush_plane_t *a,
                         const mesh_brush_plane_t *b,
                         const mesh_brush_plane_t *c,
                         float out[3]) {
    /* Cramer's rule for 3x3 system:
     *   a.nx*x + a.ny*y + a.nz*z = a.dist
     *   b.nx*x + b.ny*y + b.nz*z = b.dist
     *   c.nx*x + c.ny*y + c.nz*z = c.dist */
    float det = a->normal[0]*(b->normal[1]*c->normal[2] - b->normal[2]*c->normal[1])
              - a->normal[1]*(b->normal[0]*c->normal[2] - b->normal[2]*c->normal[0])
              + a->normal[2]*(b->normal[0]*c->normal[1] - b->normal[1]*c->normal[0]);

    if (fabsf(det) < BRUSH_EPS) return false;

    float inv = 1.0f / det;
    out[0] = inv * (a->dist*(b->normal[1]*c->normal[2] - b->normal[2]*c->normal[1])
                  - a->normal[1]*(b->dist*c->normal[2] - b->normal[2]*c->dist)
                  + a->normal[2]*(b->dist*c->normal[1] - b->normal[1]*c->dist));
    out[1] = inv * (a->normal[0]*(b->dist*c->normal[2] - b->normal[2]*c->dist)
                  - a->dist*(b->normal[0]*c->normal[2] - b->normal[2]*c->normal[0])
                  + a->normal[2]*(b->normal[0]*c->dist - b->dist*c->normal[0]));
    out[2] = inv * (a->normal[0]*(b->normal[1]*c->dist - b->dist*c->normal[1])
                  - a->normal[1]*(b->normal[0]*c->dist - b->dist*c->normal[0])
                  + a->dist*(b->normal[0]*c->normal[1] - b->normal[1]*c->normal[0]));
    return true;
}

/** Check if point is inside all half-spaces (dot(n,P) <= dist + eps). */
static bool inside_all_(const float pt[3],
                         const mesh_brush_plane_t *planes,
                         uint32_t num_planes) {
    for (uint32_t i = 0; i < num_planes; i++) {
        float d = planes[i].normal[0]*pt[0]
                + planes[i].normal[1]*pt[1]
                + planes[i].normal[2]*pt[2];
        if (d > planes[i].dist + BRUSH_EPS) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Static: sort coplanar vertices in winding order                     */
/* ------------------------------------------------------------------ */

/** Sort vertex indices by angle around centroid on the plane. */
static void sort_winding_(float *positions, uint32_t *vis, uint32_t count,
                           const float nrm[3], const float center[3]) {
    /* Build local 2D axes on the plane */
    float ax_u[3], ax_v[3];
    /* Find a non-parallel vector to cross with normal */
    float ref[3] = {1, 0, 0};
    if (fabsf(nrm[0]) > 0.9f) { ref[0] = 0; ref[1] = 1; }
    /* u = cross(nrm, ref) */
    ax_u[0] = nrm[1]*ref[2] - nrm[2]*ref[1];
    ax_u[1] = nrm[2]*ref[0] - nrm[0]*ref[2];
    ax_u[2] = nrm[0]*ref[1] - nrm[1]*ref[0];
    float ulen = sqrtf(ax_u[0]*ax_u[0]+ax_u[1]*ax_u[1]+ax_u[2]*ax_u[2]);
    if (ulen < BRUSH_EPS) return;
    ax_u[0]/=ulen; ax_u[1]/=ulen; ax_u[2]/=ulen;
    /* v = cross(nrm, u) */
    ax_v[0] = nrm[1]*ax_u[2] - nrm[2]*ax_u[1];
    ax_v[1] = nrm[2]*ax_u[0] - nrm[0]*ax_u[2];
    ax_v[2] = nrm[0]*ax_u[1] - nrm[1]*ax_u[0];

    /* Compute angles */
    float angles[BRUSH_MAX_VERTS];
    for (uint32_t i = 0; i < count; i++) {
        float dx = positions[vis[i]*3+0] - center[0];
        float dy = positions[vis[i]*3+1] - center[1];
        float dz = positions[vis[i]*3+2] - center[2];
        float pu = dx*ax_u[0] + dy*ax_u[1] + dz*ax_u[2];
        float pv = dx*ax_v[0] + dy*ax_v[1] + dz*ax_v[2];
        angles[i] = atan2f(pv, pu);
    }

    /* Simple insertion sort by angle */
    for (uint32_t i = 1; i < count; i++) {
        float ka = angles[i];
        uint32_t kv = vis[i];
        int j = (int)i - 1;
        while (j >= 0 && angles[j] > ka) {
            angles[j+1] = angles[j];
            vis[j+1] = vis[j];
            j--;
        }
        angles[j+1] = ka;
        vis[j+1] = kv;
    }
}

/* ------------------------------------------------------------------ */
/* Static: deduplicate close vertices                                  */
/* ------------------------------------------------------------------ */

static uint32_t dedup_vert_(float *positions, uint32_t count,
                             const float pt[3]) {
    for (uint32_t i = 0; i < count; i++) {
        float dx = positions[i*3+0] - pt[0];
        float dy = positions[i*3+1] - pt[1];
        float dz = positions[i*3+2] - pt[2];
        if (dx*dx + dy*dy + dz*dz < BRUSH_EPS * BRUSH_EPS * 100) return i;
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_create_from_brush                                      */
/* ------------------------------------------------------------------ */

bool mesh_create_from_brush(mesh_slot_t *slot,
                            const mesh_brush_plane_t *planes,
                            uint32_t num_planes) {
    if (!slot || !planes || num_planes == 0) return false;

    mesh_slot_clear(slot);

    /* Step 1: find all valid vertices (triple-plane intersections) */
    float verts[BRUSH_MAX_VERTS * 3];
    uint32_t vert_count = 0;

    for (uint32_t i = 0; i < num_planes && vert_count < BRUSH_MAX_VERTS; i++) {
        for (uint32_t j = i+1; j < num_planes && vert_count < BRUSH_MAX_VERTS; j++) {
            for (uint32_t k = j+1; k < num_planes && vert_count < BRUSH_MAX_VERTS; k++) {
                float pt[3];
                if (!intersect3_(&planes[i], &planes[j], &planes[k], pt))
                    continue;
                if (!inside_all_(pt, planes, num_planes))
                    continue;
                /* Deduplicate */
                if (dedup_vert_(verts, vert_count, pt) != UINT32_MAX)
                    continue;

                verts[vert_count*3+0] = pt[0];
                verts[vert_count*3+1] = pt[1];
                verts[vert_count*3+2] = pt[2];
                vert_count++;
            }
        }
    }

    if (vert_count < 4) return false; /* Need at least a tetrahedron */

    /* Add vertices to slot */
    float zero_nrm[3] = {0, 0, 0};
    for (uint32_t i = 0; i < vert_count; i++) {
        mesh_slot_add_vertex(slot, &verts[i*3], zero_nrm);
    }

    /* Step 2: for each plane, collect coplanar vertices and triangulate */
    for (uint32_t pi = 0; pi < num_planes; pi++) {
        uint32_t face_vis[BRUSH_MAX_VERTS];
        uint32_t face_count = 0;
        float center[3] = {0, 0, 0};

        for (uint32_t v = 0; v < vert_count; v++) {
            float d = planes[pi].normal[0]*verts[v*3+0]
                    + planes[pi].normal[1]*verts[v*3+1]
                    + planes[pi].normal[2]*verts[v*3+2];
            if (fabsf(d - planes[pi].dist) < BRUSH_EPS * 10) {
                face_vis[face_count++] = v;
                center[0] += verts[v*3+0];
                center[1] += verts[v*3+1];
                center[2] += verts[v*3+2];
            }
        }

        if (face_count < 3) continue;
        center[0] /= (float)face_count;
        center[1] /= (float)face_count;
        center[2] /= (float)face_count;

        /* Sort vertices in winding order around the face normal */
        sort_winding_(verts, face_vis, face_count, planes[pi].normal, center);

        /* Fan triangulation from first vertex */
        for (uint32_t t = 1; t + 1 < face_count; t++) {
            mesh_slot_add_triangle(slot, face_vis[0], face_vis[t],
                                   face_vis[t+1], (uint16_t)pi);
        }
    }

    if (slot->index_count == 0) return false;

    /* Step 3: set normals from defining planes */
    memset(slot->normals, 0, slot->vertex_count * 3 * sizeof(float));
    uint32_t fc = slot->index_count / 3;
    for (uint32_t f = 0; f < fc; f++) {
        uint16_t pi = slot->polygroup_ids[f];
        for (int j = 0; j < 3; j++) {
            uint32_t vi = slot->indices[f*3+j];
            slot->normals[vi*3+0] += planes[pi].normal[0];
            slot->normals[vi*3+1] += planes[pi].normal[1];
            slot->normals[vi*3+2] += planes[pi].normal[2];
        }
    }
    for (uint32_t v = 0; v < slot->vertex_count; v++) {
        float *n = &slot->normals[v*3];
        float len = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (len > 1e-12f) { n[0]/=len; n[1]/=len; n[2]/=len; }
    }

    return true;
}
