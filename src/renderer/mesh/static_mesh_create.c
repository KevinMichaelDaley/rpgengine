/**
 * @file static_mesh_create.c
 * @brief Create a static mesh from raw attribute arrays.
 *
 * Allocates GPU resources (VAO, VBOs, IBO), binds attributes at
 * canonical locations, computes bounding volumes, and populates
 * the submesh array.
 */

#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/gl_constants.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Default attribute data for missing optional attributes ──────── */

static const float s_default_tangent[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
static const float s_default_uv[2]      = { 0.0f, 0.0f };
static const float s_default_color[4]   = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float s_default_normal[3]  = { 0.0f, 1.0f, 0.0f };

/* ── Helpers ─────────────────────────────────────────────────────── */

/**
 * @brief Upload a VBO with either provided data or 1-element default.
 */
static int upload_vbo_(vbo_t *vbo, const gl_loader_t *loader,
                       const float *data, uint32_t vertex_count,
                       uint32_t components,
                       const float *default_data)
{
    if (vbo_create(vbo, loader) != VBO_OK) {
        return STATIC_MESH_ERR_GL;
    }
    if (data) {
        size_t size = (size_t)vertex_count * components * sizeof(float);
        if (vbo_upload(vbo, GL_ARRAY_BUFFER, data, size,
                       GL_STATIC_DRAW) != VBO_OK) {
            return STATIC_MESH_ERR_GL;
        }
    } else {
        size_t size = (size_t)components * sizeof(float);
        if (vbo_upload(vbo, GL_ARRAY_BUFFER, default_data, size,
                       GL_STATIC_DRAW) != VBO_OK) {
            return STATIC_MESH_ERR_GL;
        }
    }
    return STATIC_MESH_OK;
}

/**
 * @brief Compute AABB and bounding sphere from positions.
 */
static void compute_bounds_(const float *positions, uint32_t vertex_count,
                            float aabb_min[3], float aabb_max[3],
                            float *bsphere_radius)
{
    aabb_min[0] = aabb_min[1] = aabb_min[2] =  1e30f;
    aabb_max[0] = aabb_max[1] = aabb_max[2] = -1e30f;

    for (uint32_t i = 0; i < vertex_count; i++) {
        float x = positions[i * 3 + 0];
        float y = positions[i * 3 + 1];
        float z = positions[i * 3 + 2];
        if (x < aabb_min[0]) aabb_min[0] = x;
        if (y < aabb_min[1]) aabb_min[1] = y;
        if (z < aabb_min[2]) aabb_min[2] = z;
        if (x > aabb_max[0]) aabb_max[0] = x;
        if (y > aabb_max[1]) aabb_max[1] = y;
        if (z > aabb_max[2]) aabb_max[2] = z;
    }

    /* Bounding sphere: centroid + max distance. */
    float cx = (aabb_min[0] + aabb_max[0]) * 0.5f;
    float cy = (aabb_min[1] + aabb_max[1]) * 0.5f;
    float cz = (aabb_min[2] + aabb_max[2]) * 0.5f;
    float max_r2 = 0.0f;
    for (uint32_t i = 0; i < vertex_count; i++) {
        float dx = positions[i * 3 + 0] - cx;
        float dy = positions[i * 3 + 1] - cy;
        float dz = positions[i * 3 + 2] - cz;
        float r2 = dx * dx + dy * dy + dz * dz;
        if (r2 > max_r2) max_r2 = r2;
    }
    *bsphere_radius = sqrtf(max_r2);
}

/* ── Public API ──────────────────────────────────────────────────── */

int static_mesh_create(const gl_loader_t *loader,
                       const static_mesh_create_info_t *info,
                       static_mesh_t *out)
{
    if (!loader || !info || !out) {
        return STATIC_MESH_ERR_INVALID;
    }
    if (!info->positions || info->vertex_count == 0) {
        return STATIC_MESH_ERR_INVALID;
    }
    if (!info->indices || info->index_count == 0) {
        return STATIC_MESH_ERR_INVALID;
    }

    memset(out, 0, sizeof(*out));

    /* Position VBO (required). */
    int rc = upload_vbo_(&out->vbo_position, loader,
                         info->positions, info->vertex_count, 3, NULL);
    if (rc != STATIC_MESH_OK) goto fail;

    /* Normal VBO. */
    rc = upload_vbo_(&out->vbo_normal, loader,
                     info->normals, info->vertex_count, 3, s_default_normal);
    if (rc != STATIC_MESH_OK) goto fail;

    /* Tangent VBO. */
    rc = upload_vbo_(&out->vbo_tangent, loader,
                     info->tangents, info->vertex_count, 4, s_default_tangent);
    if (rc != STATIC_MESH_OK) goto fail;

    /* UV0 VBO. */
    rc = upload_vbo_(&out->vbo_uv0, loader,
                     info->uv0, info->vertex_count, 2, s_default_uv);
    if (rc != STATIC_MESH_OK) goto fail;

    /* UV1 VBO. */
    rc = upload_vbo_(&out->vbo_uv1, loader,
                     info->uv1, info->vertex_count, 2, s_default_uv);
    if (rc != STATIC_MESH_OK) goto fail;

    /* Color VBO. */
    rc = upload_vbo_(&out->vbo_color, loader,
                     info->colors, info->vertex_count, 4, s_default_color);
    if (rc != STATIC_MESH_OK) goto fail;

    /* Index buffer. */
    if (vbo_create(&out->ibo, loader) != VBO_OK) goto fail;
    {
        size_t ibo_size = (size_t)info->index_count * sizeof(uint32_t);
        if (vbo_upload(&out->ibo, GL_ELEMENT_ARRAY_BUFFER,
                       info->indices, ibo_size,
                       GL_STATIC_DRAW) != VBO_OK) {
            goto fail;
        }
    }

    /* VAO: bind all attribute VBOs at canonical locations. */
    if (vao_create(&out->vao, loader) != VAO_OK) goto fail;

    /* Helper: bind one attribute VBO to the VAO. */
    #define BIND_ATTR(vbo_ptr, loc, comps, stride_bytes) do { \
        vao_attribute_t attr = { \
            (uint32_t)(loc), (comps), GL_FLOAT, 0u, 0u, 0u \
        }; \
        vao_bind_attributes(&out->vao, (vbo_ptr), &attr, 1u, \
                            (size_t)(stride_bytes)); \
    } while (0)

    BIND_ATTR(&out->vbo_position, 0, 3, 3 * sizeof(float));
    BIND_ATTR(&out->vbo_normal,   1, 3, 3 * sizeof(float));
    BIND_ATTR(&out->vbo_tangent,  2, 4, 4 * sizeof(float));
    BIND_ATTR(&out->vbo_uv0,     3, 2, 2 * sizeof(float));
    BIND_ATTR(&out->vbo_uv1,     4, 2, 2 * sizeof(float));
    BIND_ATTR(&out->vbo_color,   5, 4, 4 * sizeof(float));

    #undef BIND_ATTR

    /* Bind the IBO to the VAO. */
    if (out->vao.glBindVertexArray && out->vao.glBindBuffer) {
        out->vao.glBindVertexArray(out->vao.handle);
        out->vao.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ibo.handle);
        out->vao.glBindVertexArray(0);
    }

    /* Resolve glDrawElements via loader.
     * Use a union to safely convert void* to a function pointer (C11
     * §6.5.2.3 allows type-punning through union members). */
    if (loader->get_proc_address) {
        union { void *obj; void (*fn)(uint32_t, int32_t, uint32_t,
                                      const void *); } u;
        u.obj = loader->get_proc_address("glDrawElements", loader->user_data);
        out->glDrawElements = u.fn;
    }

    /* Counts. */
    out->vertex_count = info->vertex_count;
    out->index_count  = info->index_count;
    out->lightmap_resolution = info->lightmap_resolution;

    /* Submeshes. */
    if (info->submeshes && info->submesh_count > 0) {
        size_t sz = (size_t)info->submesh_count * sizeof(render_submesh_t);
        out->submeshes = (render_submesh_t *)malloc(sz);
        if (!out->submeshes) { rc = STATIC_MESH_ERR_OOM; goto fail; }
        memcpy(out->submeshes, info->submeshes, sz);
        out->submesh_count = info->submesh_count;
    } else {
        out->submeshes = (render_submesh_t *)malloc(sizeof(render_submesh_t));
        if (!out->submeshes) { rc = STATIC_MESH_ERR_OOM; goto fail; }
        out->submeshes[0].index_offset  = 0;
        out->submeshes[0].index_count   = info->index_count;
        out->submeshes[0].material_slot = 0;
        out->submesh_count = 1;
    }

    /* Bounds. */
    compute_bounds_(info->positions, info->vertex_count,
                    out->aabb_min, out->aabb_max, &out->bsphere_radius);

    return STATIC_MESH_OK;

fail:
    static_mesh_destroy(out);
    return (rc != STATIC_MESH_OK) ? rc : STATIC_MESH_ERR_GL;
}
