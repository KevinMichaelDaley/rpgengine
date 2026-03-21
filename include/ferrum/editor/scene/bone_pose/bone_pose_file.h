/**
 * @file bone_pose_file.h
 * @brief .bpose file read/write for per-entity bone pose overrides.
 *
 * Format: [u32 version][u32 bone_count][mat4 rest_local[bone_count]]
 * Version 1 stores only rest_local; rest_world is recomputed on load
 * from the skeleton hierarchy.
 *
 * Public types (1 / 2-type rule):
 *   bone_pose_file_header_t
 */
#ifndef FERRUM_EDITOR_SCENE_BONE_POSE_FILE_H
#define FERRUM_EDITOR_SCENE_BONE_POSE_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct bone_pose_block;

/** Current .bpose file format version. */
#define BONE_POSE_FILE_VERSION 1u

/**
 * @brief .bpose file header.
 */
typedef struct bone_pose_file_header {
    uint32_t version;     /**< File format version. */
    uint32_t bone_count;  /**< Number of bones stored. */
} bone_pose_file_header_t;

/**
 * @brief Write a bone pose block to a .bpose file.
 *
 * Writes: header + rest_local matrices.
 *
 * @param path   File path to write (non-NULL).
 * @param block  Bone pose block to write (non-NULL).
 * @return true on success, false on I/O error.
 */
bool bone_pose_file_write(const char *path,
                           const struct bone_pose_block *block);

/**
 * @brief Read a .bpose file into a bone pose block.
 *
 * Reads rest_local matrices. The caller must recompute rest_world
 * from the skeleton hierarchy after loading.
 *
 * @param path   File path to read (non-NULL).
 * @param block  Bone pose block to fill (non-NULL, must have
 *               bone_count set to the expected count).
 * @return true on success, false on I/O or format error.
 */
bool bone_pose_file_read(const char *path,
                          struct bone_pose_block *block);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_BONE_POSE_FILE_H */
