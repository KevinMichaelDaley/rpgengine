/**
 * @file skeletal_mesh_fvma.c
 * @brief Create a skeletal mesh from FVMA binary data with bone attributes.
 *
 * FVMA with MESH_VAO_FLAG_BONES appends after standard geometry:
 *   [4]  bone_count (uint32)
 *   [vc × 16] bone_weights (vec4 per vertex, float)
 *   [vc × 16] bone_indices (uvec4 per vertex, uint32)
 *   [bone_count × 64] inv_bind_matrices (mat4 per bone, float row-major)
 *
 * Standard geometry is deserialized via mesh_vao_deserialize() into
 * a mesh_slot_t, then combined with bone data into a
 * skeletal_mesh_create_info_t.
 */

#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdlib.h>
#include <string.h>

/** @brief Read a uint32_t from a byte stream (little-endian). */
static uint32_t read_u32_(const uint8_t **p)
{
    uint32_t val;
    memcpy(&val, *p, 4);
    *p += 4;
    return val;
}

int skeletal_mesh_create_from_fvma(const gl_loader_t *loader,
                                   const uint8_t *fvma_data,
                                   size_t fvma_size,
                                   skeletal_mesh_t *out)
{
    if (!loader || !fvma_data || fvma_size == 0 || !out) {
        return SKELETAL_MESH_ERR_INVALID;
    }

    /* Peek at header to check BONES flag before full deserialize. */
    if (fvma_size < MESH_VAO_HEADER_SIZE) return SKELETAL_MESH_ERR_FORMAT;

    uint32_t magic;
    memcpy(&magic, fvma_data, 4);
    if (magic != MESH_VAO_MAGIC) return SKELETAL_MESH_ERR_FORMAT;

    uint32_t flags;
    memcpy(&flags, fvma_data + 16, 4);
    if (!(flags & MESH_VAO_FLAG_BONES)) return SKELETAL_MESH_ERR_FORMAT;

    /* Deserialize standard geometry into mesh_slot_t.
     * mesh_vao_deserialize() calls mesh_slot_init() internally. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    if (!mesh_vao_deserialize(fvma_data, fvma_size, &slot)) {
        return SKELETAL_MESH_ERR_FORMAT;
    }

    /* Compute offset to bone data: after all standard geometry.
     * We need to manually walk past the header + attribute data
     * to find where bone data starts. */
    uint32_t vc = slot.vertex_count;
    uint32_t ic = slot.index_count;
    uint32_t fc = ic / 3;

    size_t offset = MESH_VAO_HEADER_SIZE;
    offset += (size_t)vc * 12; /* positions always */
    if (flags & MESH_VAO_FLAG_NORMALS)  offset += (size_t)vc * 12;
    if (flags & MESH_VAO_FLAG_TANGENTS) offset += (size_t)vc * 16;
    if (flags & MESH_VAO_FLAG_UV0)      offset += (size_t)vc * 8;
    if (flags & MESH_VAO_FLAG_UV1)      offset += (size_t)vc * 8;
    if (flags & MESH_VAO_FLAG_COLORS)   offset += (size_t)vc * 16;
    offset += (size_t)ic * 4;  /* indices */
    offset += (size_t)fc * 2;  /* polygroup IDs */

    /* Read bone_count. */
    if (offset + 4 > fvma_size) {
        mesh_slot_destroy(&slot);
        return SKELETAL_MESH_ERR_FORMAT;
    }
    const uint8_t *p = fvma_data + offset;
    uint32_t bone_count = read_u32_(&p);
    if (bone_count == 0) {
        mesh_slot_destroy(&slot);
        return SKELETAL_MESH_ERR_FORMAT;
    }

    /* Validate remaining data size. */
    size_t bone_data_size = (size_t)vc * 16    /* bone_weights */
                          + (size_t)vc * 16    /* bone_indices */
                          + (size_t)bone_count * 64; /* inv_bind_matrices */
    if ((size_t)(p - fvma_data) + bone_data_size > fvma_size) {
        mesh_slot_destroy(&slot);
        return SKELETAL_MESH_ERR_FORMAT;
    }

    /* Read bone weights (may need normalization). */
    const float *bone_weights_raw = (const float *)p;
    p += (size_t)vc * 16;

    /* Read bone indices. */
    const uint32_t *bone_indices = (const uint32_t *)p;
    p += (size_t)vc * 16;

    /* Normalize bone weights so each vertex sums to 1.0.
     * Linear blend skinning requires Σw=1 for rigid transforms;
     * raw Blender weights may not sum to 1. */
    float *bone_weights = (float *)malloc((size_t)vc * 4 * sizeof(float));
    if (!bone_weights) {
        mesh_slot_destroy(&slot);
        return SKELETAL_MESH_ERR_OOM;
    }
    for (uint32_t vi = 0; vi < vc; vi++) {
        float w0 = bone_weights_raw[vi * 4 + 0];
        float w1 = bone_weights_raw[vi * 4 + 1];
        float w2 = bone_weights_raw[vi * 4 + 2];
        float w3 = bone_weights_raw[vi * 4 + 3];
        float sum = w0 + w1 + w2 + w3;
        if (sum > 1e-6f) {
            float inv = 1.0f / sum;
            bone_weights[vi * 4 + 0] = w0 * inv;
            bone_weights[vi * 4 + 1] = w1 * inv;
            bone_weights[vi * 4 + 2] = w2 * inv;
            bone_weights[vi * 4 + 3] = w3 * inv;
        } else {
            /* Fallback: full weight on first bone. */
            bone_weights[vi * 4 + 0] = 1.0f;
            bone_weights[vi * 4 + 1] = 0.0f;
            bone_weights[vi * 4 + 2] = 0.0f;
            bone_weights[vi * 4 + 3] = 0.0f;
        }
    }

    /* Read inverse-bind matrices. */
    const float *inv_bind = (const float *)p;

    /* Build create info. */
    skeletal_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.base.positions    = slot.positions;
    info.base.normals      = slot.normals;
    info.base.tangents     = slot.tangents;
    info.base.uv0          = slot.uvs[0];
    info.base.uv1          = slot.uvs[1];
    info.base.colors       = slot.colors;
    info.base.indices      = slot.indices;
    info.base.vertex_count = vc;
    info.base.index_count  = ic;
    info.bone_weights      = bone_weights;
    info.bone_indices      = bone_indices;
    info.bone_count        = bone_count;
    info.inv_bind_matrices = inv_bind;

    int rc = skeletal_mesh_create(loader, &info, out);
    free(bone_weights);
    mesh_slot_destroy(&slot);
    return rc;
}
