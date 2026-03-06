/**
 * @file gltf_scene.c
 * @brief glTF scene load/destroy/query.
 *
 * Parses a .glb/.gltf file via cgltf, stores parsed data and
 * pre-computed mesh metadata for later mesh creation.
 */

#include "ferrum/renderer/gltf/gltf_loader.h"
#include "cgltf.h"

#include <stdlib.h>
#include <string.h>

/* ── Internal scene struct ────────────────────────────────────────── */

struct gltf_scene {
    cgltf_data      *data;        /**< Parsed cgltf data (owns memory). */
    gltf_mesh_info_t *mesh_infos; /**< Pre-computed per-mesh metadata. */
    uint32_t          mesh_count; /**< Number of meshes. */
};

/* ── gltf_scene_load ──────────────────────────────────────────────── */

gltf_status_t gltf_scene_load(const char *path, gltf_scene_t **out) {
    if (!path || !out) { return GLTF_ERR_INVALID; }
    *out = NULL;

    /* Parse with cgltf. */
    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data *data = NULL;
    cgltf_result res = cgltf_parse_file(&options, path, &data);
    if (res != cgltf_result_success) {
        return (res == cgltf_result_file_not_found) ? GLTF_ERR_IO
                                                     : GLTF_ERR_PARSE;
    }

    /* Load external buffers (for GLB, buffers are inline). */
    res = cgltf_load_buffers(&options, data, path);
    if (res != cgltf_result_success) {
        cgltf_free(data);
        return GLTF_ERR_IO;
    }

    /* Validate. */
    res = cgltf_validate(data);
    if (res != cgltf_result_success) {
        cgltf_free(data);
        return GLTF_ERR_PARSE;
    }

    if (data->meshes_count == 0) {
        cgltf_free(data);
        return GLTF_ERR_NO_MESHES;
    }

    /* Allocate scene. */
    gltf_scene_t *scene = (gltf_scene_t *)calloc(1, sizeof(gltf_scene_t));
    if (!scene) { cgltf_free(data); return GLTF_ERR_ALLOC; }

    scene->data = data;
    scene->mesh_count = (uint32_t)data->meshes_count;

    /* Pre-compute mesh metadata. */
    scene->mesh_infos = (gltf_mesh_info_t *)calloc(
        scene->mesh_count, sizeof(gltf_mesh_info_t));
    if (!scene->mesh_infos) {
        cgltf_free(data);
        free(scene);
        return GLTF_ERR_ALLOC;
    }

    for (uint32_t i = 0; i < scene->mesh_count; ++i) {
        cgltf_mesh *m = &data->meshes[i];
        gltf_mesh_info_t *info = &scene->mesh_infos[i];

        /* Name. */
        if (m->name) {
            strncpy(info->name, m->name, sizeof(info->name) - 1);
        }
        info->mesh_index = (int)i;
        info->submesh_count = (uint32_t)m->primitives_count;

        /* Accumulate vertex/index counts and check for skinning. */
        for (cgltf_size p = 0; p < m->primitives_count; ++p) {
            cgltf_primitive *prim = &m->primitives[p];
            if (prim->indices) {
                info->index_count += (uint32_t)prim->indices->count;
            }
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    info->vertex_count += (uint32_t)prim->attributes[a].data->count;
                }
                if (prim->attributes[a].type == cgltf_attribute_type_joints) {
                    info->is_skinned = 1;
                }
            }
        }
    }

    *out = scene;
    return GLTF_OK;
}

/* ── gltf_scene_destroy ───────────────────────────────────────────── */

void gltf_scene_destroy(gltf_scene_t *scene) {
    if (!scene) { return; }
    if (scene->data) { cgltf_free(scene->data); }
    free(scene->mesh_infos);
    free(scene);
}

/* ── gltf_scene_mesh_count ────────────────────────────────────────── */

uint32_t gltf_scene_mesh_count(const gltf_scene_t *scene) {
    return scene ? scene->mesh_count : 0;
}

/* ── gltf_scene_mesh_info ─────────────────────────────────────────── */

gltf_status_t gltf_scene_mesh_info(const gltf_scene_t *scene,
                                    uint32_t index,
                                    gltf_mesh_info_t *info) {
    if (!scene || !info || index >= scene->mesh_count) {
        return GLTF_ERR_INVALID;
    }
    *info = scene->mesh_infos[index];
    return GLTF_OK;
}

/* ── gltf_scene_joint_count ───────────────────────────────────────── */

uint32_t gltf_scene_joint_count(const gltf_scene_t *scene) {
    if (!scene || !scene->data || scene->data->skins_count == 0) {
        return 0;
    }
    return (uint32_t)scene->data->skins[0].joints_count;
}
