/**
 * @file mesh_extrude.c
 * @brief Grouped face extrusion — offset + side walls.
 *
 * Non-static functions (4 max): mesh_extrude.
 * Static helpers: compute_face_normal_, collect_boundary_edges_,
 *                 remap_duplicate_vertex_.
 */
#include "ferrum/editor/mesh/mesh_extrude.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Types for edge tracking                                             */
/* ------------------------------------------------------------------ */

/** Oriented half-edge with face ownership. */
typedef struct {
    uint32_t v0, v1;    /* oriented edge vertices */
    uint32_t face_idx;  /* triangle index owning this half-edge */
} half_edge_t;

/** Boundary edge ready for wall creation. */
typedef struct {
    uint32_t v0, v1;  /* original oriented boundary edge */
} boundary_edge_t;

/* ------------------------------------------------------------------ */
/* Static helpers                                                      */
/* ------------------------------------------------------------------ */

/** Compute face normal for triangle at face_idx. */
static void compute_face_normal_(const mesh_slot_t *slot, uint32_t face_idx,
                                 float out[3]) {
    const uint32_t *tri = &slot->indices[face_idx * 3];
    const float *a = &slot->positions[tri[0] * 3];
    const float *b = &slot->positions[tri[1] * 3];
    const float *c = &slot->positions[tri[2] * 3];

    float ab[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float ac[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };

    out[0] = ab[1]*ac[2] - ab[2]*ac[1];
    out[1] = ab[2]*ac[0] - ab[0]*ac[2];
    out[2] = ab[0]*ac[1] - ab[1]*ac[0];

    float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-12f) {
        out[0] /= len; out[1] /= len; out[2] /= len;
    }
}

/**
 * Half-edge comparison for qsort: sort by canonical edge (min,max).
 * This groups half-edges by edge, letting us find boundary edges.
 */
static int halfedge_cmp_(const void *a, const void *b) {
    const half_edge_t *ea = (const half_edge_t *)a;
    const half_edge_t *eb = (const half_edge_t *)b;
    uint32_t a0 = ea->v0 < ea->v1 ? ea->v0 : ea->v1;
    uint32_t a1 = ea->v0 < ea->v1 ? ea->v1 : ea->v0;
    uint32_t b0 = eb->v0 < eb->v1 ? eb->v0 : eb->v1;
    uint32_t b1 = eb->v0 < eb->v1 ? eb->v1 : eb->v0;
    if (a0 != b0) return (a0 < b0) ? -1 : 1;
    if (a1 != b1) return (a1 < b1) ? -1 : 1;
    return 0;
}

/**
 * Find boundary edges of selected faces.
 * A boundary edge is one that appears in only one selected face,
 * or appears at the mesh boundary.
 * Returns allocated array + count. Caller must free.
 */
static boundary_edge_t *collect_boundary_edges_(
    const mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
    const uint32_t *sel_faces, uint32_t sel_count,
    uint32_t *out_count)
{
    /* Build half-edge list from selected faces */
    uint32_t he_count = sel_count * 3;
    half_edge_t *halves = malloc(he_count * sizeof(half_edge_t));
    if (!halves) { *out_count = 0; return NULL; }

    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t fi = sel_faces[i];
        const uint32_t *tri = &slot->indices[fi * 3];
        halves[i*3+0] = (half_edge_t){ tri[0], tri[1], fi };
        halves[i*3+1] = (half_edge_t){ tri[1], tri[2], fi };
        halves[i*3+2] = (half_edge_t){ tri[2], tri[0], fi };
    }

    /* Sort by canonical edge */
    qsort(halves, he_count, sizeof(half_edge_t), halfedge_cmp_);

    /* Boundary = half-edge whose canonical twin is NOT in the list
     * (or twin belongs to unselected face). We check pairs. */
    boundary_edge_t *bounds = malloc(he_count * sizeof(boundary_edge_t));
    uint32_t bc = 0;

    (void)sel; /* selection already encoded in sel_faces */

    uint32_t idx = 0;
    while (idx < he_count) {
        uint32_t a0 = halves[idx].v0 < halves[idx].v1 ? halves[idx].v0 : halves[idx].v1;
        uint32_t a1 = halves[idx].v0 < halves[idx].v1 ? halves[idx].v1 : halves[idx].v0;

        /* Count how many half-edges share this canonical edge */
        uint32_t end = idx + 1;
        while (end < he_count) {
            uint32_t e0 = halves[end].v0 < halves[end].v1 ? halves[end].v0 : halves[end].v1;
            uint32_t e1 = halves[end].v0 < halves[end].v1 ? halves[end].v1 : halves[end].v0;
            if (e0 != a0 || e1 != a1) break;
            end++;
        }

        uint32_t group_size = end - idx;
        if (group_size == 1) {
            /* Only one selected face uses this edge → it's a boundary */
            bounds[bc++] = (boundary_edge_t){ halves[idx].v0, halves[idx].v1 };
        }
        /* If 2 selected faces share it, it's interior → skip */

        idx = end;
    }

    free(halves);
    *out_count = bc;
    return bounds;
}

/**
 * Find or create the duplicate vertex for original vertex v.
 * Uses a vertex remap table (indexed by original vertex ID).
 * UINT32_MAX = not yet duplicated.
 */
static uint32_t get_or_create_dup_(mesh_slot_t *slot, uint32_t v,
                                    uint32_t *remap, uint32_t orig_vcount,
                                    const float *offset) {
    if (v >= orig_vcount) return v; /* already new */
    if (remap[v] != UINT32_MAX) return remap[v];

    /* Duplicate vertex with offset */
    float pos[3] = {
        slot->positions[v*3+0] + offset[0],
        slot->positions[v*3+1] + offset[1],
        slot->positions[v*3+2] + offset[2]
    };
    float nrm[3] = {
        slot->normals[v*3+0],
        slot->normals[v*3+1],
        slot->normals[v*3+2]
    };
    uint32_t nv = mesh_slot_add_vertex(slot, pos, nrm);
    if (nv == UINT32_MAX) return UINT32_MAX;

    /* Copy UV and color attributes */
    for (int ch = 0; ch < MESH_SLOT_UV_CHANNELS; ch++) {
        if (slot->uvs[ch]) {
            slot->uvs[ch][nv*2+0] = slot->uvs[ch][v*2+0];
            slot->uvs[ch][nv*2+1] = slot->uvs[ch][v*2+1];
        }
    }
    if (slot->colors) {
        memcpy(&slot->colors[nv*4], &slot->colors[v*4], 4*sizeof(float));
    }
    if (slot->tangents) {
        memcpy(&slot->tangents[nv*4], &slot->tangents[v*4], 4*sizeof(float));
    }

    remap[v] = nv;
    return nv;
}

/* ------------------------------------------------------------------ */
/* Public: mesh_extrude                                                */
/* ------------------------------------------------------------------ */

bool mesh_extrude(mesh_slot_t *slot, mesh_sel_bitset_t *sel,
                  float distance, const float *direction) {
    if (!slot || !sel) return false;

    uint32_t face_count = slot->index_count / 3;

    /* Collect selected face indices */
    uint32_t sel_count = 0;
    uint32_t *sel_faces = malloc(face_count * sizeof(uint32_t));
    if (!sel_faces) return false;

    for (uint32_t f = 0; f < face_count; f++) {
        if (mesh_sel_bitset_test(sel, f)) {
            sel_faces[sel_count++] = f;
        }
    }
    if (sel_count == 0) { free(sel_faces); return false; }

    /* Compute extrusion direction */
    float dir[3];
    if (direction) {
        dir[0] = direction[0]; dir[1] = direction[1]; dir[2] = direction[2];
    } else {
        /* Average normal of selected faces */
        dir[0] = dir[1] = dir[2] = 0;
        for (uint32_t i = 0; i < sel_count; i++) {
            float n[3];
            compute_face_normal_(slot, sel_faces[i], n);
            dir[0] += n[0]; dir[1] += n[1]; dir[2] += n[2];
        }
        float len = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (len > 1e-12f) {
            dir[0] /= len; dir[1] /= len; dir[2] /= len;
        }
    }

    float offset[3] = { dir[0]*distance, dir[1]*distance, dir[2]*distance };

    /* Find boundary edges */
    uint32_t boundary_count = 0;
    boundary_edge_t *boundaries = collect_boundary_edges_(
        slot, sel, sel_faces, sel_count, &boundary_count);
    if (!boundaries && boundary_count > 0) {
        free(sel_faces);
        return false;
    }

    uint32_t orig_vcount = slot->vertex_count;

    /* Vertex remap: original → duplicated offset vertex */
    uint32_t *remap = malloc(orig_vcount * sizeof(uint32_t));
    if (!remap) { free(boundaries); free(sel_faces); return false; }
    memset(remap, 0xFF, orig_vcount * sizeof(uint32_t));

    /* Pre-reserve space for new geometry */
    uint32_t max_new_verts = 0;
    for (uint32_t i = 0; i < sel_count; i++) {
        (void)i;
        max_new_verts += 3; /* worst case: all unique */
    }
    mesh_slot_reserve_vertices(slot, slot->vertex_count + max_new_verts);
    mesh_slot_reserve_indices(slot, slot->index_count + boundary_count * 6);

    /* Remap selected face vertices to their offset duplicates */
    for (uint32_t i = 0; i < sel_count; i++) {
        uint32_t fi = sel_faces[i];
        uint32_t *tri = &slot->indices[fi * 3];
        for (int j = 0; j < 3; j++) {
            uint32_t nv = get_or_create_dup_(slot, tri[j], remap, orig_vcount, offset);
            if (nv == UINT32_MAX) {
                free(remap); free(boundaries); free(sel_faces);
                return false;
            }
            tri[j] = nv;
        }
    }

    /* Create side wall quads for each boundary edge.
     * Boundary edge (v0, v1) in original → quad (v0, v1, dup1, dup0).
     * Split into 2 triangles with outward-facing normals. */
    for (uint32_t i = 0; i < boundary_count; i++) {
        uint32_t v0 = boundaries[i].v0;
        uint32_t v1 = boundaries[i].v1;
        uint32_t d0 = remap[v0];
        uint32_t d1 = remap[v1];

        if (d0 == UINT32_MAX || d1 == UINT32_MAX) continue;

        /* Wall quad: two triangles
         * Winding depends on orientation — use (v0, v1, d1) and (v0, d1, d0) */
        mesh_slot_add_triangle(slot, v0, v1, d1, 0);
        mesh_slot_add_triangle(slot, v0, d1, d0, 0);
    }

    /* Update selection to point at the new (extruded) faces */
    mesh_sel_bitset_clear_all(sel);
    uint32_t new_face_count = slot->index_count / 3;
    for (uint32_t i = 0; i < sel_count; i++) {
        if (sel_faces[i] < new_face_count) {
            mesh_sel_bitset_set(sel, sel_faces[i]);
        }
    }

    free(remap);
    free(boundaries);
    free(sel_faces);

    return true;
}
