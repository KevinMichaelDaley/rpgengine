/**
 * @file gen_zone.c
 * @brief Procedural "massive open zone" generator for the chunked lightmap bake
 *        (rpg-fzht). Instead of procedural boxes it INSTANCES the real hall
 *        column + vault meshes (datasets/hall_lm/hall_demo_col_0.dmesh,
 *        hall_demo_vault_0_0.dmesh) across a 3 m bay grid: a free-standing column
 *        at every grid node and a barrel/groin vault over every bay, forming an
 *        open hypostyle hall (no walls -> sky light floods in). A flat floor is
 *        emitted as its own tiles. Every instance is written as its OWN .dmesh
 *        (transformed copy, same uv1 lightmap unwrap) so each gets its own atlas
 *        rect / per-instance lightmap. See dmesh_loader.h for the format:
 *        uint32 corner_count, then 10 LE floats/corner (pos3,nrm3,uv0_2,uv1_2),
 *        Y-up.
 *
 * Standalone (libc + libm):
 *   cc -O2 -o build/gen_zone tools/gen_zone.c -lm
 *   build/gen_zone datasets/zone [bays] [hall_dir]
 * Prints the world AABB for the baker's svo_bounds.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAY 3.0f                 /* hall bay pitch (m), from the col/vault module */
#define COL_CX 3.0f              /* col_0 footprint centre x */
#define COL_CZ (-3.0f)           /* col_0 footprint centre z */

/* Raw dmesh: corner_count + corner_count*10 floats. */
typedef struct { uint32_t n; float *d; } DMesh;

static int dmesh_load_raw(const char *path, DMesh *m) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "gen_zone: cannot open %s\n", path); return -1; }
    if (fread(&m->n, 4, 1, f) != 1 || m->n == 0 || m->n % 3) { fclose(f); return -1; }
    m->d = malloc((size_t)m->n * 10 * sizeof(float));
    if (!m->d) { fclose(f); return -1; }
    if (fread(m->d, sizeof(float), (size_t)m->n * 10, f) != (size_t)m->n * 10) { fclose(f); free(m->d); return -1; }
    fclose(f); return 0;
}

/* Write a translated copy of @p src (positions += off, normals/uvs unchanged). */
static int write_instance(const char *path, const DMesh *src, const float off[3],
                          float wmin[3], float wmax[3]) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(&src->n, 4, 1, f);
    for (uint32_t c = 0; c < src->n; ++c) {
        float rec[10];
        memcpy(rec, &src->d[c*10], 10 * sizeof(float));
        rec[0] += off[0]; rec[1] += off[1]; rec[2] += off[2];
        for (int k = 0; k < 3; ++k) {
            if (rec[k] < wmin[k]) wmin[k] = rec[k];
            if (rec[k] > wmax[k]) wmax[k] = rec[k];
        }
        fwrite(rec, sizeof(float), 10, f);
    }
    fclose(f); return 0;
}

/* Emit one floor quad tile spanning [x0,x1]x[z0,z1] at y=0, with a unit [0,1]
 * lightmap unwrap and a world-scaled material uv. */
static int write_floor(const char *path, float x0, float x1, float z0, float z1,
                       float wmin[3], float wmax[3]) {
    /* corners CCW seen from above (normal +Y) */
    float P[4][3] = { {x0,0,z0}, {x1,0,z0}, {x1,0,z1}, {x0,0,z1} };
    float luv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
    const float ms = 2.0f; /* metres per material-texture tile */
    const int tri[6] = { 0,1,2, 0,2,3 };
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t n = 6; fwrite(&n, 4, 1, f);
    for (int k = 0; k < 6; ++k) {
        int i = tri[k];
        float rec[10] = { P[i][0],P[i][1],P[i][2], 0,1,0,
                          P[i][0]/ms, P[i][2]/ms, luv[i][0], luv[i][1] };
        fwrite(rec, sizeof(float), 10, f);
        for (int c = 0; c < 3; ++c) {
            if (P[i][c] < wmin[c]) wmin[c] = P[i][c];
            if (P[i][c] > wmax[c]) wmax[c] = P[i][c];
        }
    }
    fclose(f); return 0;
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "datasets/zone";
    int   bays = argc > 2 ? atoi(argv[2]) : 24;    /* bays per axis */
    const char *hall = argc > 3 ? argv[3] : "datasets/hall_lm";
    if (bays < 1) bays = 1;

    char cp[512], vp[512];
    snprintf(cp, sizeof cp, "%s/hall_demo_col_0.dmesh", hall);
    snprintf(vp, sizeof vp, "%s/hall_demo_vault_0_0.dmesh", hall);
    DMesh col, vault;
    if (dmesh_load_raw(cp, &col) || dmesh_load_raw(vp, &vault)) {
        fprintf(stderr, "gen_zone: failed to load col/vault templates from %s\n", hall);
        return 1;
    }

    float wmin[3] = { 1e30f,1e30f,1e30f }, wmax[3] = { -1e30f,-1e30f,-1e30f };
    int ncol = 0, nvault = 0, nfloor = 0;

    /* Columns at every grid node (gi,gj) in [0,bays]: translate col_0's centre
     * (COL_CX,0,COL_CZ) to world (BAY*gi, 0, -BAY*gj). */
    for (int gj = 0; gj <= bays; ++gj)
        for (int gi = 0; gi <= bays; ++gi) {
            float off[3] = { BAY*(float)gi - COL_CX, 0.0f, -BAY*(float)gj - COL_CZ };
            char p[512]; snprintf(p, sizeof p, "%s/col_%03d_%03d.dmesh", outdir, gi, gj);
            if (write_instance(p, &col, off, wmin, wmax) == 0) ++ncol;
        }
    /* Vaults over every bay (bi,bj) in [0,bays): vault_0_0 spans x[0,3] z[-3,0]
     * (bay 0,0), so translate by (BAY*bi, 0, -BAY*bj). */
    for (int bj = 0; bj < bays; ++bj)
        for (int bi = 0; bi < bays; ++bi) {
            float off[3] = { BAY*(float)bi, 0.0f, -BAY*(float)bj };
            char p[512]; snprintf(p, sizeof p, "%s/vault_%03d_%03d.dmesh", outdir, bi, bj);
            if (write_instance(p, &vault, off, wmin, wmax) == 0) ++nvault;
        }
    /* Floor: one tile per 2x2 bays (keeps each tile's lightmap rect reasonable). */
    int step = 2;
    for (int bj = 0; bj < bays; bj += step)
        for (int bi = 0; bi < bays; bi += step) {
            float x0 = BAY*(float)bi - (bi==0?1.5f:0.0f);
            float x1 = BAY*(float)(bi+step < bays ? bi+step : bays) + (bi+step>=bays?1.5f:0.0f);
            float z1 = -BAY*(float)bj + (bj==0?1.5f:0.0f);
            float z0 = -BAY*(float)(bj+step < bays ? bj+step : bays) - (bj+step>=bays?1.5f:0.0f);
            char p[512]; snprintf(p, sizeof p, "%s/floor_%03d_%03d.dmesh", outdir, bi, bj);
            if (write_floor(p, x0, x1, z0, z1, wmin, wmax) == 0) ++nfloor;
        }

    free(col.d); free(vault.d);
    fprintf(stderr, "gen_zone: %d cols + %d vaults + %d floor = %d meshes, %.0fx%.0f m\n",
            ncol, nvault, nfloor, ncol+nvault+nfloor, wmax[0]-wmin[0], wmax[2]-wmin[2]);
    printf("BOUNDS %.3f %.3f %.3f  %.3f %.3f %.3f\n",
           wmin[0]-1.0f, wmin[1]-1.0f, wmin[2]-1.0f, wmax[0]+1.0f, wmax[1]+1.0f, wmax[2]+1.0f);
    return 0;
}
