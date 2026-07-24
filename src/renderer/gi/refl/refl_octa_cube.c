/**
 * @file refl_octa_cube.c
 * @brief Cube-face -> octahedral resample (see refl_octa.h).
 */
#include "ferrum/renderer/gi/refl_octa.h"

#include <math.h>
#include <stddef.h>

/* GL cube-face selection for direction d: face index + in-face (s,t) in
 * [0,1], matching the GL_TEXTURE_CUBE_MAP_POSITIVE_X.. layout. */
static uint32_t cube_face_st(const float d[3], float *s, float *t)
{
    float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
    uint32_t face;
    float sc, tc, ma;
    if (ax >= ay && ax >= az) {
        ma = ax;
        if (d[0] >= 0.0f) { face = 0u; sc = -d[2]; tc = -d[1]; }
        else              { face = 1u; sc =  d[2]; tc = -d[1]; }
    } else if (ay >= az) {
        ma = ay;
        if (d[1] >= 0.0f) { face = 2u; sc =  d[0]; tc =  d[2]; }
        else              { face = 3u; sc =  d[0]; tc = -d[2]; }
    } else {
        ma = az;
        if (d[2] >= 0.0f) { face = 4u; sc =  d[0]; tc = -d[1]; }
        else              { face = 5u; sc = -d[0]; tc = -d[1]; }
    }
    if (ma < 1e-8f) ma = 1e-8f;
    *s = (sc / ma) * 0.5f + 0.5f;
    *t = (tc / ma) * 0.5f + 0.5f;
    return face;
}

/* Bilinear RGBA tap on one face (clamped to edge). */
static void face_tap(const float *face, uint32_t res, float s, float t,
                     float out[4])
{
    float fx = s * (float)res - 0.5f;
    float fy = t * (float)res - 0.5f;
    int32_t x0 = (int32_t)floorf(fx), y0 = (int32_t)floorf(fy);
    float wx = fx - (float)x0, wy = fy - (float)y0;
    for (int c = 0; c < 4; ++c)
        out[c] = 0.0f;
    for (int32_t j = 0; j < 2; ++j)
        for (int32_t i = 0; i < 2; ++i) {
            int32_t x = x0 + i, y = y0 + j;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= (int32_t)res) x = (int32_t)res - 1;
            if (y >= (int32_t)res) y = (int32_t)res - 1;
            float w = (i ? wx : 1.0f - wx) * (j ? wy : 1.0f - wy);
            const float *p = &face[((size_t)y * res + (size_t)x) * 4u];
            for (int c = 0; c < 4; ++c)
                out[c] += p[c] * w;
        }
}

void refl_octa_from_cube(const float *const faces[6], uint32_t face_res,
                         float *dst, uint32_t res)
{
    if (faces == NULL || dst == NULL || face_res == 0u || res == 0u)
        return;
    for (uint32_t f = 0; f < 6u; ++f)
        if (faces[f] == NULL)
            return;
    for (uint32_t y = 0; y < res; ++y)
        for (uint32_t x = 0; x < res; ++x) {
            float uv[2] = { ((float)x + 0.5f) / (float)res,
                            ((float)y + 0.5f) / (float)res };
            float d[3], s, t;
            refl_octa_decode(uv, d);
            uint32_t face = cube_face_st(d, &s, &t);
            face_tap(faces[face], face_res, s, t,
                     &dst[((size_t)y * res + x) * 4u]);
        }
}
