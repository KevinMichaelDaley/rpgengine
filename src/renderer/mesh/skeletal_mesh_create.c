/**
 * @file skeletal_mesh_create.c
 * @brief Create a skeletal mesh from raw attribute arrays.
 *
 * Delegates static geometry creation to static_mesh_create(), then
 * uploads bone weight/index VBOs at attribute locations 6 and 7,
 * and deep-copies the inverse-bind matrix array.
 */

#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/renderer/gl_constants.h"

#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

/**
 * @brief Validate bone-related fields in the create info.
 */
static int validate_bone_fields_(const skeletal_mesh_create_info_t *info)
{
    if (!info->bone_weights)      return SKELETAL_MESH_ERR_INVALID;
    if (!info->bone_indices)      return SKELETAL_MESH_ERR_INVALID;
    if (info->bone_count == 0)    return SKELETAL_MESH_ERR_INVALID;
    if (!info->inv_bind_matrices) return SKELETAL_MESH_ERR_INVALID;
    return SKELETAL_MESH_OK;
}

/**
 * @brief Upload bone weight VBO (vec4 per vertex, float attribute).
 */
static int upload_bone_weights_(vbo_t *vbo, const gl_loader_t *loader,
                                const float *weights, uint32_t vertex_count,
                                vao_t *vao)
{
    if (vbo_create(vbo, loader) != VBO_OK) return SKELETAL_MESH_ERR_GL;

    size_t size = (size_t)vertex_count * 4 * sizeof(float);
    if (vbo_upload(vbo, GL_ARRAY_BUFFER, weights, size,
                   GL_STATIC_DRAW) != VBO_OK) {
        return SKELETAL_MESH_ERR_GL;
    }

    /* Bind as vec4 float attribute at location 6. */
    vao_attribute_t attr = {
        SKELETAL_MESH_ATTR_BONE_WEIGHTS, 4, GL_FLOAT, 0u, 0u, 0u
    };
    if (vao_bind_attributes(vao, vbo, &attr, 1u,
                            4 * sizeof(float)) != VAO_OK) {
        return SKELETAL_MESH_ERR_GL;
    }

    return SKELETAL_MESH_OK;
}

/**
 * @brief Upload bone index VBO (uvec4 per vertex, integer attribute).
 */
static int upload_bone_indices_(vbo_t *vbo, const gl_loader_t *loader,
                                const uint32_t *indices, uint32_t vertex_count,
                                vao_t *vao)
{
    if (vbo_create(vbo, loader) != VBO_OK) return SKELETAL_MESH_ERR_GL;

    size_t size = (size_t)vertex_count * 4 * sizeof(uint32_t);
    if (vbo_upload(vbo, GL_ARRAY_BUFFER, indices, size,
                   GL_STATIC_DRAW) != VBO_OK) {
        return SKELETAL_MESH_ERR_GL;
    }

    /* Bind as ivec4 integer attribute at location 7. */
    vao_attribute_t attr = {
        SKELETAL_MESH_ATTR_BONE_INDICES, 4, GL_UNSIGNED_INT, 0u, 0u, 1u
    };
    if (vao_bind_attributes(vao, vbo, &attr, 1u,
                            4 * sizeof(uint32_t)) != VAO_OK) {
        return SKELETAL_MESH_ERR_GL;
    }

    return SKELETAL_MESH_OK;
}

/* ── Public API ──────────────────────────────────────────────────── */

int skeletal_mesh_create(const gl_loader_t *loader,
                         const skeletal_mesh_create_info_t *info,
                         skeletal_mesh_t *out)
{
    if (!loader || !info || !out) return SKELETAL_MESH_ERR_INVALID;

    /* Validate bone fields before doing any work. */
    int rc = validate_bone_fields_(info);
    if (rc != SKELETAL_MESH_OK) return rc;

    memset(out, 0, sizeof(*out));

    /* Create the base static mesh (positions, normals, etc.). */
    rc = static_mesh_create(loader, &info->base, &out->base);
    if (rc != STATIC_MESH_OK) {
        /* Map static_mesh error to skeletal_mesh error. */
        return (rc == STATIC_MESH_ERR_INVALID) ? SKELETAL_MESH_ERR_INVALID
             : (rc == STATIC_MESH_ERR_OOM)     ? SKELETAL_MESH_ERR_OOM
             :                                   SKELETAL_MESH_ERR_GL;
    }

    /* Upload bone weight VBO at location 6. */
    rc = upload_bone_weights_(&out->vbo_bone_weights, loader,
                              info->bone_weights,
                              info->base.vertex_count,
                              &out->base.vao);
    if (rc != SKELETAL_MESH_OK) goto fail;

    /* Upload bone index VBO at location 7. */
    rc = upload_bone_indices_(&out->vbo_bone_indices, loader,
                              info->bone_indices,
                              info->base.vertex_count,
                              &out->base.vao);
    if (rc != SKELETAL_MESH_OK) goto fail;

    /* Deep-copy inverse-bind matrices. */
    size_t mat_size = (size_t)info->bone_count * 16 * sizeof(float);
    out->inv_bind_matrices = (float *)malloc(mat_size);
    if (!out->inv_bind_matrices) { rc = SKELETAL_MESH_ERR_OOM; goto fail; }
    memcpy(out->inv_bind_matrices, info->inv_bind_matrices, mat_size);
    out->bone_count = info->bone_count;

    return SKELETAL_MESH_OK;

fail:
    skeletal_mesh_destroy(out);
    return rc;
}
