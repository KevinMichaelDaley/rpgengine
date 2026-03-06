/**
 * @file fskel_write.c
 * @brief .fskel format writer.
 *
 * Writes skeleton hierarchy, constraints, and IBMs to a binary file.
 * Format is little-endian, flat arrays, no chunk headers (fixed layout).
 *
 * Non-static functions: 1 (fskel_write)
 */

#include "ferrum/animation/fskel_loader.h"
#include "ferrum/animation/fskel_format.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <string.h>

/**
 * @brief Write a 4-byte little-endian uint32.
 */
static bool write_u32(FILE *f, uint32_t v) {
    return fwrite(&v, 4, 1, f) == 1;
}

bool fskel_write(const char *path,
                 const skeleton_def_t *skel,
                 const mat4_t *ibms,
                 uint32_t ibm_count) {
    if (!path || !skel) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    /* Header: magic, version, joint_count, max_constraints, ibm_count. */
    if (!write_u32(f, FSKEL_MAGIC)) goto fail;
    if (!write_u32(f, FSKEL_VERSION)) goto fail;
    if (!write_u32(f, skel->joint_count)) goto fail;
    if (!write_u32(f, skel->max_constraints_per_joint)) goto fail;
    if (!write_u32(f, ibm_count)) goto fail;

    uint32_t n = skel->joint_count;

    /* Joint names. */
    for (uint32_t i = 0; i < n; i++) {
        if (fwrite(skel->joint_names[i], SKELETON_JOINT_NAME_MAX, 1, f) != 1)
            goto fail;
    }

    /* Parent indices. */
    if (n > 0 && fwrite(skel->parent_indices, sizeof(uint32_t), n, f) != n)
        goto fail;

    /* Rest local transforms. */
    if (n > 0 && fwrite(skel->rest_local, sizeof(mat4_t), n, f) != n)
        goto fail;

    /* Rest world transforms. */
    if (n > 0 && fwrite(skel->rest_world, sizeof(mat4_t), n, f) != n)
        goto fail;

    /* Constraint counts. */
    if (skel->constraint_counts) {
        if (n > 0 && fwrite(skel->constraint_counts, sizeof(uint32_t), n, f) != n)
            goto fail;
    } else {
        /* Write zeros. */
        for (uint32_t i = 0; i < n; i++) {
            uint32_t zero = 0;
            if (!write_u32(f, zero)) goto fail;
        }
    }

    /* Constraints (flat: joint_count × max_constraints × sizeof(constraint_def_t)). */
    uint32_t max_c = skel->max_constraints_per_joint;
    if (max_c > 0 && skel->constraints) {
        size_t total = (size_t)n * max_c;
        if (fwrite(skel->constraints, sizeof(constraint_def_t), total, f) != total)
            goto fail;
    }

    /* Inverse bind matrices. */
    if (ibm_count > 0 && ibms) {
        if (fwrite(ibms, sizeof(mat4_t), ibm_count, f) != ibm_count)
            goto fail;
    }

    fclose(f);
    return true;

fail:
    fclose(f);
    return false;
}
