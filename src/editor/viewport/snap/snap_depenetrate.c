/**
 * @file snap_depenetrate.c
 * @brief Depenetration for surface snap.
 *
 * snap_depenetrate_plane: vertex projection against a single plane.
 * snap_depenetrate_vs_tris: combined tri-tri SAT + bidirectional
 *   vertex-vs-face for full depenetration (containment, edge-face,
 *   edge-edge).
 *
 * Non-static functions (2 / 4 limit):
 *   snap_depenetrate_plane
 *   snap_depenetrate_vs_tris
 */

#include "ferrum/editor/viewport/snap/snap_depenetrate.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/physics/mesh_narrowphase.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec4.h"

#include <math.h>
#include <string.h>

/* ---- Helpers ---- */

/**
 * @brief Transform a snap_mesh_t vertex to world space.
 */
static vec3_t transform_vertex_(const snap_mesh_t *mesh,
                                  const mat4_t *model,
                                  uint32_t vertex_idx) {
    vec4_t local = {
        mesh->positions[vertex_idx * 3 + 0],
        mesh->positions[vertex_idx * 3 + 1],
        mesh->positions[vertex_idx * 3 + 2],
        1.0f
    };
    vec4_t world = mat4_mul_vec4(*model, local);
    return (vec3_t){world.x, world.y, world.z};
}

/**
 * @brief Check whether a point projects inside a triangle (half-plane test).
 *
 * @param p   Point to test (must be on or near the triangle's plane).
 * @param a   Triangle vertex 0.
 * @param b   Triangle vertex 1.
 * @param c   Triangle vertex 2.
 * @param fn  Triangle face normal (cross(b-a, c-a), not necessarily unit).
 * @return true if p projects inside triangle abc.
 */
static bool point_in_triangle_(vec3_t p, vec3_t a, vec3_t b, vec3_t c,
                                 vec3_t fn) {
    vec3_t ab = {b.x - a.x, b.y - a.y, b.z - a.z};
    vec3_t ap = {p.x - a.x, p.y - a.y, p.z - a.z};
    vec3_t c0 = {ab.y * ap.z - ab.z * ap.y,
                 ab.z * ap.x - ab.x * ap.z,
                 ab.x * ap.y - ab.y * ap.x};
    if (c0.x * fn.x + c0.y * fn.y + c0.z * fn.z < 0.0f) return false;

    vec3_t bc = {c.x - b.x, c.y - b.y, c.z - b.z};
    vec3_t bp = {p.x - b.x, p.y - b.y, p.z - b.z};
    vec3_t c1 = {bc.y * bp.z - bc.z * bp.y,
                 bc.z * bp.x - bc.x * bp.z,
                 bc.x * bp.y - bc.y * bp.x};
    if (c1.x * fn.x + c1.y * fn.y + c1.z * fn.z < 0.0f) return false;

    vec3_t ca = {a.x - c.x, a.y - c.y, a.z - c.z};
    vec3_t cp = {p.x - c.x, p.y - c.y, p.z - c.z};
    vec3_t c2 = {ca.y * cp.z - ca.z * cp.y,
                 ca.z * cp.x - ca.x * cp.z,
                 ca.x * cp.y - ca.y * cp.x};
    if (c2.x * fn.x + c2.y * fn.y + c2.z * fn.z < 0.0f) return false;

    return true;
}

/**
 * @brief Test one point against one triangle for containment penetration.
 *
 * If the point is behind the face (on the interior side) and projects
 * inside the triangle, records the penetration depth and normal.
 *
 * Updates min_pen/min_normal to track the SHALLOWEST face (nearest
 * escape) for this vertex, and increments *hit_count.
 *
 * The caller uses hit_count to verify the vertex is behind multiple
 * faces (required for true containment in a convex shape — a vertex
 * outside a convex shape may be behind one face but not others).
 *
 * @param point      Point to test.
 * @param ta,tb,tc   Triangle vertices.
 * @param min_pen    In/out: minimum penetration for this vertex so far.
 * @param min_normal In/out: normal of shallowest face.
 * @param hit_count  In/out: number of faces this vertex is behind.
 */
static void check_point_vs_tri_(vec3_t point,
                                  vec3_t ta, vec3_t tb, vec3_t tc,
                                  float *min_pen, vec3_t *min_normal,
                                  int *hit_count,
                                  vec3_t *hit_normals,
                                  int max_hit_normals) {
    vec3_t ab = {tb.x - ta.x, tb.y - ta.y, tb.z - ta.z};
    vec3_t ac = {tc.x - ta.x, tc.y - ta.y, tc.z - ta.z};
    vec3_t fn = {ab.y * ac.z - ab.z * ac.y,
                 ab.z * ac.x - ab.x * ac.z,
                 ab.x * ac.y - ab.y * ac.x};

    float fn_len = sqrtf(fn.x * fn.x + fn.y * fn.y + fn.z * fn.z);
    if (fn_len < 1e-8f) return;

    vec3_t n = {fn.x / fn_len, fn.y / fn_len, fn.z / fn_len};

    float dx = point.x - ta.x;
    float dy = point.y - ta.y;
    float dz = point.z - ta.z;
    float dist = dx * n.x + dy * n.y + dz * n.z;

    if (dist >= 0.0f) return;
    float pen = -dist;

    vec3_t proj = {point.x - dist * n.x,
                   point.y - dist * n.y,
                   point.z - dist * n.z};

    if (!point_in_triangle_(proj, ta, tb, tc, fn)) return;

    /* Track shallowest face — nearest escape for this vertex. */
    if (pen < *min_pen) {
        *min_pen = pen;
        *min_normal = n;
    }

    /* Count this as a hit for containment verification.
     * Only count if the normal direction is distinct from previously
     * seen normals (avoid double-counting two triangles of the same
     * face). A 0.99 dot threshold treats normals within ~8° as same. */
    for (int i = 0; i < *hit_count; ++i) {
        float d = hit_normals[i].x * n.x +
                  hit_normals[i].y * n.y +
                  hit_normals[i].z * n.z;
        if (d > 0.99f) return;  /* Same face direction. */
    }
    if (*hit_count < max_hit_normals) {
        hit_normals[*hit_count] = n;
    }
    (*hit_count)++;
}

/* ---- Public API ---- */

bool snap_depenetrate_plane(const snap_mesh_t *mesh_b,
                             const mat4_t *model_b,
                             vec3_t surface_point,
                             vec3_t surface_normal,
                             snap_depenetrate_result_t *result)
{
    if (!mesh_b || !model_b || !result) return false;
    if (!mesh_b->positions) return false;

    memset(result, 0, sizeof(*result));

    float max_depth = 0.0f;

    for (uint32_t i = 0; i < mesh_b->vertex_count; ++i) {
        vec3_t world = transform_vertex_(mesh_b, model_b, i);

        float dx = world.x - surface_point.x;
        float dy = world.y - surface_point.y;
        float dz = world.z - surface_point.z;
        float signed_dist = dx * surface_normal.x +
                           dy * surface_normal.y +
                           dz * surface_normal.z;

        if (signed_dist < -max_depth) {
            max_depth = -signed_dist;
        }
    }

    if (max_depth <= 0.0f) return false;

    result->penetration = max_depth;
    result->push = (vec3_t){
        surface_normal.x * max_depth,
        surface_normal.y * max_depth,
        surface_normal.z * max_depth
    };

    return true;
}

bool snap_depenetrate_vs_tris(const snap_mesh_t *mesh_b,
                               const mat4_t *model_b,
                               const vec3_t *env_tris,
                               uint32_t env_tri_count,
                               snap_depenetrate_result_t *result)
{
    if (!mesh_b || !model_b || !env_tris || !result) return false;
    if (!mesh_b->positions || !mesh_b->indices) return false;
    if (env_tri_count == 0) return false;

    memset(result, 0, sizeof(*result));

    uint32_t ent_tri_count = mesh_b->index_count / 3;
    if (ent_tri_count == 0) return false;

    float max_pen = 0.0f;
    vec3_t best_normal = {0, 0, 0};

    /* --- Pass 1: Entity vertices vs env faces (containment) ---
     * For each entity vertex, find the shallowest env face it's behind
     * (nearest escape). Then take the deepest such escape across all
     * vertices. This correctly handles convex containment: a vertex
     * inside a box is behind all 6 faces, but only the nearest face
     * gives the correct push direction. */
    for (uint32_t vi = 0; vi < mesh_b->vertex_count; ++vi) {
        vec3_t v = transform_vertex_(mesh_b, model_b, vi);
        float vtx_min_pen = 1e30f;
        vec3_t vtx_min_normal = {0, 0, 0};
        int vtx_hits = 0;
        vec3_t vtx_hit_normals[6]; /* Max 6 face directions for a box. */
        for (uint32_t ti = 0; ti < env_tri_count; ++ti) {
            check_point_vs_tri_(v,
                env_tris[ti * 3 + 0], env_tris[ti * 3 + 1],
                env_tris[ti * 3 + 2],
                &vtx_min_pen, &vtx_min_normal, &vtx_hits,
                vtx_hit_normals, 6);
        }
        /* Require vertex to be behind >= 2 distinct face directions
         * to count as truly contained. A vertex outside a convex shape
         * might project inside one face from the wrong side, but won't
         * be behind multiple non-parallel faces. */
        if (vtx_hits >= 2 && vtx_min_pen > max_pen) {
            max_pen = vtx_min_pen;
            best_normal = vtx_min_normal;
        }
    }

    /* --- Pass 2: Env vertices vs entity faces (reverse containment) ---
     * Same min-per-vertex, max-across-vertices logic, but reversed:
     * check env vertices against entity faces. Catches env corners
     * inside entity surfaces (e.g., box corner inside capsule). */
    for (uint32_t ei = 0; ei < env_tri_count * 3; ++ei) {
        vec3_t ev = env_tris[ei];
        float vtx_min_pen = 1e30f;
        vec3_t vtx_min_normal = {0, 0, 0};
        int vtx_hits = 0;
        vec3_t vtx_hit_normals[6];
        for (uint32_t ti = 0; ti < ent_tri_count; ++ti) {
            vec3_t ea = transform_vertex_(mesh_b, model_b,
                                            mesh_b->indices[ti * 3 + 0]);
            vec3_t eb = transform_vertex_(mesh_b, model_b,
                                            mesh_b->indices[ti * 3 + 1]);
            vec3_t ec = transform_vertex_(mesh_b, model_b,
                                            mesh_b->indices[ti * 3 + 2]);
            check_point_vs_tri_(ev, ea, eb, ec,
                                  &vtx_min_pen, &vtx_min_normal, &vtx_hits,
                                  vtx_hit_normals, 6);
        }
        /* For reverse containment, push direction is inverted:
         * the normal points outward from entity face, but we need
         * to push the entity AWAY from the env vertex, so negate. */
        if (vtx_hits >= 2 && vtx_min_pen > max_pen) {
            max_pen = vtx_min_pen;
            best_normal = (vec3_t){
                -vtx_min_normal.x, -vtx_min_normal.y, -vtx_min_normal.z
            };
        }
    }

    /* --- Pass 3: Triangle-vs-triangle SAT (edge crossings) ---
     * Catches edge-face and edge-edge intersections that the
     * vertex-vs-face passes miss (e.g., box edge crossing through
     * capsule cylinder face without any vertex being contained).
     *
     * SAT returns the minimum separation axis for each pair. We take
     * the deepest such contact, but only the push-direction normal —
     * the push magnitude is clamped to the minimum of SAT depth and
     * the vertex-vs-face depth, to avoid overshooting. */
    float sat_max_pen = 0.0f;
    vec3_t sat_best_normal = {0, 0, 0};

    for (uint32_t ei = 0; ei < ent_tri_count; ++ei) {
        vec3_t ea = transform_vertex_(mesh_b, model_b,
                                        mesh_b->indices[ei * 3 + 0]);
        vec3_t eb = transform_vertex_(mesh_b, model_b,
                                        mesh_b->indices[ei * 3 + 1]);
        vec3_t ec = transform_vertex_(mesh_b, model_b,
                                        mesh_b->indices[ei * 3 + 2]);
        phys_triangle_t ent_tri;
        ent_tri.v[0] = ea;
        ent_tri.v[1] = eb;
        ent_tri.v[2] = ec;

        for (uint32_t ti = 0; ti < env_tri_count; ++ti) {
            phys_triangle_t env_tri;
            env_tri.v[0] = env_tris[ti * 3 + 0];
            env_tri.v[1] = env_tris[ti * 3 + 1];
            env_tri.v[2] = env_tris[ti * 3 + 2];

            phys_contact_point_t contact;
            if (!phys_triangle_vs_triangle(&env_tri, &ent_tri,
                                             0.0f, &contact)) {
                continue;
            }

            /* contact.normal points from env (A) toward entity (B). */
            if (contact.penetration > sat_max_pen) {
                sat_max_pen = contact.penetration;
                sat_best_normal = contact.normal;
            }
        }
    }

    /* Merge SAT result with vertex-vs-face result.
     * Use whichever pass found deeper penetration. */
    if (sat_max_pen > max_pen) {
        max_pen = sat_max_pen;
        best_normal = sat_best_normal;
    }

    if (max_pen < 1e-5f) return false;

    result->penetration = max_pen;
    result->push = (vec3_t){
        best_normal.x * max_pen,
        best_normal.y * max_pen,
        best_normal.z * max_pen
    };

    return true;
}
