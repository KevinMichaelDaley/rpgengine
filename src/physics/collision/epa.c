/**
 * @file epa.c
 * @brief EPA (Expanding Polytope Algorithm) for penetration depth.
 *
 * After GJK detects intersection, EPA expands the simplex into a convex
 * polytope and iteratively adds support points on the face closest to
 * the origin until convergence, yielding the minimum translation vector.
 *
 * Reference: "Collision Detection in Interactive 3D Environments"
 * (van den Bergen, 2003).
 *
 * Non-static functions (1):
 *   1. phys_epa_penetration
 */

#include "ferrum/physics/gjk_epa.h"
#include "ferrum/physics/phys_vec3_ops.h"

#include <float.h>
#include <math.h>
#include <string.h>

/** Maximum EPA vertices (Minkowski difference points). */
#define EPA_MAX_VERTS 128

/** Maximum EPA faces (triangular). */
#define EPA_MAX_FACES 256

/** EPA convergence tolerance. */
#define EPA_TOLERANCE 1e-4f

/** Maximum EPA iterations. */
#define EPA_MAX_ITER 64

/** EPA vertex: Minkowski difference point + contributing points. */
typedef struct epa_vertex {
    phys_vec3_t p;   /**< Minkowski difference point. */
    phys_vec3_t a;   /**< Support point on shape A. */
    phys_vec3_t b;   /**< Support point on shape B. */
} epa_vertex_t;

/** EPA triangular face. */
typedef struct epa_face {
    uint16_t v[3];       /**< Vertex indices (CCW from outside). */
    phys_vec3_t normal;  /**< Outward-facing unit normal. */
    float dist;          /**< Distance from origin to face plane. */
    uint8_t alive;       /**< 1 if active, 0 if removed. */
} epa_face_t;

/** EPA horizon edge (boundary between visible and non-visible faces). */
typedef struct epa_edge {
    uint16_t a, b;
} epa_edge_t;

/* ── Vector helpers (use phys_vec3_ops.h) ─────────────────────────── */

/* Shared simplex from GJK — defined in gjk.c. */
typedef struct gjk_vertex {
    phys_vec3_t p;
    phys_vec3_t a;
    phys_vec3_t b;
} gjk_vertex_t;

typedef struct gjk_simplex {
    gjk_vertex_t verts[4];
    int count;
} gjk_simplex_t;

extern _Thread_local gjk_simplex_t g_last_simplex;

/** Compute outward normal for a triangle face and its distance from origin.
 *  Returns false if face is degenerate. */
static bool epa_face_normal(const epa_vertex_t *verts, const epa_face_t *f,
                             phys_vec3_t *normal_out, float *dist_out) {
    phys_vec3_t a = verts[f->v[0]].p;
    phys_vec3_t b = verts[f->v[1]].p;
    phys_vec3_t c = verts[f->v[2]].p;
    phys_vec3_t ab = phys_vec3_sub(b, a);
    phys_vec3_t ac = phys_vec3_sub(c, a);
    phys_vec3_t n = phys_vec3_cross(ab, ac);
    float len = sqrtf(phys_vec3_dot(n, n));
    if (len < 1e-10f) return false;
    float inv_len = 1.0f / len;
    *normal_out = (phys_vec3_t){n.x * inv_len, n.y * inv_len, n.z * inv_len};
    *dist_out = phys_vec3_dot(*normal_out, a);
    return true;
}

/** Initialize an EPA face and compute its normal. Returns false if degenerate. */
static bool epa_init_face(epa_face_t *f, uint16_t va, uint16_t vb, uint16_t vc,
                           const epa_vertex_t *verts) {
    f->v[0] = va;
    f->v[1] = vb;
    f->v[2] = vc;
    f->alive = 1;
    if (!epa_face_normal(verts, f, &f->normal, &f->dist)) {
        f->alive = 0;
        return false;
    }
    /* Ensure normal points away from origin. */
    if (f->dist < 0) {
        /* Flip winding. */
        uint16_t tmp = f->v[1];
        f->v[1] = f->v[2];
        f->v[2] = tmp;
        f->normal = phys_vec3_neg(f->normal);
        f->dist = -f->dist;
    }
    return true;
}

bool phys_epa_penetration(phys_gjk_support_fn support_a, const void *shape_a,
                           phys_gjk_support_fn support_b, const void *shape_b,
                           phys_gjk_result_t *result) {
    if (!support_a || !support_b || !result) {
        return false;
    }

    gjk_simplex_t *simplex = &g_last_simplex;

    /* We need a tetrahedron to start EPA.  If GJK ended with fewer points,
       we need to build up to 4. */
    if (simplex->count < 4) {
        /* Try to expand simplex to tetrahedron by adding support points
           in orthogonal directions. */
        phys_vec3_t dirs[6] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };
        for (int i = 0; i < 6 && simplex->count < 4; i++) {
            gjk_vertex_t v;
            v.a = support_a(shape_a, dirs[i]);
            v.b = support_b(shape_b, phys_vec3_neg(dirs[i]));
            v.p = phys_vec3_sub(v.a, v.b);

            /* Check that this point isn't already in the simplex. */
            int dup = 0;
            for (int j = 0; j < simplex->count; j++) {
                phys_vec3_t diff = phys_vec3_sub(v.p, simplex->verts[j].p);
                if (phys_vec3_dot(diff, diff) < 1e-8f) { dup = 1; break; }
            }
            if (!dup) {
                simplex->verts[simplex->count++] = v;
            }
        }
    }

    if (simplex->count < 4) {
        /* Degenerate case — can't form tetrahedron. Return best guess. */
        result->penetration = 0.01f;
        result->normal = (phys_vec3_t){1, 0, 0};
        result->closest_a = simplex->verts[0].a;
        result->closest_b = simplex->verts[0].b;
        return true;
    }

    /* Copy simplex vertices to EPA vertex array. */
    epa_vertex_t verts[EPA_MAX_VERTS];
    uint32_t vert_count = 4;
    for (int i = 0; i < 4; i++) {
        verts[i].p = simplex->verts[i].p;
        verts[i].a = simplex->verts[i].a;
        verts[i].b = simplex->verts[i].b;
    }

    /* Build initial polytope from tetrahedron (4 faces). */
    epa_face_t faces[EPA_MAX_FACES];
    uint32_t face_count = 0;
    memset(faces, 0, sizeof(faces));

    /* The four faces of the tetrahedron, with consistent outward normals. */
    epa_init_face(&faces[0], 0, 1, 2, verts); face_count++;
    epa_init_face(&faces[1], 0, 2, 3, verts); face_count++;
    epa_init_face(&faces[2], 0, 3, 1, verts); face_count++;
    epa_init_face(&faces[3], 1, 3, 2, verts); face_count++;

    /* Verify all faces have normals pointing away from origin.
       Check by ensuring the centroid of the tet is behind each face. */
    phys_vec3_t centroid = {0};
    for (uint32_t i = 0; i < 4; i++) {
        centroid.x += verts[i].p.x * 0.25f;
        centroid.y += verts[i].p.y * 0.25f;
        centroid.z += verts[i].p.z * 0.25f;
    }
    for (uint32_t i = 0; i < face_count; i++) {
        if (!faces[i].alive) continue;
        float cd = phys_vec3_dot(faces[i].normal, centroid) - faces[i].dist;
        if (cd > 0) {
            /* Normal points toward centroid — flip. */
            uint16_t tmp = faces[i].v[1];
            faces[i].v[1] = faces[i].v[2];
            faces[i].v[2] = tmp;
            faces[i].normal = phys_vec3_neg(faces[i].normal);
            faces[i].dist = -faces[i].dist;
        }
    }

    /* EPA iteration loop. */
    for (int iter = 0; iter < EPA_MAX_ITER; iter++) {
        /* Find the face closest to the origin. */
        uint32_t closest_face = UINT32_MAX;
        float closest_dist = FLT_MAX;
        for (uint32_t i = 0; i < face_count; i++) {
            if (!faces[i].alive) continue;
            if (faces[i].dist < closest_dist) {
                closest_dist = faces[i].dist;
                closest_face = i;
            }
        }

        if (closest_face == UINT32_MAX) break;

        /* Get new support point along closest face normal. */
        phys_vec3_t search_dir = faces[closest_face].normal;
        epa_vertex_t new_vert;
        new_vert.a = support_a(shape_a, search_dir);
        new_vert.b = support_b(shape_b, phys_vec3_neg(search_dir));
        new_vert.p = phys_vec3_sub(new_vert.a, new_vert.b);

        float new_dist = phys_vec3_dot(new_vert.p, search_dir);

        /* Check convergence: if new point doesn't extend polytope
           significantly, we're done. */
        if (new_dist - closest_dist < EPA_TOLERANCE) {
            /* Converged — extract result from closest face. */
            result->penetration = closest_dist;
            result->normal = faces[closest_face].normal;

            /* Compute barycentric coords of origin projected onto face. */
            uint16_t i0 = faces[closest_face].v[0];
            uint16_t i1 = faces[closest_face].v[1];
            uint16_t i2 = faces[closest_face].v[2];

            phys_vec3_t va = verts[i0].p;
            phys_vec3_t vb = verts[i1].p;
            phys_vec3_t vc = verts[i2].p;
            phys_vec3_t ab = phys_vec3_sub(vb, va);
            phys_vec3_t ac = phys_vec3_sub(vc, va);
            phys_vec3_t n = faces[closest_face].normal;
            /* Project origin onto face plane: p = origin - dist * normal = -dist * normal,
               but we want it relative to va. */
            phys_vec3_t proj = {
                -closest_dist * n.x - va.x,
                -closest_dist * n.y - va.y,
                -closest_dist * n.z - va.z,
            };
            /* Negate because origin projects to dist*normal on the face. */
            proj = (phys_vec3_t){
                closest_dist * n.x - va.x,
                closest_dist * n.y - va.y,
                closest_dist * n.z - va.z,
            };

            float d00 = phys_vec3_dot(ab, ab);
            float d01 = phys_vec3_dot(ab, ac);
            float d11 = phys_vec3_dot(ac, ac);
            float d20 = phys_vec3_dot(proj, ab);
            float d21 = phys_vec3_dot(proj, ac);
            float denom = d00 * d11 - d01 * d01;
            float bv = 0.333f, bw = 0.333f;
            if (fabsf(denom) > 1e-12f) {
                bv = (d11 * d20 - d01 * d21) / denom;
                bw = (d00 * d21 - d01 * d20) / denom;
            }
            float bu = 1.0f - bv - bw;
            if (bu < 0) bu = 0;
            if (bv < 0) bv = 0;
            if (bw < 0) bw = 0;
            float sum = bu + bv + bw;
            if (sum > 1e-10f) { bu /= sum; bv /= sum; bw /= sum; }

            result->closest_a = (phys_vec3_t){
                verts[i0].a.x * bu + verts[i1].a.x * bv + verts[i2].a.x * bw,
                verts[i0].a.y * bu + verts[i1].a.y * bv + verts[i2].a.y * bw,
                verts[i0].a.z * bu + verts[i1].a.z * bv + verts[i2].a.z * bw,
            };
            result->closest_b = (phys_vec3_t){
                verts[i0].b.x * bu + verts[i1].b.x * bv + verts[i2].b.x * bw,
                verts[i0].b.y * bu + verts[i1].b.y * bv + verts[i2].b.y * bw,
                verts[i0].b.z * bu + verts[i1].b.z * bv + verts[i2].b.z * bw,
            };
            return true;
        }

        /* Add new vertex to polytope. */
        if (vert_count >= EPA_MAX_VERTS) break;
        uint16_t new_idx = (uint16_t)vert_count;
        verts[vert_count++] = new_vert;

        /* Find faces visible from new point and collect horizon edges. */
        uint8_t visible[EPA_MAX_FACES];
        memset(visible, 0, face_count);
        for (uint32_t i = 0; i < face_count; i++) {
            if (!faces[i].alive) continue;
            float d = phys_vec3_dot(faces[i].normal, new_vert.p) - faces[i].dist;
            if (d > EPA_TOLERANCE * 0.1f) {
                visible[i] = 1;
            }
        }

        /* Collect horizon edges. */
        epa_edge_t horizon[EPA_MAX_FACES * 3];
        uint32_t horizon_count = 0;

        for (uint32_t i = 0; i < face_count; i++) {
            if (!faces[i].alive || !visible[i]) continue;
            for (int ei = 0; ei < 3; ei++) {
                uint16_t ea = faces[i].v[ei];
                uint16_t eb = faces[i].v[(ei + 1) % 3];

                int adj_visible = 0;
                for (uint32_t j = 0; j < face_count; j++) {
                    if (j == i || !faces[j].alive) continue;
                    for (int ej = 0; ej < 3; ej++) {
                        if (faces[j].v[ej] == eb && faces[j].v[(ej + 1) % 3] == ea) {
                            if (visible[j]) adj_visible = 1;
                            break;
                        }
                    }
                    if (adj_visible) break;
                }

                if (!adj_visible && horizon_count < EPA_MAX_FACES * 3u) {
                    horizon[horizon_count].a = ea;
                    horizon[horizon_count].b = eb;
                    horizon_count++;
                }
            }
        }

        /* Remove visible faces. */
        for (uint32_t i = 0; i < face_count; i++) {
            if (visible[i]) faces[i].alive = 0;
        }

        /* Add new faces from horizon edges to new vertex. */
        for (uint32_t i = 0; i < horizon_count; i++) {
            if (face_count >= EPA_MAX_FACES) break;
            epa_init_face(&faces[face_count], horizon[i].a, horizon[i].b,
                          new_idx, verts);
            face_count++;
        }
    }

    /* Failed to converge — return best available. */
    uint32_t best = UINT32_MAX;
    float best_dist = FLT_MAX;
    for (uint32_t i = 0; i < face_count; i++) {
        if (!faces[i].alive) continue;
        if (faces[i].dist < best_dist) {
            best_dist = faces[i].dist;
            best = i;
        }
    }
    if (best != UINT32_MAX) {
        result->penetration = best_dist;
        result->normal = faces[best].normal;
        result->closest_a = verts[faces[best].v[0]].a;
        result->closest_b = verts[faces[best].v[0]].b;
        return true;
    }

    return false;
}
