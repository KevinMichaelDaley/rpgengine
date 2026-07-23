/**
 * @file lm_gpu_voxvol.c
 * @brief Channel-volume sets for the sliced-render-target voxelizer (see
 *        lm_gpu_voxelize_internal.h, rpg-bpiz): five 3D textures whose layers
 *        slice along a chosen world axis, plus their readback.
 */
#include "lm_gpu_voxelize_internal.h"

/* (u,v) texture dims = the two non-axis world dims in ascending order. */
static void vols_shape(const int dims[3], int axis, int *u, int *v, int *l)
{
    *u = axis == 0 ? dims[1] : dims[0];
    *v = axis == 2 ? dims[1] : dims[2];
    *l = dims[axis];
}

bool lm_voxi_vols_create(lm_voxi_vols_t *v, const int dims[3], int axis)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    v->axis = axis;
    vols_shape(dims, axis, &v->udim, &v->vdim, &v->layers);
    static const GLenum ifmt[LM_VOX_CHANNELS] = {
        GLV_R32F, GLV_RGBA32F, GLV_RGBA32F, GLV_RGBA32F, GLV_R32F
    };
    gl->GenTextures(LM_VOX_CHANNELS, v->tex);
    for (int c = 0; c < LM_VOX_CHANNELS; ++c) {
        gl->BindTexture(GLV_TEXTURE_3D, v->tex[c]);
        gl->TexParameteri(GLV_TEXTURE_3D, GLV_TEXTURE_MIN_FILTER, GLV_NEAREST);
        gl->TexParameteri(GLV_TEXTURE_3D, GLV_TEXTURE_MAG_FILTER, GLV_NEAREST);
        gl->TexParameteri(GLV_TEXTURE_3D, GLV_TEXTURE_MAX_LEVEL, 0);
        gl->TexImage3D(GLV_TEXTURE_3D, 0, (GLint)ifmt[c], v->udim, v->vdim,
                       v->layers, 0, ifmt[c] == GLV_R32F ? GLV_RED : GLV_RGBA,
                       GLV_FLOAT, NULL);
    }
    return true;
}

void lm_voxi_vols_free(lm_voxi_vols_t *v)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    gl->DeleteTextures(LM_VOX_CHANNELS, v->tex);
    for (int c = 0; c < LM_VOX_CHANNELS; ++c) v->tex[c] = 0u;
}

void lm_voxi_vols_read(const lm_voxi_vols_t *v, int channel, int comps,
                       float *dst)
{
    lm_voxi_gl_t *gl = &lm_voxi_gl;
    gl->PixelStorei(GLV_PACK_ALIGNMENT, 1);
    gl->BindTexture(GLV_TEXTURE_3D, v->tex[channel]);
    gl->GetTexImage(GLV_TEXTURE_3D, 0, comps == 1 ? GLV_RED : GLV_RGBA,
                    GLV_FLOAT, dst);
}
