/**
 * @file lm_sh.c
 * @brief Order-2 real spherical harmonics (see lm_sh.h).
 */
#include "ferrum/lightmap/lm_sh.h"

#include <math.h>
#include <string.h>

/* Ramamoorthi-Hanrahan cosine-lobe convolution factors, per band. */
#define LM_SH_A0 3.14159265358979324f       /* pi        (band 0) */
#define LM_SH_A1 2.09439510239319549f       /* 2*pi/3    (band 1) */
#define LM_SH_A2 0.78539816339744831f       /* pi/4      (band 2) */

void lm_sh9_zero(lm_sh9_t *sh)
{
    memset(sh->c, 0, sizeof sh->c);
}

void lm_sh9_basis(vec3_t dir, float out[9])
{
    const float x = dir.x, y = dir.y, z = dir.z;
    out[0] = 0.282094792f;                    /* Y(0, 0) */
    out[1] = 0.488602512f * y;                /* Y(1,-1) */
    out[2] = 0.488602512f * z;                /* Y(1, 0) */
    out[3] = 0.488602512f * x;                /* Y(1, 1) */
    out[4] = 1.092548431f * x * y;            /* Y(2,-2) */
    out[5] = 1.092548431f * y * z;            /* Y(2,-1) */
    out[6] = 0.315391565f * (3.0f * z * z - 1.0f); /* Y(2, 0) */
    out[7] = 1.092548431f * x * z;            /* Y(2, 1) */
    out[8] = 0.546274215f * (x * x - y * y);  /* Y(2, 2) */
}

void lm_sh9_add_sample(lm_sh9_t *sh, vec3_t dir, float value, float weight)
{
    float y[9];
    lm_sh9_basis(vec3_normalize_safe(dir, 1e-8f), y);
    const float s = value * weight;
    for (int i = 0; i < 9; ++i)
        sh->c[i] += s * y[i];
}

float lm_sh9_irradiance(const lm_sh9_t *sh, vec3_t normal)
{
    float y[9];
    lm_sh9_basis(vec3_normalize_safe(normal, 1e-8f), y);
    const float *c = sh->c;
    return LM_SH_A0 * c[0] * y[0]
           + LM_SH_A1 * (c[1] * y[1] + c[2] * y[2] + c[3] * y[3])
           + LM_SH_A2 * (c[4] * y[4] + c[5] * y[5] + c[6] * y[6]
                         + c[7] * y[7] + c[8] * y[8]);
}
