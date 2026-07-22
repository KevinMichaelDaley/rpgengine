/**
 * @file gi_static_volume.c
 * @brief Baked-irradiance 3D texture upload (see gi_static_volume.h).
 */
#include "ferrum/renderer/gi/gi_static_volume.h"

#include <glad/glad.h>
#include <string.h>

bool gi_static_volume_upload(gi_static_volume_t *v, const float *rgb,
                             const int dims[3], const float origin[3],
                             float voxel)
{
    if (v == NULL || rgb == NULL || dims == NULL || origin == NULL)
        return false;
    if (dims[0] < 1 || dims[1] < 1 || dims[2] < 1 || voxel <= 0.0f)
        return false;

    memset(v, 0, sizeof *v);
    for (int i = 0; i < 3; ++i) { v->origin[i] = origin[i]; v->dims[i] = dims[i]; }
    v->voxel = voxel;

    glGenTextures(1, &v->tex);
    glBindTexture(GL_TEXTURE_3D, v->tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F,
                 dims[0], dims[1], dims[2], 0, GL_RGB, GL_FLOAT, rgb);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);
    return v->tex != 0;
}

bool gi_static_volume_refresh(gi_static_volume_t *v, const float *rgb,
                              const int dims[3], const float origin[3],
                              float voxel)
{
    if (v == NULL || rgb == NULL || dims == NULL || origin == NULL)
        return false;
    /* Dims changed or never created: full (re)upload. */
    if (v->tex == 0 || v->dims[0] != dims[0] || v->dims[1] != dims[1] ||
        v->dims[2] != dims[2]) {
        gi_static_volume_destroy(v);
        return gi_static_volume_upload(v, rgb, dims, origin, voxel);
    }
    /* Same-shape window: subimage into the EXISTING texture (no re-create, no
     * re-point needed beyond the origin uniforms). */
    for (int i = 0; i < 3; ++i) v->origin[i] = origin[i];
    v->voxel = voxel;
    glBindTexture(GL_TEXTURE_3D, v->tex);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, dims[0], dims[1], dims[2],
                    GL_RGB, GL_FLOAT, rgb);
    glBindTexture(GL_TEXTURE_3D, 0);
    return true;
}

void gi_static_volume_destroy(gi_static_volume_t *v)
{
    if (v == NULL) return;
    if (v->tex) glDeleteTextures(1, &v->tex);
    memset(v, 0, sizeof *v);
}
