/**
 * @file gltf_bind_pose.c
 * @brief Compute bind-pose bone matrices from glTF skeleton hierarchy.
 *
 * Walks the cgltf joint node hierarchy to build world-space transforms,
 * then multiplies each by the corresponding inverse bind matrix to
 * produce the final bone matrices for bind-pose rendering.
 *
 * cgltf outputs column-major matrices (OpenGL convention):
 * - cgltf_node_transform_world() → column-major
 * - cgltf_accessor_read_float() for MAT4 → column-major (per glTF spec)
 * No transposing is needed.
 */

#include "ferrum/renderer/gltf/gltf_loader.h"
#include "cgltf.h"

#include <string.h>

/* Access internal scene struct (defined in gltf_scene.c). */
struct gltf_scene {
    cgltf_data       *data;
    gltf_mesh_info_t *mesh_infos;
    uint32_t          mesh_count;
};

/* ── Column-major mat4 helpers ────────────────────────────────────── */

/** Store a 4×4 identity into 16 floats (column-major). */
static void mat4_ident_(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/**
 * Multiply two column-major 4×4 matrices: out = a × b.
 * out may NOT alias a or b.
 */
static void mat4_mul_(float *out, const float *a, const float *b) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
}

/* ── gltf_scene_compute_bind_pose ─────────────────────────────────── */

gltf_status_t gltf_scene_compute_bind_pose(const gltf_scene_t *scene,
                                            float *out_mats,
                                            uint32_t capacity) {
    if (!scene || !out_mats) { return GLTF_ERR_INVALID; }
    if (!scene->data || scene->data->skins_count == 0) {
        return GLTF_ERR_INVALID;
    }

    const cgltf_skin *skin = &scene->data->skins[0];
    uint32_t joint_count = (uint32_t)skin->joints_count;

    if (capacity < joint_count) { return GLTF_ERR_INVALID; }

    /* For each joint: bone[j] = joint_world[j] × inverse_bind[j].
     * Both cgltf_node_transform_world() and the glTF MAT4 accessor
     * output column-major floats — no transposing needed. */
    for (uint32_t j = 0; j < joint_count; ++j) {
        float joint_world[16];
        cgltf_node_transform_world(skin->joints[j], joint_world);

        /* Read inverse bind matrix (column-major per glTF spec). */
        float ibm[16];
        if (skin->inverse_bind_matrices) {
            cgltf_accessor_read_float(skin->inverse_bind_matrices, j,
                                      ibm, 16);
        } else {
            mat4_ident_(ibm);
        }

        /* bone[j] = joint_world × ibm. */
        mat4_mul_(&out_mats[j * 16], joint_world, ibm);
    }

    return GLTF_OK;
}
