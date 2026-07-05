/**
 * @file procgen_chunk_mesh.c
 * @brief Per-chunk mesh generation: emit cube faces not occluded by neighbors.
 *
 * Simple, reliable Minecraft-style meshing.  For each solid voxel, emit
 * the 6 faces that are not occluded by adjacent solid voxels.
 */

#include "ferrum/procgen/procgen_chunk_builder.h"
#include "ferrum/procgen/procgen_svo_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Query a voxel in the chunk SVO ────────────────────────────── */
static int svo_is_solid(const npc_svo_grid_t *g, int x, int y, int z) {
    uint32_t cells = 1u << g->max_depth;
    if (x < 0 || y < 0 || z < 0
        || (uint32_t)x >= cells
        || (uint32_t)y >= cells
        || (uint32_t)z >= cells) {
        return 0;
    }
    uint32_t node_index = 0;
    uint32_t remaining  = cells;
    for (uint32_t depth = 0; depth < g->max_depth; depth++) {
        remaining >>= 1;
        uint32_t cx = ((uint32_t)z / remaining) & 1;
        uint32_t cy = ((uint32_t)y / remaining) & 1;
        uint32_t cz2 = ((uint32_t)x / remaining) & 1;
        uint32_t child_idx = (cx << 2) | (cy << 1) | cz2;
        uint32_t child = g->nodes[node_index].children[child_idx];
        if (child == NPC_SVO_INVALID_NODE) return 0;
        node_index = child;
        if (g->nodes[node_index].flags & NPC_SVO_FLAG_SOLID) return 1;
    }
    return 0;
}

static void ensure_cap(procgen_mesh_t *m, uint32_t needed) {
    if (needed > m->vertex_cap) {
        uint32_t nc = m->vertex_cap ? m->vertex_cap * 2 : 65536;
        if (nc < needed) nc = needed;
        float *nd = realloc(m->vertices, nc * sizeof(float));
        if (!nd) return;
        m->vertices   = nd;
        m->vertex_cap = nc;
    }
}

/**
 * @brief Emit two triangles for one face of a cube at (x0,y0,z0)→(x1,y1,z1).
 *
 * @param axis  0=X, 1=Y, 2=Z
 * @param sign  +1 for positive face, -1 for negative
 */
static void emit_face(procgen_mesh_t *m,
                      float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      int axis, int sign) {
    ensure_cap(m, m->vertex_count + 18);
    float *v = m->vertices + m->vertex_count;

    if (axis == 0) {
        float xx = (sign > 0) ? x1 : x0;
        if (sign > 0) {
            /* +X face: normal +X.  From +X: Y right, Z up.  CCW = (y0,z0)→(y0,z1)→(y1,z1) */
            v[0]=xx;v[1]=y0;v[2]=z0; v[3]=xx;v[4]=y0;v[5]=z1; v[6]=xx;v[7]=y1;v[8]=z1;
            v[9]=xx;v[10]=y0;v[11]=z0; v[12]=xx;v[13]=y1;v[14]=z1; v[15]=xx;v[16]=y1;v[17]=z0;
        } else {
            /* -X face: normal -X.  From -X: Y right, Z up.  CCW = (y0,z1)→(y0,z0)→(y1,z0) */
            v[0]=xx;v[1]=y0;v[2]=z1; v[3]=xx;v[4]=y0;v[5]=z0; v[6]=xx;v[7]=y1;v[8]=z0;
            v[9]=xx;v[10]=y0;v[11]=z1; v[12]=xx;v[13]=y1;v[14]=z0; v[15]=xx;v[16]=y1;v[17]=z1;
        }
    } else if (axis == 1) {
        float yy = (sign > 0) ? y1 : y0;
        if (sign > 0) {
            /* +Y face (top): normal +Y.  From above: X right, Z up.  CCW = (x0,z0)→(x1,z0)→(x1,z1) */
            v[0]=x0;v[1]=yy;v[2]=z0; v[3]=x1;v[4]=yy;v[5]=z0; v[6]=x1;v[7]=yy;v[8]=z1;
            v[9]=x0;v[10]=yy;v[11]=z0; v[12]=x1;v[13]=yy;v[14]=z1; v[15]=x0;v[16]=yy;v[17]=z1;
        } else {
            /* -Y face (bottom): normal -Y.  From below: X right, Z up.  CCW = (x1,z0)→(x1,z1)→(x0,z1) */
            v[0]=x1;v[1]=yy;v[2]=z0; v[3]=x1;v[4]=yy;v[5]=z1; v[6]=x0;v[7]=yy;v[8]=z1;
            v[9]=x1;v[10]=yy;v[11]=z0; v[12]=x0;v[13]=yy;v[14]=z1; v[15]=x0;v[16]=yy;v[17]=z0;
        }
    } else {
        float zz = (sign > 0) ? z1 : z0;
        if (sign > 0) {
            /* +Z face (front): normal +Z.  From +Z: X right, Y up.  CCW = (x0,y0)→(x1,y0)→(x1,y1) */
            v[0]=x0;v[1]=y0;v[2]=zz; v[3]=x1;v[4]=y0;v[5]=zz; v[6]=x1;v[7]=y1;v[8]=zz;
            v[9]=x0;v[10]=y0;v[11]=zz; v[12]=x1;v[13]=y1;v[14]=zz; v[15]=x0;v[16]=y1;v[17]=zz;
        } else {
            /* -Z face (back): normal -Z.  From -Z: X right, Y up.  CCW = (x1,y0)→(x0,y0)→(x0,y1) */
            v[0]=x1;v[1]=y0;v[2]=zz; v[3]=x0;v[4]=y0;v[5]=zz; v[6]=x0;v[7]=y1;v[8]=zz;
            v[9]=x1;v[10]=y0;v[11]=zz; v[12]=x0;v[13]=y1;v[14]=zz; v[15]=x1;v[16]=y1;v[17]=zz;
        }
    }
    m->vertex_count += 18;
}

uint32_t procgen_mesh_from_svo(const npc_svo_grid_t *g, procgen_mesh_t *mesh) {
    uint32_t cells = 1u << g->max_depth;
    float    vs    = g->voxel_size;
    float    ox    = g->world_bounds.min.x;
    float    oy    = g->world_bounds.min.y;
    float    oz    = g->world_bounds.min.z;
    uint32_t tris  = 0;

    for (uint32_t z = 0; z < cells; z++) {
        for (uint32_t y = 0; y < cells; y++) {
            for (uint32_t x = 0; x < cells; x++) {
                if (!svo_is_solid(g, (int)x, (int)y, (int)z)) continue;

                float x0 = ox + (float)x * vs;
                float y0 = oy + (float)y * vs;
                float z0 = oz + (float)z * vs;
                float x1 = x0 + vs;
                float y1 = y0 + vs;
                float z1 = z0 + vs;

                if (!svo_is_solid(g, (int)x+1, (int)y,   (int)z))   { emit_face(mesh, x0,y0,z0,x1,y1,z1, 0, 1); tris+=2; }
                if (!svo_is_solid(g, (int)x-1, (int)y,   (int)z))   { emit_face(mesh, x0,y0,z0,x1,y1,z1, 0,-1); tris+=2; }
                if (!svo_is_solid(g, (int)x,   (int)y+1, (int)z))   { emit_face(mesh, x0,y0,z0,x1,y1,z1, 1, 1); tris+=2; }
                if (!svo_is_solid(g, (int)x,   (int)y-1, (int)z))   { emit_face(mesh, x0,y0,z0,x1,y1,z1, 1,-1); tris+=2; }
                if (!svo_is_solid(g, (int)x,   (int)y,   (int)z+1)) { emit_face(mesh, x0,y0,z0,x1,y1,z1, 2, 1); tris+=2; }
                if (!svo_is_solid(g, (int)x,   (int)y,   (int)z-1)) { emit_face(mesh, x0,y0,z0,x1,y1,z1, 2,-1); tris+=2; }
            }
        }
    }
    return tris;
}

void procgen_mesh_init(procgen_mesh_t *m) {
    memset(m, 0, sizeof(*m));
}

void procgen_mesh_destroy(procgen_mesh_t *m) {
    free(m->vertices);
    memset(m, 0, sizeof(*m));
}
