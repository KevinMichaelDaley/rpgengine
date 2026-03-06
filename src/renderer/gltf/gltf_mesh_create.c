/**
 * @file gltf_mesh_create.c
 * @brief Create static_mesh_t / skeletal_mesh_t from parsed glTF data.
 *
 * Extracts vertex attributes from cgltf accessors, merges multi-primitive
 * meshes into a single vertex/index buffer with submeshes.
 */

#include "ferrum/renderer/gltf/gltf_loader.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/renderer/gl_loader.h"
#include "cgltf.h"

#include <stdlib.h>
#include <string.h>

/* Access internal scene struct (defined in gltf_scene.c). */
struct gltf_scene {
    cgltf_data       *data;
    gltf_mesh_info_t *mesh_infos;
    uint32_t          mesh_count;
};

/* ── Accessor helpers ─────────────────────────────────────────────── */

/** Read a float component from an accessor at the given index. */
static cgltf_bool read_floats_(const cgltf_accessor *acc, cgltf_size idx,
                                float *out, cgltf_size count) {
    return cgltf_accessor_read_float(acc, idx, out, count);
}

/** Read a uint component from an accessor at the given index. */
static cgltf_bool read_uint_(const cgltf_accessor *acc, cgltf_size idx,
                              cgltf_uint *out, cgltf_size count) {
    return cgltf_accessor_read_uint(acc, idx, out, count);
}

/** Find accessor for a named attribute in a primitive. */
static const cgltf_accessor *find_attr_(const cgltf_primitive *prim,
                                         cgltf_attribute_type type,
                                         int index) {
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        if (prim->attributes[i].type == type &&
            prim->attributes[i].index == index) {
            return prim->attributes[i].data;
        }
    }
    return NULL;
}

/* ── Extract geometry into flat arrays ────────────────────────────── */

/**
 * @brief Extract all vertex/index data from a cgltf_mesh.
 *
 * Caller must free all returned arrays.
 * Returns 0 on success, -1 on failure.
 */
static int extract_mesh_data_(
    const cgltf_mesh *mesh,
    float **out_positions,   /* vec3 × total_verts */
    float **out_normals,     /* vec3 × total_verts (NULL if none) */
    float **out_tangents,    /* vec4 × total_verts (NULL if none) */
    float **out_uv0,         /* vec2 × total_verts (NULL if none) */
    float **out_uv1,         /* vec2 × total_verts (NULL if none) */
    float **out_colors,      /* vec4 × total_verts (NULL if none) */
    uint32_t **out_indices,
    float **out_bone_weights,    /* vec4 × total_verts (NULL if none) */
    uint32_t **out_bone_indices, /* uvec4 × total_verts (NULL if none) */
    render_submesh_t **out_submeshes,
    uint32_t *out_vertex_count,
    uint32_t *out_index_count,
    uint32_t *out_submesh_count,
    int *out_has_skin)
{
    /* First pass: count totals. */
    uint32_t total_verts = 0, total_indices = 0;
    int has_normals = 0, has_tangents = 0, has_uv0 = 0, has_uv1 = 0;
    int has_colors = 0, has_joints = 0;

    for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
        const cgltf_primitive *prim = &mesh->primitives[p];
        const cgltf_accessor *pos = find_attr_(prim, cgltf_attribute_type_position, 0);
        if (!pos) { return -1; }
        total_verts += (uint32_t)pos->count;
        if (prim->indices) { total_indices += (uint32_t)prim->indices->count; }

        if (find_attr_(prim, cgltf_attribute_type_normal, 0))   { has_normals = 1; }
        if (find_attr_(prim, cgltf_attribute_type_tangent, 0))  { has_tangents = 1; }
        if (find_attr_(prim, cgltf_attribute_type_texcoord, 0)) { has_uv0 = 1; }
        if (find_attr_(prim, cgltf_attribute_type_texcoord, 1)) { has_uv1 = 1; }
        if (find_attr_(prim, cgltf_attribute_type_color, 0))    { has_colors = 1; }
        if (find_attr_(prim, cgltf_attribute_type_joints, 0))   { has_joints = 1; }
    }

    if (total_verts == 0) { return -1; }

    /* Allocate arrays. */
    float *positions = (float *)calloc((size_t)total_verts * 3, sizeof(float));
    float *normals   = has_normals  ? (float *)calloc((size_t)total_verts * 3, sizeof(float)) : NULL;
    float *tangents  = has_tangents ? (float *)calloc((size_t)total_verts * 4, sizeof(float)) : NULL;
    float *uv0       = has_uv0     ? (float *)calloc((size_t)total_verts * 2, sizeof(float)) : NULL;
    float *uv1       = has_uv1     ? (float *)calloc((size_t)total_verts * 2, sizeof(float)) : NULL;
    float *colors    = has_colors   ? (float *)calloc((size_t)total_verts * 4, sizeof(float)) : NULL;
    uint32_t *indices = total_indices > 0 ? (uint32_t *)calloc(total_indices, sizeof(uint32_t)) : NULL;

    float    *bw = has_joints ? (float *)calloc((size_t)total_verts * 4, sizeof(float))    : NULL;
    uint32_t *bi = has_joints ? (uint32_t *)calloc((size_t)total_verts * 4, sizeof(uint32_t)) : NULL;

    render_submesh_t *submeshes = (render_submesh_t *)calloc(
        mesh->primitives_count, sizeof(render_submesh_t));

    if (!positions || (total_indices > 0 && !indices) || !submeshes) {
        free(positions); free(normals); free(tangents); free(uv0); free(uv1);
        free(colors); free(indices); free(bw); free(bi); free(submeshes);
        return -1;
    }

    /* Second pass: read data. */
    uint32_t vert_offset = 0, idx_offset = 0;

    for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
        const cgltf_primitive *prim = &mesh->primitives[p];
        const cgltf_accessor *pos_acc = find_attr_(prim, cgltf_attribute_type_position, 0);
        uint32_t prim_verts = (uint32_t)pos_acc->count;

        /* Positions (required). */
        for (uint32_t v = 0; v < prim_verts; ++v) {
            read_floats_(pos_acc, v, &positions[(vert_offset + v) * 3], 3);
        }

        /* Normals. */
        const cgltf_accessor *nrm = find_attr_(prim, cgltf_attribute_type_normal, 0);
        if (nrm && normals) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                read_floats_(nrm, v, &normals[(vert_offset + v) * 3], 3);
            }
        }

        /* Tangents. */
        const cgltf_accessor *tan = find_attr_(prim, cgltf_attribute_type_tangent, 0);
        if (tan && tangents) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                read_floats_(tan, v, &tangents[(vert_offset + v) * 4], 4);
            }
        }

        /* UV0. */
        const cgltf_accessor *tc0 = find_attr_(prim, cgltf_attribute_type_texcoord, 0);
        if (tc0 && uv0) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                read_floats_(tc0, v, &uv0[(vert_offset + v) * 2], 2);
            }
        }

        /* UV1. */
        const cgltf_accessor *tc1 = find_attr_(prim, cgltf_attribute_type_texcoord, 1);
        if (tc1 && uv1) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                read_floats_(tc1, v, &uv1[(vert_offset + v) * 2], 2);
            }
        }

        /* Colors. */
        const cgltf_accessor *col = find_attr_(prim, cgltf_attribute_type_color, 0);
        if (col && colors) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                read_floats_(col, v, &colors[(vert_offset + v) * 4], 4);
            }
        }

        /* Joints + Weights. */
        const cgltf_accessor *j0 = find_attr_(prim, cgltf_attribute_type_joints, 0);
        const cgltf_accessor *w0 = find_attr_(prim, cgltf_attribute_type_weights, 0);
        if (j0 && bi) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                cgltf_uint tmp[4] = {0};
                read_uint_(j0, v, tmp, 4);
                bi[(vert_offset + v) * 4 + 0] = tmp[0];
                bi[(vert_offset + v) * 4 + 1] = tmp[1];
                bi[(vert_offset + v) * 4 + 2] = tmp[2];
                bi[(vert_offset + v) * 4 + 3] = tmp[3];
            }
        }
        if (w0 && bw) {
            for (uint32_t v = 0; v < prim_verts; ++v) {
                read_floats_(w0, v, &bw[(vert_offset + v) * 4], 4);
            }
        }

        /* Indices (offset by vert_offset to merge primitives). */
        uint32_t prim_indices = 0;
        if (prim->indices) {
            prim_indices = (uint32_t)prim->indices->count;
            for (uint32_t k = 0; k < prim_indices; ++k) {
                cgltf_uint raw = 0;
                read_uint_(prim->indices, k, &raw, 1);
                indices[idx_offset + k] = raw + vert_offset;
            }
        }

        /* Submesh. */
        submeshes[p].index_offset = idx_offset;
        submeshes[p].index_count  = prim_indices;
        submeshes[p].material_slot = (uint16_t)p;

        vert_offset += prim_verts;
        idx_offset  += prim_indices;
    }

    *out_positions    = positions;
    *out_normals      = normals;
    *out_tangents     = tangents;
    *out_uv0          = uv0;
    *out_uv1          = uv1;
    *out_colors       = colors;
    *out_indices      = indices;
    *out_bone_weights = bw;
    *out_bone_indices = bi;
    *out_submeshes    = submeshes;
    *out_vertex_count = vert_offset;
    *out_index_count  = idx_offset;
    *out_submesh_count = (uint32_t)mesh->primitives_count;
    *out_has_skin     = has_joints;
    return 0;
}

/* ── Find skin for a mesh node ────────────────────────────────────── */

/** Walk all nodes to find the skin associated with a given mesh pointer. */
static const cgltf_skin *find_skin_for_mesh_(const cgltf_data *data,
                                              const cgltf_mesh *mesh) {
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        if (data->nodes[i].mesh == mesh && data->nodes[i].skin) {
            return data->nodes[i].skin;
        }
    }
    /* Fall back to first skin if any. */
    return data->skins_count > 0 ? &data->skins[0] : NULL;
}

/* ── gltf_scene_create_static_mesh ────────────────────────────────── */

gltf_status_t gltf_scene_create_static_mesh(const gltf_scene_t *scene,
                                             uint32_t index,
                                             const gl_loader_t *loader,
                                             static_mesh_t *out) {
    if (!scene || !loader || !out || index >= scene->mesh_count) {
        return GLTF_ERR_INVALID;
    }

    const cgltf_mesh *mesh = &scene->data->meshes[index];

    float *positions = NULL, *normals = NULL, *tangents = NULL;
    float *uv0 = NULL, *uv1 = NULL, *colors = NULL;
    float *bw = NULL;
    uint32_t *indices = NULL, *bi = NULL;
    render_submesh_t *submeshes = NULL;
    uint32_t vc = 0, ic = 0, sc = 0;
    int has_skin = 0;

    if (extract_mesh_data_(mesh, &positions, &normals, &tangents,
                           &uv0, &uv1, &colors, &indices,
                           &bw, &bi, &submeshes,
                           &vc, &ic, &sc, &has_skin) != 0) {
        return GLTF_ERR_PARSE;
    }

    static_mesh_create_info_t info;
    memset(&info, 0, sizeof(info));
    info.positions    = positions;
    info.normals      = normals;
    info.tangents     = tangents;
    info.uv0          = uv0;
    info.uv1          = uv1;
    info.colors       = colors;
    info.indices      = indices;
    info.vertex_count = vc;
    info.index_count  = ic;
    info.submeshes    = submeshes;
    info.submesh_count = sc;

    int rc = static_mesh_create(loader, &info, out);

    free(positions); free(normals); free(tangents);
    free(uv0); free(uv1); free(colors);
    free(indices); free(bw); free(bi); free(submeshes);

    return (rc == 0) ? GLTF_OK : GLTF_ERR_GPU;
}

/* ── gltf_scene_create_skeletal_mesh ──────────────────────────────── */

gltf_status_t gltf_scene_create_skeletal_mesh(const gltf_scene_t *scene,
                                               uint32_t index,
                                               const gl_loader_t *loader,
                                               skeletal_mesh_t *out) {
    if (!scene || !loader || !out || index >= scene->mesh_count) {
        return GLTF_ERR_INVALID;
    }

    /* Check that this mesh is skinned. */
    if (!scene->mesh_infos[index].is_skinned) {
        return GLTF_ERR_INVALID;
    }

    const cgltf_mesh *mesh = &scene->data->meshes[index];

    float *positions = NULL, *normals = NULL, *tangents = NULL;
    float *uv0 = NULL, *uv1 = NULL, *colors = NULL;
    float *bw = NULL;
    uint32_t *indices = NULL, *bi = NULL;
    render_submesh_t *submeshes = NULL;
    uint32_t vc = 0, ic = 0, sc = 0;
    int has_skin = 0;

    if (extract_mesh_data_(mesh, &positions, &normals, &tangents,
                           &uv0, &uv1, &colors, &indices,
                           &bw, &bi, &submeshes,
                           &vc, &ic, &sc, &has_skin) != 0) {
        return GLTF_ERR_PARSE;
    }

    /* Find the skin and extract inverse bind matrices. */
    const cgltf_skin *skin = find_skin_for_mesh_(scene->data, mesh);
    if (!skin) {
        free(positions); free(normals); free(tangents);
        free(uv0); free(uv1); free(colors);
        free(indices); free(bw); free(bi); free(submeshes);
        return GLTF_ERR_PARSE;
    }

    uint32_t bone_count = (uint32_t)skin->joints_count;
    float *inv_bind = (float *)calloc((size_t)bone_count * 16, sizeof(float));
    if (!inv_bind) {
        free(positions); free(normals); free(tangents);
        free(uv0); free(uv1); free(colors);
        free(indices); free(bw); free(bi); free(submeshes);
        return GLTF_ERR_ALLOC;
    }

    /* Read inverse bind matrices from accessor. */
    if (skin->inverse_bind_matrices) {
        for (uint32_t j = 0; j < bone_count; ++j) {
            cgltf_accessor_read_float(skin->inverse_bind_matrices, j,
                                      &inv_bind[j * 16], 16);
        }
    } else {
        /* No IBM accessor — use identity for all bones. */
        for (uint32_t j = 0; j < bone_count; ++j) {
            inv_bind[j * 16 + 0]  = 1.0f;
            inv_bind[j * 16 + 5]  = 1.0f;
            inv_bind[j * 16 + 10] = 1.0f;
            inv_bind[j * 16 + 15] = 1.0f;
        }
    }

    skeletal_mesh_create_info_t sinfo;
    memset(&sinfo, 0, sizeof(sinfo));
    sinfo.base.positions    = positions;
    sinfo.base.normals      = normals;
    sinfo.base.tangents     = tangents;
    sinfo.base.uv0          = uv0;
    sinfo.base.uv1          = uv1;
    sinfo.base.colors       = colors;
    sinfo.base.indices      = indices;
    sinfo.base.vertex_count = vc;
    sinfo.base.index_count  = ic;
    sinfo.base.submeshes    = submeshes;
    sinfo.base.submesh_count = sc;
    sinfo.bone_weights      = bw;
    sinfo.bone_indices      = bi;
    sinfo.bone_count        = bone_count;
    sinfo.inv_bind_matrices = inv_bind;

    int rc = skeletal_mesh_create(loader, &sinfo, out);

    free(positions); free(normals); free(tangents);
    free(uv0); free(uv1); free(colors);
    free(indices); free(bw); free(bi); free(submeshes);
    free(inv_bind);

    return (rc == 0) ? GLTF_OK : GLTF_ERR_GPU;
}
