/**
 * @file sdf_field.c
 * @brief Sampled SDF grid: sampling + resample/downsample (see sdf_field.h).
 */
#include "ferrum/renderer/chunk/sdf_field.h"

#include <math.h>
#include <stddef.h>

uint32_t sdf_field_cells(const sdf_field_t *f)
{
    if (f == NULL)
        return 0u;
    return (uint32_t)f->dims[0] * (uint32_t)f->dims[1] * (uint32_t)f->dims[2];
}

/* One cell (clamped to the grid). */
static float sdf_at(const sdf_field_t *f, int x, int y, int z)
{
    if (x < 0) x = 0; else if (x >= f->dims[0]) x = f->dims[0] - 1;
    if (y < 0) y = 0; else if (y >= f->dims[1]) y = f->dims[1] - 1;
    if (z < 0) z = 0; else if (z >= f->dims[2]) z = f->dims[2] - 1;
    return f->data[(size_t)(z*f->dims[1]+y)*f->dims[0]+x];
}

float sdf_field_sample(const sdf_field_t *f, float x, float y, float z)
{
    const float p[3] = { x, y, z };
    /* Outside the field's world box -> sentinel (cells span [min, min+dims*vox]). */
    for (int a = 0; a < 3; ++a) {
        float hi = f->min[a] + (float)f->dims[a] * f->voxel;
        if (p[a] < f->min[a] || p[a] > hi)
            return SDF_FIELD_OUTSIDE;
    }
    /* Continuous grid coord: cell i's centre is at (i+0.5)*voxel from min. */
    float g[3]; int i0[3]; float fr[3];
    for (int a = 0; a < 3; ++a) {
        g[a] = (p[a] - f->min[a]) / f->voxel - 0.5f;
        if (g[a] < 0.0f) g[a] = 0.0f;
        float top = (float)(f->dims[a] - 1);
        if (g[a] > top) g[a] = top;
        i0[a] = (int)floorf(g[a]);
        fr[a] = g[a] - (float)i0[a];
    }
    /* Trilinear over the 8 surrounding cells. */
    float c00 = sdf_at(f,i0[0],  i0[1],  i0[2])  *(1-fr[0]) + sdf_at(f,i0[0]+1,i0[1],  i0[2])  *fr[0];
    float c10 = sdf_at(f,i0[0],  i0[1]+1,i0[2])  *(1-fr[0]) + sdf_at(f,i0[0]+1,i0[1]+1,i0[2])  *fr[0];
    float c01 = sdf_at(f,i0[0],  i0[1],  i0[2]+1)*(1-fr[0]) + sdf_at(f,i0[0]+1,i0[1],  i0[2]+1)*fr[0];
    float c11 = sdf_at(f,i0[0],  i0[1]+1,i0[2]+1)*(1-fr[0]) + sdf_at(f,i0[0]+1,i0[1]+1,i0[2]+1)*fr[0];
    float c0 = c00*(1-fr[1]) + c10*fr[1];
    float c1 = c01*(1-fr[1]) + c11*fr[1];
    return c0*(1-fr[2]) + c1*fr[2];
}

void sdf_field_resample(const sdf_field_t *src, sdf_field_t *dst)
{
    for (int z = 0; z < dst->dims[2]; ++z)
    for (int y = 0; y < dst->dims[1]; ++y)
    for (int x = 0; x < dst->dims[0]; ++x) {
        float wx = dst->min[0] + ((float)x + 0.5f) * dst->voxel;
        float wy = dst->min[1] + ((float)y + 0.5f) * dst->voxel;
        float wz = dst->min[2] + ((float)z + 0.5f) * dst->voxel;
        dst->data[(size_t)(z*dst->dims[1]+y)*dst->dims[0]+x] = sdf_field_sample(src, wx, wy, wz);
    }
}
