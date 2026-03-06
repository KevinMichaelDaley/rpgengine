/**
 * @file fskel_load.c
 * @brief .fskel format loader.
 *
 * Reads skeleton hierarchy, constraints, IBMs, and optional v2 chunks
 * (COLL, JNTS) from a binary file.  Backward-compatible with v1 files.
 *
 * Non-static functions: 1 (fskel_load)
 */

#include "ferrum/animation/fskel_loader.h"
#include "ferrum/animation/fskel_format.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Maximum joints to prevent insane allocations. */
#define FSKEL_MAX_JOINTS 4096

/**
 * @brief Read a 4-byte little-endian uint32.
 */
static bool read_u32(FILE *f, uint32_t *out) {
    return fread(out, 4, 1, f) == 1;
}

bool fskel_load(const char *path,
                skeleton_def_t *out_skel,
                mat4_t **out_ibms,
                uint32_t *out_ibm_count) {
    if (!path || !out_skel) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    /* Read header. */
    uint32_t magic = 0, version = 0, joint_count = 0;
    uint32_t max_constraints = 0, ibm_count = 0;

    if (!read_u32(f, &magic) || magic != FSKEL_MAGIC) goto fail;
    if (!read_u32(f, &version) || (version < 1 || version > FSKEL_VERSION)) goto fail;
    if (!read_u32(f, &joint_count)) goto fail;
    if (!read_u32(f, &max_constraints)) goto fail;
    if (!read_u32(f, &ibm_count)) goto fail;

    /* Sanity checks. */
    if (joint_count > FSKEL_MAX_JOINTS) goto fail;
    if (max_constraints > 64) goto fail;
    if (ibm_count > FSKEL_MAX_JOINTS) goto fail;

    /* Initialize skeleton. */
    if (!skeleton_def_init(out_skel, joint_count, max_constraints)) goto fail;

    /* Read joint names. */
    for (uint32_t i = 0; i < joint_count; i++) {
        if (fread(out_skel->joint_names[i], SKELETON_JOINT_NAME_MAX, 1, f) != 1)
            goto fail_skel;
    }

    /* Read parent indices. */
    if (joint_count > 0 &&
        fread(out_skel->parent_indices, sizeof(uint32_t), joint_count, f) != joint_count)
        goto fail_skel;

    /* Read rest local transforms. */
    if (joint_count > 0 &&
        fread(out_skel->rest_local, sizeof(mat4_t), joint_count, f) != joint_count)
        goto fail_skel;

    /* Read rest world transforms. */
    if (joint_count > 0 &&
        fread(out_skel->rest_world, sizeof(mat4_t), joint_count, f) != joint_count)
        goto fail_skel;

    /* Read constraint counts. */
    if (out_skel->constraint_counts && joint_count > 0) {
        if (fread(out_skel->constraint_counts, sizeof(uint32_t), joint_count, f) != joint_count)
            goto fail_skel;
    }

    /* Read constraints. */
    if (max_constraints > 0 && out_skel->constraints) {
        size_t total = (size_t)joint_count * max_constraints;
        if (fread(out_skel->constraints, sizeof(constraint_def_t), total, f) != total)
            goto fail_skel;
    }

    /* Read IBMs. */
    if (ibm_count > 0 && out_ibms) {
        *out_ibms = (mat4_t *)calloc(ibm_count, sizeof(mat4_t));
        if (!*out_ibms) goto fail_skel;
        if (fread(*out_ibms, sizeof(mat4_t), ibm_count, f) != ibm_count) {
            free(*out_ibms);
            *out_ibms = NULL;
            goto fail_skel;
        }
    }

    if (out_ibm_count) *out_ibm_count = ibm_count;

    /* --- v2 COLL chunk: per-bone collision descriptors --- */
    if (version >= 2 && joint_count > 0) {
        uint32_t hull_vertex_count = 0;
        if (!read_u32(f, &hull_vertex_count)) goto fail_skel;

        out_skel->colliders = (bone_collider_desc_t *)calloc(
            joint_count, sizeof(bone_collider_desc_t));
        if (!out_skel->colliders) goto fail_skel;

        if (version >= 4) {
            /* v4+: 52-byte records (includes collision_group). */
            if (fread(out_skel->colliders, sizeof(bone_collider_desc_t),
                      joint_count, f) != joint_count)
                goto fail_skel;
        } else {
            /* v2/v3: 48-byte records (no collision_group).
             * Read each record individually and zero collision_group. */
            for (uint32_t i = 0; i < joint_count; i++) {
                if (fread(&out_skel->colliders[i], 48, 1, f) != 1)
                    goto fail_skel;
                out_skel->colliders[i].collision_group = 0;
            }
        }

        /* Read convex hull vertex data. */
        if (hull_vertex_count > 0) {
            out_skel->hull_vertices = (float *)calloc(
                (size_t)hull_vertex_count * 3, sizeof(float));
            if (!out_skel->hull_vertices) goto fail_skel;
            if (fread(out_skel->hull_vertices, sizeof(float) * 3,
                      hull_vertex_count, f) != hull_vertex_count)
                goto fail_skel;
        }
        out_skel->hull_vertex_count = hull_vertex_count;

        /* --- JNTS chunk: per-bone joint descriptors --- */
        out_skel->joints = (bone_joint_desc_t *)calloc(
            joint_count, sizeof(bone_joint_desc_t));
        if (!out_skel->joints) goto fail_skel;

        if (version == 2) {
            /* v2 JNTS: 28-byte records (scalar limit_min/max).
             * Read each record individually and convert. */
            for (uint32_t i = 0; i < joint_count; i++) {
                uint32_t jt = 0;
                float ax[3] = {0}, rl = 0, lmin = 0, lmax = 0;
                if (fread(&jt, 4, 1, f) != 1) goto fail_skel;
                if (fread(ax, 4, 3, f) != 3) goto fail_skel;
                if (fread(&rl, 4, 1, f) != 1) goto fail_skel;
                if (fread(&lmin, 4, 1, f) != 1) goto fail_skel;
                if (fread(&lmax, 4, 1, f) != 1) goto fail_skel;
                out_skel->joints[i].joint_type = jt;
                out_skel->joints[i].axis[0] = ax[0];
                out_skel->joints[i].axis[1] = ax[1];
                out_skel->joints[i].axis[2] = ax[2];
                out_skel->joints[i].rest_length = rl;
                out_skel->joints[i].limit_min[0] = lmin;
                out_skel->joints[i].limit_max[0] = lmax;
                out_skel->joints[i].limit_axes =
                    (lmin != 0.0f || lmax != 0.0f) ? 1u : 0u;
            }
        } else {
            /* v3+: 48-byte records (full bone_joint_desc_t). */
            if (fread(out_skel->joints, sizeof(bone_joint_desc_t),
                      joint_count, f) != joint_count)
                goto fail_skel;
        }
    }
    /* v1 files: colliders and joints remain NULL. */

    fclose(f);
    return true;

fail_skel:
    skeleton_def_destroy(out_skel);
fail:
    fclose(f);
    return false;
}
