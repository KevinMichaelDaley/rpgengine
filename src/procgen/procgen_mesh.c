/**
 * @file procgen_mesh.c
 * @brief Greedy mesh generation from an SVO grid.
 *
 * Scans all six axis-aligned directions and merges adjacent coplanar
 * faces into maximal axis-aligned rectangles.  This produces a compact
 * triangle mesh suitable for GPU rendering.
 */

#include "ferrum/procgen/procgen_svo_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ──────────────────────────────────────────── */

static void ensure_capacity(procgen_mesh_t *mesh, uint32_t needed) {
    if (needed > mesh->vertex_cap) {
        uint32_t new_cap = mesh->vertex_cap ? mesh->vertex_cap * 2 : 65536;
        if (new_cap < needed) new_cap = needed;
        float *new_data = realloc(mesh->vertices, new_cap * sizeof(float));
        if (!new_data) return;
        mesh->vertices  = new_data;
        mesh->vertex_cap = new_cap;
    }
}

/**
 * @brief Emit two triangles forming an axis-aligned quad.
 *
 * @param axis  0 = X-normal (quad in YZ plane),
 *              1 = Y-normal (quad in XZ plane),
 *              2 = Z-normal (quad in XY plane).
 */
static void emit_quad(procgen_mesh_t *mesh,
                      float x_min, float y_min, float z_min,
                      float x_max, float y_max, float z_max,
                      int axis, int sign) {
    ensure_capacity(mesh, mesh->vertex_count + 18);
    float *v = mesh->vertices + mesh->vertex_count;

    switch (axis) {
    case 0: {  /* YZ plane at X — +sign means normal +X */
        float xx = (sign > 0) ? x_max : x_min;
        /* Standing on +X side looking toward origin: CCW → normal +X */
        if (sign > 0) {
            v[0]=xx;v[1]=y_min;v[2]=z_min; v[3]=xx;v[4]=y_min;v[5]=z_max; v[6]=xx;v[7]=y_max;v[8]=z_max;
            v[9]=xx;v[10]=y_min;v[11]=z_min; v[12]=xx;v[13]=y_max;v[14]=z_max; v[15]=xx;v[16]=y_max;v[17]=z_min;
        } else {
            v[0]=xx;v[1]=y_min;v[2]=z_max; v[3]=xx;v[4]=y_min;v[5]=z_min; v[6]=xx;v[7]=y_max;v[8]=z_min;
            v[9]=xx;v[10]=y_min;v[11]=z_max; v[12]=xx;v[13]=y_max;v[14]=z_min; v[15]=xx;v[16]=y_max;v[17]=z_max;
        }
        break;
    }
    case 1: {  /* XZ plane at Y — +sign means normal +Y (up) */
        float yy = (sign > 0) ? y_max : y_min;
        /* Looking down from above: CCW → normal +Y */
        if (sign > 0) {
            v[0]=x_min;v[1]=yy;v[2]=z_min; v[3]=x_max;v[4]=yy;v[5]=z_min; v[6]=x_max;v[7]=yy;v[8]=z_max;
            v[9]=x_min;v[10]=yy;v[11]=z_min; v[12]=x_max;v[13]=yy;v[14]=z_max; v[15]=x_min;v[16]=yy;v[17]=z_max;
        } else {
            v[0]=x_max;v[1]=yy;v[2]=z_min; v[3]=x_min;v[4]=yy;v[5]=z_min; v[6]=x_min;v[7]=yy;v[8]=z_max;
            v[9]=x_max;v[10]=yy;v[11]=z_min; v[12]=x_min;v[13]=yy;v[14]=z_max; v[15]=x_max;v[16]=yy;v[17]=z_max;
        }
        break;
    }
    default: {  /* XY plane at Z — +sign means normal +Z */
        float zz = (sign > 0) ? z_max : z_min;
        /* Looking from +Z toward origin: CCW → normal +Z */
        if (sign > 0) {
            v[0]=x_min;v[1]=y_min;v[2]=zz; v[3]=x_max;v[4]=y_min;v[5]=zz; v[6]=x_max;v[7]=y_max;v[8]=zz;
            v[9]=x_min;v[10]=y_min;v[11]=zz; v[12]=x_max;v[13]=y_max;v[14]=zz; v[15]=x_min;v[16]=y_max;v[17]=zz;
        } else {
            v[0]=x_max;v[1]=y_min;v[2]=zz; v[3]=x_min;v[4]=y_min;v[5]=zz; v[6]=x_min;v[7]=y_max;v[8]=zz;
            v[9]=x_max;v[10]=y_min;v[11]=zz; v[12]=x_min;v[13]=y_max;v[14]=zz; v[15]=x_max;v[16]=y_max;v[17]=zz;
        }
        break;
    }
    }
    mesh->vertex_count += 18;
}

/* ── SVO query (exposed here for mesh generation) ──────────────── */

static int is_solid_at(const npc_svo_grid_t *grid, int x, int y, int z) {
    if (!grid) return 0;
    uint32_t cells = 1u << grid->max_depth;
    if (x < 0 || y < 0 || z < 0
        || (uint32_t)x >= cells
        || (uint32_t)y >= cells
        || (uint32_t)z >= cells) {
        return 0;
    }
    uint32_t node = 0;
    uint32_t c    = cells;
    for (uint32_t d = 0; d < grid->max_depth; d++) {
        c >>= 1;
        uint32_t cx = ((uint32_t)z / c) & 1;
        uint32_t cy = ((uint32_t)y / c) & 1;
        uint32_t cz2 = ((uint32_t)x / c) & 1;
        uint32_t ci = (cx << 2) | (cy << 1) | cz2;
        uint32_t ch = grid->nodes[node].children[ci];
        if (ch == NPC_SVO_INVALID_NODE) return 0;
        node = ch;
        if (grid->nodes[node].flags & NPC_SVO_FLAG_SOLID) return 1;
    }
    return 0;
}

/* ── Greedy scan helpers ───────────────────────────────────────── */

#define MESH_BLOCK_SHIFT 0  /* match SVO rasterizer block size */

uint32_t procgen_mesh_from_svo(const npc_svo_grid_t *grid,
                                procgen_mesh_t       *mesh) {
    uint32_t full_cells = 1u << grid->max_depth;
    uint32_t block      = 1u << MESH_BLOCK_SHIFT;
    uint32_t cells      = full_cells / block;
    float    voxel      = grid->voxel_size * (float)block;
    float    origin_x   = grid->world_bounds.min.x;
    float    origin_y   = grid->world_bounds.min.y;
    float    origin_z   = grid->world_bounds.min.z;
    uint32_t total_tris = 0;

    /* Helper: check if a block is solid at block coordinates */
    #define SOLID(bx,by,bz) is_solid_at(grid, (int)((bx)*block), (int)((by)*block), (int)((bz)*block))

    /* For each of the 6 directions, scan slices and merge coplanar faces. */
    for (int axis = 0; axis < 3; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (uint32_t slice = 0; slice < cells; slice++) {
                int *mask = calloc(cells * cells, sizeof(int));

                for (uint32_t b = 0; b < cells; b++) {
                    for (uint32_t a = 0; a < cells; a++) {
                        int sx=(int)slice,sy=(int)b,sz=(int)a;
                        int nx=sx,ny=sy,nz=sz;
                        if(axis==0){sx=(int)slice;nx=sx+sign;}
                        else if(axis==1){sy=(int)slice;ny=sy+sign;}
                        else{sz=(int)slice;nz=sz+sign;}
                        int solid = SOLID(sx,sy,sz);
                        int air   = (sign>0)?(slice+1>=cells||!SOLID(nx,ny,nz)):(slice==0||!SOLID(nx,ny,nz));
                        mask[b*cells+a] = solid && air;
                    }
                }

                for (uint32_t b=0;b<cells;b++) for(uint32_t a=0;a<cells;a++){
                    if(!mask[b*cells+a])continue;
                    uint32_t a_end=a;while(a_end+1<cells&&mask[b*cells+(a_end+1)])a_end++;
                    uint32_t b_end=b;int ok=1;while(b_end+1<cells&&ok){for(uint32_t ca=a;ca<=a_end;ca++)if(!mask[(b_end+1)*cells+ca]){ok=0;break;}if(ok)b_end++;}
                    /* a runs in the first varying dimension, b in the second.
                       axis→(a-axis, b-axis): 0→(Z,Y), 1→(X,Z), 2→(X,Y) */
                    float a0 = origin_x + (float)a      * voxel;
                    float a1 = origin_x + (float)(a_end + 1) * voxel;
                    float b0 = origin_x + (float)b      * voxel;
                    float b1 = origin_x + (float)(b_end + 1) * voxel;
                    if (axis == 0) { a0 = origin_z + (float)a*voxel; a1 = origin_z + (float)(a_end+1)*voxel;
                                     b0 = origin_y + (float)b*voxel; b1 = origin_y + (float)(b_end+1)*voxel; }
                    else if (axis == 1) { /* a=X already */ 
                                     b0 = origin_z + (float)b*voxel; b1 = origin_z + (float)(b_end+1)*voxel; }
                    /* else axis==2: a=X, b=Y, both origin_x already correct */
                    float fixed;
                    if (axis == 0)      fixed = origin_x + (float)(slice + (sign > 0 ? 1 : 0)) * voxel;
                    else if (axis == 1) fixed = origin_y + (float)(slice + (sign > 0 ? 1 : 0)) * voxel;
                    else                fixed = origin_z + (float)(slice + (sign > 0 ? 1 : 0)) * voxel;

                    if (axis == 0)      emit_quad(mesh, fixed, b0, a0, fixed, b1, a1, axis, sign);
                    else if (axis == 1) emit_quad(mesh, a0, fixed, b0, a1, fixed, b1, axis, sign);
                    else                emit_quad(mesh, a0, b0, fixed, a1, b1, fixed, axis, sign);
                    total_tris+=2;
                    for(uint32_t cb=b;cb<=b_end;cb++)for(uint32_t ca=a;ca<=a_end;ca++)mask[cb*cells+ca]=0;
                }
                free(mask);
            }
        }
    }
    #undef SOLID
    return total_tris;
}

void procgen_mesh_init(procgen_mesh_t *mesh) {
    memset(mesh, 0, sizeof(*mesh));
}

void procgen_mesh_destroy(procgen_mesh_t *mesh) {
    free(mesh->vertices);
    memset(mesh, 0, sizeof(*mesh));
}
