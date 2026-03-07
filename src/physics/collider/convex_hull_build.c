/**
 * @file convex_hull_build.c
 * @brief Incremental convex hull construction from a point cloud.
 *
 * Uses an incremental algorithm:
 *   1. Find initial tetrahedron from 4 non-coplanar points
 *   2. For each remaining point, if outside current hull:
 *      a. Find all faces visible from the point
 *      b. Remove visible faces, add new faces from horizon edges to point
 *   3. Extract final vertex/face/index arrays
 *
 * Accepts up to PHYS_CONVEX_BUILD_MAX_INPUT input points but produces
 * an output hull with at most PHYS_CONVEX_MAX_VERTS vertices.
 *
 * Non-static functions (1):
 *   1. phys_convex_hull_build
 */

#include "ferrum/physics/convex_hull.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Maximum input points accepted by hull build. */
#define BUILD_MAX_INPUT 131072u

/* Maximum faces during construction (more than final hull due to
   intermediate states).  For N<=64 output verts, the hull has at
   most 2*N-4 faces (Euler formula).  Extra headroom for churn. */
#define BUILD_MAX_FACES 256u

/* Maximum edges in the horizon loop. */
#define BUILD_MAX_HORIZON 512u

/** Working face during construction. */
typedef struct build_face {
    uint32_t v[3];        /**< Vertex indices (CCW from outside). */
    phys_vec3_t normal;   /**< Outward-facing unit normal. */
    float dist;           /**< Plane distance from origin (dot(normal, v[0])). */
    uint8_t alive;        /**< 1 if face is active, 0 if deleted. */
} build_face_t;

/** Edge of the horizon (boundary between visible and non-visible faces). */
typedef struct horizon_edge {
    uint32_t a, b;        /**< Vertex indices (ordered so visible face is on left). */
} horizon_edge_t;

static phys_vec3_t cross(phys_vec3_t a, phys_vec3_t b) {
    return (phys_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

/**
 * Compute outward normal for triangle (a, b, c) with CCW winding.
 * Returns zero vector if degenerate.
 */
static phys_vec3_t tri_normal(phys_vec3_t a, phys_vec3_t b, phys_vec3_t c) {
    phys_vec3_t ab = vec3_sub(b, a);
    phys_vec3_t ac = vec3_sub(c, a);
    phys_vec3_t n = cross(ab, ac);
    float len = vec3_magnitude(n);
    if (len < 1e-10f) {
        return (phys_vec3_t){0, 0, 0};
    }
    return vec3_scale(n, 1.0f / len);
}

/**
 * Signed distance from point p to plane defined by face f.
 * Positive = outside (same side as normal).
 */
static float point_plane_dist(const build_face_t *f, phys_vec3_t p,
                              const phys_vec3_t *verts) {
    (void)verts;
    return vec3_dot(f->normal, p) - f->dist;
}

/**
 * Find 4 non-coplanar points to form the initial tetrahedron.
 * Returns 0 on success, -1 if all points are coplanar/collinear.
 */
static int find_initial_tet(const phys_vec3_t *pts, uint32_t count,
                            uint32_t out[4]) {
    if (count < 4) return -1;

    /* Find two points with max distance along each axis, then pick
     * the pair with the greatest separation.  O(n) instead of O(n²). */
    uint32_t min_idx[3] = {0, 0, 0};
    uint32_t max_idx[3] = {0, 0, 0};
    for (uint32_t i = 1; i < count; i++) {
        if (pts[i].x < pts[min_idx[0]].x) min_idx[0] = i;
        if (pts[i].x > pts[max_idx[0]].x) max_idx[0] = i;
        if (pts[i].y < pts[min_idx[1]].y) min_idx[1] = i;
        if (pts[i].y > pts[max_idx[1]].y) max_idx[1] = i;
        if (pts[i].z < pts[min_idx[2]].z) min_idx[2] = i;
        if (pts[i].z > pts[max_idx[2]].z) max_idx[2] = i;
    }
    float best = 0;
    out[0] = 0;
    out[1] = 1;
    for (int ax = 0; ax < 3; ax++) {
        float d = vec3_magnitude(vec3_sub(pts[max_idx[ax]], pts[min_idx[ax]]));
        if (d > best) {
            best = d;
            out[0] = min_idx[ax];
            out[1] = max_idx[ax];
        }
    }
    if (best < 1e-8f) return -1;

    /* Find point furthest from line (out[0], out[1]). */
    phys_vec3_t line_dir = vec3_normalize_safe(vec3_sub(pts[out[1]], pts[out[0]]), 1e-10f);
    best = 0;
    out[2] = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (i == out[0] || i == out[1]) continue;
        phys_vec3_t diff = vec3_sub(pts[i], pts[out[0]]);
        float proj = vec3_dot(diff, line_dir);
        phys_vec3_t closest = vec3_add(pts[out[0]], vec3_scale(line_dir, proj));
        float d = vec3_magnitude(vec3_sub(pts[i], closest));
        if (d > best) {
            best = d;
            out[2] = i;
        }
    }
    if (out[2] == UINT32_MAX || best < 1e-8f) return -1;

    /* Find point furthest from plane (out[0], out[1], out[2]). */
    phys_vec3_t plane_n = tri_normal(pts[out[0]], pts[out[1]], pts[out[2]]);
    if (vec3_magnitude(plane_n) < 0.5f) return -1;

    float plane_d = vec3_dot(plane_n, pts[out[0]]);
    best = 0;
    out[3] = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        if (i == out[0] || i == out[1] || i == out[2]) continue;
        float d = fabsf(vec3_dot(plane_n, pts[i]) - plane_d);
        if (d > best) {
            best = d;
            out[3] = i;
        }
    }
    if (out[3] == UINT32_MAX || best < 1e-8f) return -1;

    /* Ensure consistent winding: if out[3] is above the plane, flip
       the triangle winding so normals point outward. */
    float side = vec3_dot(plane_n, pts[out[3]]) - plane_d;
    if (side > 0) {
        /* Point is above → triangle normal points toward it.
           Swap out[1] and out[2] to flip normal away from out[3]. */
        uint32_t tmp = out[1];
        out[1] = out[2];
        out[2] = tmp;
    }

    return 0;
}

/** Initialize a build face with correct normal orientation. */
static void init_face(build_face_t *f, uint32_t a, uint32_t b, uint32_t c,
                      const phys_vec3_t *verts) {
    f->v[0] = a;
    f->v[1] = b;
    f->v[2] = c;
    f->normal = tri_normal(verts[a], verts[b], verts[c]);
    f->dist = vec3_dot(f->normal, verts[a]);
    f->alive = 1;
}

int phys_convex_hull_build(phys_convex_hull_t *hull,
                           const phys_vec3_t *points,
                           uint32_t count) {
    if (!hull || !points || count < 4 || count > BUILD_MAX_INPUT) {
        return -1;
    }

    /* Working arrays — heap-allocated since count may be large. */
    build_face_t faces[BUILD_MAX_FACES];
    uint32_t face_count = 0;
    memset(faces, 0, sizeof(faces));

    /* Step 1: Find initial tetrahedron. */
    uint32_t tet[4];
    if (find_initial_tet(points, count, tet) != 0) {
        return -1;
    }

    /* Use the input points directly as our vertex reference.
     * The incremental algorithm only adds vertices that are already
     * in the input array, so we index into `points[]` throughout. */
    const phys_vec3_t *verts = points;

    /* Step 2: Create 4 faces of the tetrahedron. */
    init_face(&faces[0], tet[0], tet[1], tet[2], verts);
    init_face(&faces[1], tet[0], tet[2], tet[3], verts);
    init_face(&faces[2], tet[0], tet[3], tet[1], verts);
    init_face(&faces[3], tet[1], tet[3], tet[2], verts);
    face_count = 4;

    /* Verify winding: each face's normal must point away from the
       opposite vertex. */
    for (uint32_t fi = 0; fi < 4; fi++) {
        uint32_t opp = UINT32_MAX;
        for (uint32_t ti = 0; ti < 4; ti++) {
            uint32_t tv = tet[ti];
            if (tv != faces[fi].v[0] && tv != faces[fi].v[1] && tv != faces[fi].v[2]) {
                opp = tv;
                break;
            }
        }
        if (opp == UINT32_MAX) continue;

        float d = point_plane_dist(&faces[fi], verts[opp], verts);
        if (d > 0) {
            uint32_t tmp = faces[fi].v[1];
            faces[fi].v[1] = faces[fi].v[2];
            faces[fi].v[2] = tmp;
            faces[fi].normal = vec3_scale(faces[fi].normal, -1.0f);
            faces[fi].dist = -faces[fi].dist;
        }
    }

    /* Step 3: Incrementally add remaining points. */
    /* Heap-allocate the on_hull tracker since count may be >> 64. */
    uint8_t *on_hull = calloc(count, sizeof(uint8_t));
    if (!on_hull) return -1;
    on_hull[tet[0]] = 1;
    on_hull[tet[1]] = 1;
    on_hull[tet[2]] = 1;
    on_hull[tet[3]] = 1;

    for (uint32_t pi = 0; pi < count; pi++) {
        if (on_hull[pi]) continue;

        /* Check if point is outside any face. */
        float max_dist = 0;
        for (uint32_t fi = 0; fi < face_count; fi++) {
            if (!faces[fi].alive) continue;
            float d = point_plane_dist(&faces[fi], verts[pi], verts);
            if (d > max_dist) max_dist = d;
        }

        if (max_dist < 1e-6f) continue;

        on_hull[pi] = 1;

        /* Find all faces visible from this point. */
        uint8_t visible[BUILD_MAX_FACES];
        memset(visible, 0, sizeof(visible));
        for (uint32_t fi = 0; fi < face_count; fi++) {
            if (!faces[fi].alive) continue;
            float d = point_plane_dist(&faces[fi], verts[pi], verts);
            if (d > 1e-7f) {
                visible[fi] = 1;
            }
        }

        /* Find horizon edges. */
        horizon_edge_t horizon[BUILD_MAX_HORIZON];
        uint32_t horizon_count = 0;

        for (uint32_t fi = 0; fi < face_count; fi++) {
            if (!faces[fi].alive || !visible[fi]) continue;
            for (int ei = 0; ei < 3; ei++) {
                uint32_t ea = faces[fi].v[ei];
                uint32_t eb = faces[fi].v[(ei + 1) % 3];

                int has_adjacent_visible = 0;
                for (uint32_t fj = 0; fj < face_count; fj++) {
                    if (fj == fi || !faces[fj].alive) continue;
                    for (int ej = 0; ej < 3; ej++) {
                        if (faces[fj].v[ej] == eb && faces[fj].v[(ej + 1) % 3] == ea) {
                            if (visible[fj]) {
                                has_adjacent_visible = 1;
                            }
                            break;
                        }
                    }
                    if (has_adjacent_visible) break;
                }

                if (!has_adjacent_visible && horizon_count < BUILD_MAX_HORIZON) {
                    horizon[horizon_count].a = ea;
                    horizon[horizon_count].b = eb;
                    horizon_count++;
                }
            }
        }

        /* Remove visible faces. */
        for (uint32_t fi = 0; fi < face_count; fi++) {
            if (visible[fi]) {
                faces[fi].alive = 0;
            }
        }

        /* Add new faces from each horizon edge to the new point. */
        for (uint32_t hi = 0; hi < horizon_count; hi++) {
            if (face_count >= BUILD_MAX_FACES) break;
            init_face(&faces[face_count], horizon[hi].a, horizon[hi].b, pi, verts);

            phys_vec3_t c = {0, 0, 0};
            for (uint32_t ti = 0; ti < 4; ti++) {
                c = vec3_add(c, verts[tet[ti]]);
            }
            c = vec3_scale(c, 0.25f);

            float d = point_plane_dist(&faces[face_count], c, verts);
            if (d > 0) {
                uint32_t tmp = faces[face_count].v[1];
                faces[face_count].v[1] = faces[face_count].v[2];
                faces[face_count].v[2] = tmp;
                faces[face_count].normal = vec3_scale(faces[face_count].normal, -1.0f);
                faces[face_count].dist = -faces[face_count].dist;
            }

            face_count++;
        }
    }

    /* Step 4: Extract final hull.  Collect unique vertices used by alive faces. */
    memset(hull, 0, sizeof(*hull));

    /* Collect unique vertex indices — need a set tracking up to count entries. */
    uint8_t *used = calloc(count, sizeof(uint8_t));
    if (!used) { free(on_hull); return -1; }
    for (uint32_t fi = 0; fi < face_count; fi++) {
        if (!faces[fi].alive) continue;
        used[faces[fi].v[0]] = 1;
        used[faces[fi].v[1]] = 1;
        used[faces[fi].v[2]] = 1;
    }

    /* Build vertex remap table (heap-allocated for large count). */
    uint32_t *remap = malloc(count * sizeof(uint32_t));
    if (!remap) { free(used); free(on_hull); return -1; }
    memset(remap, 0xFF, count * sizeof(uint32_t));
    uint32_t vc = 0;
    for (uint32_t i = 0; i < count && vc < PHYS_CONVEX_MAX_VERTS; i++) {
        if (used[i]) {
            hull->vertices[vc] = verts[i];
            remap[i] = vc;
            vc++;
        }
    }
    hull->vertex_count = vc;

    free(used);
    free(on_hull);

    /* Build faces and indices. */
    uint32_t fc = 0;
    uint32_t ic = 0;
    for (uint32_t fi = 0; fi < face_count; fi++) {
        if (!faces[fi].alive) continue;
        if (fc >= PHYS_CONVEX_MAX_FACES || ic + 3 > PHYS_CONVEX_MAX_INDICES) break;

        hull->faces[fc].index_start = (uint16_t)ic;
        hull->faces[fc].index_count = 3;
        hull->faces[fc].normal = faces[fi].normal;

        hull->indices[ic + 0] = (uint16_t)remap[faces[fi].v[0]];
        hull->indices[ic + 1] = (uint16_t)remap[faces[fi].v[1]];
        hull->indices[ic + 2] = (uint16_t)remap[faces[fi].v[2]];
        ic += 3;
        fc++;
    }
    hull->face_count = fc;
    hull->index_count = ic;

    free(remap);

    /* Merge coplanar triangular faces into convex polygons.
       For a cube, 12 triangles should become 6 quads. */
    for (uint32_t i = 0; i < hull->face_count; i++) {
        for (uint32_t j = i + 1; j < hull->face_count; ) {
            /* Check if faces i and j are coplanar (same normal). */
            float ndot = vec3_dot(hull->faces[i].normal, hull->faces[j].normal);
            if (ndot < 0.9999f) {
                j++;
                continue;
            }

            /* Check if they share an edge (two common vertices). */
            uint16_t shared_a = UINT16_MAX, shared_b = UINT16_MAX;
            uint16_t unique_j = UINT16_MAX;
            uint16_t is = hull->faces[i].index_start;
            uint16_t ic_i = hull->faces[i].index_count;
            uint16_t js = hull->faces[j].index_start;
            uint16_t jc = hull->faces[j].index_count;

            /* Find vertices in j that are NOT in i (the unique vertex to add). */
            for (uint16_t jk = 0; jk < jc; jk++) {
                uint16_t jv = hull->indices[js + jk];
                int found = 0;
                for (uint16_t ik = 0; ik < ic_i; ik++) {
                    if (hull->indices[is + ik] == jv) {
                        if (shared_a == UINT16_MAX) shared_a = jv;
                        else shared_b = jv;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    unique_j = jv;
                }
            }

            if (shared_a == UINT16_MAX || shared_b == UINT16_MAX || unique_j == UINT16_MAX) {
                j++;
                continue;
            }

            /* Merge: insert unique_j into face i's index list between shared_a and shared_b.
               Find where shared_b appears after shared_a in face i's winding. */
            int insert_pos = -1;
            for (uint16_t ik = 0; ik < ic_i; ik++) {
                if (hull->indices[is + ik] == shared_a) {
                    uint16_t next = hull->indices[is + ((ik + 1) % ic_i)];
                    if (next == shared_b) {
                        insert_pos = (int)(is + ik + 1);
                        break;
                    }
                }
                if (hull->indices[is + ik] == shared_b) {
                    uint16_t next = hull->indices[is + ((ik + 1) % ic_i)];
                    if (next == shared_a) {
                        insert_pos = (int)(is + ik + 1);
                        break;
                    }
                }
            }

            if (insert_pos < 0 || hull->index_count + 1 > PHYS_CONVEX_MAX_INDICES) {
                j++;
                continue;
            }

            /* Shift indices after insert_pos right by 1. */
            for (uint32_t k = hull->index_count; k > (uint32_t)insert_pos; k--) {
                hull->indices[k] = hull->indices[k - 1];
            }
            hull->indices[insert_pos] = unique_j;
            hull->index_count++;
            hull->faces[i].index_count++;

            /* Update all face index_starts after the insertion point. */
            for (uint32_t k = 0; k < hull->face_count; k++) {
                if (k != i && hull->faces[k].index_start >= (uint16_t)insert_pos) {
                    hull->faces[k].index_start++;
                }
            }

            /* Remove face j by swapping with last. */
            hull->face_count--;
            if (j < hull->face_count) {
                hull->faces[j] = hull->faces[hull->face_count];
            }
            /* Don't increment j — re-check the swapped face. */
        }
    }

    phys_convex_hull_recompute_bounds(hull);
    return 0;
}
