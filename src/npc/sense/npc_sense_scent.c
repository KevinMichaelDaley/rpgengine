/**
 * @file npc_sense_scent.c
 * @brief Scent field implementation: grid alloc, emit, advection,
 *        trilinear sample.
 *
 * Non-static functions (5 max for minimum viable):
 *   1. npc_scent_field_init
 *   2. npc_scent_field_destroy
 *   3. npc_scent_emit
 *   4. npc_scent_advect
 *   5. npc_scent_sample
 */

#include "ferrum/npc/npc_sense_scent.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ADVECT_DAMP 0.95f

/* ── Internal helpers ────────────────────────────────────────────── */

/**
 * @brief Flat-array offset of cell (x,y,z), scaled by
 *        NPC_SCENT_MAX_TYPES.
 */
static inline int cell_off_(uint32_t res, int x, int y, int z)
{
    int r = (int)res;
    return (z * r * r + y * r + x) * (int)NPC_SCENT_MAX_TYPES;
}

/**
 * @brief Linear interpolation.
 */
static inline float lerp_(float a, float b, float t)
{
    return a + (b - a) * t;
}

/**
 * @brief Trilinear sample: read all NPC_SCENT_MAX_TYPES from the
 *        fractional grid coordinate (gx, gy, gz) into @p out.
 *
 * Values outside 0..(res-1) produce all zeroes.
 */
static void sample_8_(const float *grid, uint32_t res,
                      float gx, float gy, float gz,
                      float out[NPC_SCENT_MAX_TYPES])
{
    int i;
    for (i = 0; i < NPC_SCENT_MAX_TYPES; i++) out[i] = 0.0f;

    int r = (int)res;
    if (gx < 0.0f || gy < 0.0f || gz < 0.0f) return;
    if (gx >= (float)(r - 1) || gy >= (float)(r - 1) || gz >= (float)(r - 1))
        return;

    int x0 = (int)gx;
    int y0 = (int)gy;
    int z0 = (int)gz;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    float dx = gx - (float)x0;
    float dy = gy - (float)y0;
    float dz = gz - (float)z0;

    int o000 = cell_off_(res, x0, y0, z0);
    int o100 = cell_off_(res, x1, y0, z0);
    int o010 = cell_off_(res, x0, y1, z0);
    int o110 = cell_off_(res, x1, y1, z0);
    int o001 = cell_off_(res, x0, y0, z1);
    int o101 = cell_off_(res, x1, y0, z1);
    int o011 = cell_off_(res, x0, y1, z1);
    int o111 = cell_off_(res, x1, y1, z1);

    for (i = 0; i < NPC_SCENT_MAX_TYPES; i++) {
        float c00  = lerp_(grid[o000 + i], grid[o100 + i], dx);
        float c01  = lerp_(grid[o001 + i], grid[o101 + i], dx);
        float c10  = lerp_(grid[o010 + i], grid[o110 + i], dx);
        float c11  = lerp_(grid[o011 + i], grid[o111 + i], dx);
        float c0   = lerp_(c00, c10, dy);
        float c1   = lerp_(c01, c11, dy);
        out[i]     = lerp_(c0, c1, dz);
    }
}

/* ── Lifecycle ──────────────────────────────────────────────────── */

bool npc_scent_field_init(npc_scent_field_t *f, uint32_t res,
                          float cell_size)
{
    if (!f || res == 0 || cell_size <= 0.0f) return false;
    memset(f, 0, sizeof(*f));
    size_t sz = (size_t)res * res * res * (size_t)NPC_SCENT_MAX_TYPES;
    f->grid = (float *)calloc(sz, sizeof(float));
    if (!f->grid) return false;
    f->res       = res;
    f->cell_size = cell_size;
    return true;
}

void npc_scent_field_destroy(npc_scent_field_t *f)
{
    if (!f) return;
    free(f->grid);
    memset(f, 0, sizeof(*f));
}

/* ── Emission ────────────────────────────────────────────────────── */

void npc_scent_emit(npc_scent_field_t *f, const npc_scent_emitter_t *em)
{
    if (!f || !f->grid || !em) return;
    if (em->type >= NPC_SCENT_TYPE_COUNT) return;

    int ix = (int)(em->pos[0] / f->cell_size);
    int iy = (int)(em->pos[1] / f->cell_size);
    int iz = (int)(em->pos[2] / f->cell_size);

    int r = (int)f->res;
    if (ix < 0) ix = 0;
    if (ix >= r) ix = r - 1;
    if (iy < 0) iy = 0;
    if (iy >= r) iy = r - 1;
    if (iz < 0) iz = 0;
    if (iz >= r) iz = r - 1;

    int idx = cell_off_(f->res, ix, iy, iz) + (int)em->type;
    f->grid[idx] += em->intensity;
}

/* ── Advection ───────────────────────────────────────────────────── */

void npc_scent_advect(npc_scent_field_t *f, float dt)
{
    if (!f || !f->grid || dt <= 0.0f) return;

    uint32_t res = f->res;
    size_t   total = (size_t)res * res * res * (size_t)NPC_SCENT_MAX_TYPES;
    float   *old  = (float *)malloc(total * sizeof(float));
    if (!old) return;
    memcpy(old, f->grid, total * sizeof(float));

    /* Convert world wind to cell-offset per tick. */
    float wx = f->wind[0] * dt / f->cell_size;
    float wy = f->wind[1] * dt / f->cell_size;
    float wz = f->wind[2] * dt / f->cell_size;

    uint32_t x, y, z;
    for (z = 0; z < res; z++) {
        for (y = 0; y < res; y++) {
            for (x = 0; x < res; x++) {
                /* Backtrack: where did scent come from before wind pushed? */
                float sx = (float)(int)x - wx;
                float sy = (float)(int)y - wy;
                float sz = (float)(int)z - wz;

                int off = cell_off_(res, (int)x, (int)y, (int)z);

                if (sx >= 0.0f && sy >= 0.0f && sz >= 0.0f &&
                    sx < (float)((int)res - 1) &&
                    sy < (float)((int)res - 1) &&
                    sz < (float)((int)res - 1)) {
                    float vals[NPC_SCENT_MAX_TYPES];
                    sample_8_(old, res, sx, sy, sz, vals);
                    int t;
                    for (t = 0; t < NPC_SCENT_MAX_TYPES; t++)
                        f->grid[off + t] = vals[t] * ADVECT_DAMP;
                } else {
                    int t;
                    for (t = 0; t < NPC_SCENT_MAX_TYPES; t++)
                        f->grid[off + t] = 0.0f;
                }
            }
        }
    }

    free(old);
}

/* ── Sampling ────────────────────────────────────────────────────── */

bool npc_scent_sample(const npc_scent_field_t *f,
                      float wx, float wy, float wz,
                      npc_scent_sample_t *out)
{
    if (!f || !f->grid || !out) return false;

    float gx = wx / f->cell_size;
    float gy = wy / f->cell_size;
    float gz = wz / f->cell_size;

    float vals[NPC_SCENT_MAX_TYPES];
    sample_8_(f->grid, f->res, gx, gy, gz, vals);

    float            best  = 0.0f;
    npc_scent_type_t bestt = NPC_SCENT_BLOOD;
    int t;
    for (t = 0; t < NPC_SCENT_MAX_TYPES; t++) {
        if (vals[t] > best) {
            best  = vals[t];
            bestt = (npc_scent_type_t)t;
        }
    }

    out->type      = bestt;
    out->intensity = best;
    return best > 0.0f;
}
