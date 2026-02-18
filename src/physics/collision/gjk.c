/**
 * @file gjk.c
 * @brief GJK (Gilbert-Johnson-Keerthi) intersection test for convex shapes.
 *
 * Uses the Minkowski difference support function to iteratively build
 * a simplex that either contains the origin (intersection) or proves
 * shapes are separated (closest point computation).
 *
 * Reference: "A fast procedure for computing the distance between
 * complex objects in three-dimensional space" (Gilbert et al., 1988).
 *
 * Non-static functions (1):
 *   1. phys_gjk_intersect
 */

#include "ferrum/physics/gjk_epa.h"

#include <float.h>
#include <math.h>
#include <string.h>

/** Maximum GJK iterations before giving up. */
#define GJK_MAX_ITER 64

/** GJK simplex vertex: stores Minkowski difference point and contributing
 *  points from each shape (for closest-point extraction). */
typedef struct gjk_vertex {
    phys_vec3_t p;   /**< Minkowski difference point (a - b). */
    phys_vec3_t a;   /**< Support point on shape A. */
    phys_vec3_t b;   /**< Support point on shape B. */
} gjk_vertex_t;

/** GJK simplex (1–4 vertices). */
typedef struct gjk_simplex {
    gjk_vertex_t verts[4];
    int count;
} gjk_simplex_t;

/* Shared simplex for EPA to pick up after GJK (external linkage). */
_Thread_local gjk_simplex_t g_last_simplex;

/* ── Vector helpers (local, avoid header dependency) ──────────── */

static float dot3(phys_vec3_t a, phys_vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static phys_vec3_t sub3(phys_vec3_t a, phys_vec3_t b) {
    return (phys_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static phys_vec3_t neg3(phys_vec3_t v) {
    return (phys_vec3_t){-v.x, -v.y, -v.z};
}

static phys_vec3_t cross3(phys_vec3_t a, phys_vec3_t b) {
    return (phys_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

/** Compute Minkowski difference support: support_a(dir) - support_b(-dir). */
static gjk_vertex_t gjk_support(phys_gjk_support_fn sa, const void *da,
                                 phys_gjk_support_fn sb, const void *db,
                                 phys_vec3_t dir) {
    gjk_vertex_t v;
    v.a = sa(da, dir);
    v.b = sb(db, neg3(dir));
    v.p = sub3(v.a, v.b);
    return v;
}

/* ── Simplex cases ─────────────────────────────────────────────── */

/**
 * Line case: simplex has 2 points {B, A} where A is newest.
 * Determine which Voronoi region of the line segment the origin lies in,
 * and reduce the simplex accordingly.
 * Returns new search direction via *dir.
 */
static int gjk_do_line(gjk_simplex_t *s, phys_vec3_t *dir) {
    phys_vec3_t a = s->verts[1].p;  /* newest */
    phys_vec3_t b = s->verts[0].p;
    phys_vec3_t ab = sub3(b, a);
    phys_vec3_t ao = neg3(a);

    if (dot3(ab, ao) > 0) {
        /* Origin is in the direction of B from A — keep line. */
        *dir = cross3(cross3(ab, ao), ab);
        /* If direction is zero (origin on line), perturb. */
        if (dot3(*dir, *dir) < 1e-14f) {
            *dir = cross3(ab, (phys_vec3_t){1, 0, 0});
            if (dot3(*dir, *dir) < 1e-14f) {
                *dir = cross3(ab, (phys_vec3_t){0, 1, 0});
            }
        }
        return 0;
    }

    /* Origin is behind A — reduce to point A. */
    s->verts[0] = s->verts[1];
    s->count = 1;
    *dir = ao;
    return 0;
}

/**
 * Triangle case: simplex has 3 points {C, B, A} where A is newest.
 * Returns 1 if origin is inside the triangle prism (need tetrahedron).
 */
static int gjk_do_triangle(gjk_simplex_t *s, phys_vec3_t *dir) {
    phys_vec3_t a = s->verts[2].p;  /* newest */
    phys_vec3_t b = s->verts[1].p;
    phys_vec3_t c = s->verts[0].p;
    phys_vec3_t ab = sub3(b, a);
    phys_vec3_t ac = sub3(c, a);
    phys_vec3_t ao = neg3(a);
    phys_vec3_t abc = cross3(ab, ac);

    /* Check if origin is outside edge AB. */
    phys_vec3_t ab_perp = cross3(ab, abc);
    if (dot3(ab_perp, ao) > 0) {
        /* Reduce to line {B, A}. */
        s->verts[0] = s->verts[1];
        s->verts[1] = s->verts[2];
        s->count = 2;
        return gjk_do_line(s, dir);
    }

    /* Check if origin is outside edge AC. */
    phys_vec3_t ac_perp = cross3(abc, ac);
    if (dot3(ac_perp, ao) > 0) {
        /* Reduce to line {C, A}. */
        s->verts[1] = s->verts[2];
        s->count = 2;
        return gjk_do_line(s, dir);
    }

    /* Origin is inside the triangle region — check which side. */
    if (dot3(abc, ao) > 0) {
        /* Above triangle — search upward. */
        *dir = abc;
    } else {
        /* Below triangle — flip winding and search downward. */
        gjk_vertex_t tmp = s->verts[0];
        s->verts[0] = s->verts[1];
        s->verts[1] = tmp;
        *dir = neg3(abc);
    }
    return 0;
}

/**
 * Tetrahedron case: simplex has 4 points {D, C, B, A} where A is newest.
 * Returns 1 if origin is inside the tetrahedron.
 */
static int gjk_do_tetrahedron(gjk_simplex_t *s, phys_vec3_t *dir) {
    phys_vec3_t a = s->verts[3].p;  /* newest */
    phys_vec3_t b = s->verts[2].p;
    phys_vec3_t c = s->verts[1].p;
    phys_vec3_t d = s->verts[0].p;
    phys_vec3_t ab = sub3(b, a);
    phys_vec3_t ac = sub3(c, a);
    phys_vec3_t ad = sub3(d, a);
    phys_vec3_t ao = neg3(a);

    /* Check each face (excluding the base BCD since A was just added
       and we came from the direction of A). */

    /* Face ABC (normal points away from D). */
    phys_vec3_t abc = cross3(ab, ac);
    if (dot3(abc, ad) > 0) abc = neg3(abc); /* ensure outward */
    if (dot3(abc, ao) > 0) {
        /* Origin is outside face ABC — reduce to triangle {C, B, A}. */
        s->verts[0] = s->verts[1];
        s->verts[1] = s->verts[2];
        s->verts[2] = s->verts[3];
        s->count = 3;
        return gjk_do_triangle(s, dir);
    }

    /* Face ACD (normal points away from B). */
    phys_vec3_t acd = cross3(ac, ad);
    if (dot3(acd, ab) > 0) acd = neg3(acd);
    if (dot3(acd, ao) > 0) {
        /* Origin is outside face ACD — reduce to triangle {D, C, A}. */
        s->verts[2] = s->verts[3];
        s->count = 3;
        return gjk_do_triangle(s, dir);
    }

    /* Face ADB (normal points away from C). */
    phys_vec3_t adb = cross3(ad, ab);
    if (dot3(adb, ac) > 0) adb = neg3(adb);
    if (dot3(adb, ao) > 0) {
        /* Origin is outside face ADB — reduce to triangle {B, D, A}. */
        s->verts[1] = s->verts[0];
        s->verts[0] = s->verts[2];
        s->verts[2] = s->verts[3];
        s->count = 3;
        return gjk_do_triangle(s, dir);
    }

    /* Origin is inside all faces — tetrahedron contains origin! */
    return 1;
}

/**
 * Process simplex and compute next search direction.
 * Returns 1 if origin is contained (intersection found).
 */
static int gjk_do_simplex(gjk_simplex_t *s, phys_vec3_t *dir) {
    switch (s->count) {
    case 2: return gjk_do_line(s, dir);
    case 3: return gjk_do_triangle(s, dir);
    case 4: return gjk_do_tetrahedron(s, dir);
    default: return 0;
    }
}

/* ── Closest point extraction from final simplex ──────────────── */

/** Compute closest point on simplex to origin and extract barycentric
 *  coordinates to reconstruct contact points on A and B. */
static void gjk_closest_points(const gjk_simplex_t *s,
                                phys_vec3_t *closest_a,
                                phys_vec3_t *closest_b,
                                float *dist_out) {
    if (s->count == 1) {
        *closest_a = s->verts[0].a;
        *closest_b = s->verts[0].b;
        *dist_out = sqrtf(dot3(s->verts[0].p, s->verts[0].p));
        return;
    }

    if (s->count == 2) {
        /* Closest point on line segment to origin. */
        phys_vec3_t a = s->verts[0].p;
        phys_vec3_t b = s->verts[1].p;
        phys_vec3_t ab = sub3(b, a);
        float ab2 = dot3(ab, ab);
        float t = 0.5f;
        if (ab2 > 1e-12f) {
            t = -dot3(a, ab) / ab2;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
        }
        float s0 = 1.0f - t;
        *closest_a = (phys_vec3_t){
            s->verts[0].a.x * s0 + s->verts[1].a.x * t,
            s->verts[0].a.y * s0 + s->verts[1].a.y * t,
            s->verts[0].a.z * s0 + s->verts[1].a.z * t,
        };
        *closest_b = (phys_vec3_t){
            s->verts[0].b.x * s0 + s->verts[1].b.x * t,
            s->verts[0].b.y * s0 + s->verts[1].b.y * t,
            s->verts[0].b.z * s0 + s->verts[1].b.z * t,
        };
        phys_vec3_t cp = {
            a.x * s0 + b.x * t,
            a.y * s0 + b.y * t,
            a.z * s0 + b.z * t,
        };
        *dist_out = sqrtf(dot3(cp, cp));
        return;
    }

    /* Triangle: project origin onto plane, compute barycentric coords. */
    phys_vec3_t a = s->verts[0].p;
    phys_vec3_t b = s->verts[1].p;
    phys_vec3_t c = s->verts[2].p;
    phys_vec3_t ab = sub3(b, a);
    phys_vec3_t ac = sub3(c, a);
    phys_vec3_t ao = neg3(a);

    float d00 = dot3(ab, ab);
    float d01 = dot3(ab, ac);
    float d11 = dot3(ac, ac);
    float d20 = dot3(ao, ab);
    float d21 = dot3(ao, ac);
    float denom = d00 * d11 - d01 * d01;

    float v = 0.333f, w = 0.333f;
    if (fabsf(denom) > 1e-12f) {
        v = (d11 * d20 - d01 * d21) / denom;
        w = (d00 * d21 - d01 * d20) / denom;
    }
    float u = 1.0f - v - w;

    /* Clamp to triangle. */
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (w < 0) w = 0;
    float sum = u + v + w;
    if (sum > 1e-10f) { u /= sum; v /= sum; w /= sum; }

    *closest_a = (phys_vec3_t){
        s->verts[0].a.x * u + s->verts[1].a.x * v + s->verts[2].a.x * w,
        s->verts[0].a.y * u + s->verts[1].a.y * v + s->verts[2].a.y * w,
        s->verts[0].a.z * u + s->verts[1].a.z * v + s->verts[2].a.z * w,
    };
    *closest_b = (phys_vec3_t){
        s->verts[0].b.x * u + s->verts[1].b.x * v + s->verts[2].b.x * w,
        s->verts[0].b.y * u + s->verts[1].b.y * v + s->verts[2].b.y * w,
        s->verts[0].b.z * u + s->verts[1].b.y * v + s->verts[2].b.z * w,
    };
    phys_vec3_t cp = {
        a.x * u + b.x * v + c.x * w,
        a.y * u + b.y * v + c.y * w,
        a.z * u + b.z * v + c.z * w,
    };
    *dist_out = sqrtf(dot3(cp, cp));
}

/* ── Public API ────────────────────────────────────────────────── */

bool phys_gjk_intersect(phys_gjk_support_fn support_a, const void *shape_a,
                         phys_gjk_support_fn support_b, const void *shape_b,
                         phys_gjk_result_t *result) {
    if (!support_a || !support_b || !result) {
        if (result) {
            memset(result, 0, sizeof(*result));
        }
        return false;
    }

    memset(result, 0, sizeof(*result));

    /* Initial search direction: from B's center toward A's center.
       Use (1,0,0) as a reasonable default. */
    phys_vec3_t dir = {1.0f, 0.0f, 0.0f};

    gjk_simplex_t simplex;
    simplex.count = 0;

    /* First support point. */
    gjk_vertex_t v = gjk_support(support_a, shape_a, support_b, shape_b, dir);
    simplex.verts[0] = v;
    simplex.count = 1;

    /* Search toward origin from first point. */
    dir = neg3(v.p);
    if (dot3(dir, dir) < 1e-14f) {
        /* First support point is at origin — shapes intersect. */
        result->intersecting = true;
        g_last_simplex = simplex;
        return true;
    }

    for (int iter = 0; iter < GJK_MAX_ITER; iter++) {
        v = gjk_support(support_a, shape_a, support_b, shape_b, dir);

        /* If the new point didn't pass the origin, shapes are separated. */
        if (dot3(v.p, dir) < 0) {
            result->intersecting = false;
            gjk_closest_points(&simplex, &result->closest_a,
                               &result->closest_b, &result->distance);
            return false;
        }

        /* Add to simplex. */
        simplex.verts[simplex.count] = v;
        simplex.count++;

        /* Process simplex — check if origin is enclosed. */
        if (gjk_do_simplex(&simplex, &dir)) {
            result->intersecting = true;
            g_last_simplex = simplex;
            return true;
        }

        /* Safety: if direction is zero, bail. */
        if (dot3(dir, dir) < 1e-14f) {
            result->intersecting = true;
            g_last_simplex = simplex;
            return true;
        }
    }

    /* Failed to converge — treat as non-intersecting. */
    result->intersecting = false;
    gjk_closest_points(&simplex, &result->closest_a,
                       &result->closest_b, &result->distance);
    return false;
}
