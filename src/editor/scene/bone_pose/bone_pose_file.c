/**
 * @file bone_pose_file.c
 * @brief .bpose file read/write for per-entity bone pose overrides.
 *
 * Non-static functions (2 / 4 limit):
 *   1. bone_pose_file_write
 *   2. bone_pose_file_read
 */

#include "ferrum/editor/scene/bone_pose/bone_pose_file.h"
#include "ferrum/editor/scene/bone_pose/bone_pose_store.h"

#include <stdio.h>
#include <string.h>

bool bone_pose_file_write(const char *path,
                           const bone_pose_block_t *block) {
    if (!path || !block) return false;
    if (block->bone_count == 0) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    bone_pose_file_header_t header;
    header.version = BONE_POSE_FILE_VERSION;
    header.bone_count = block->bone_count;

    bool ok = true;
    if (fwrite(&header, sizeof(header), 1, f) != 1) ok = false;
    if (ok && fwrite(block->rest_local,
                      sizeof(mat4_t), block->bone_count, f)
            != block->bone_count) {
        ok = false;
    }

    fclose(f);
    return ok;
}

bool bone_pose_file_read(const char *path,
                          bone_pose_block_t *block) {
    if (!path || !block) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    bone_pose_file_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        return false;
    }

    if (header.version != BONE_POSE_FILE_VERSION) {
        fclose(f);
        return false;
    }

    if (header.bone_count == 0 || header.bone_count > BONE_POSE_MAX_BONES) {
        fclose(f);
        return false;
    }

    /* Read rest_local matrices. */
    block->bone_count = header.bone_count;
    if (fread(block->rest_local, sizeof(mat4_t), header.bone_count, f)
        != header.bone_count) {
        fclose(f);
        return false;
    }

    fclose(f);
    block->active = true;
    return true;
}
