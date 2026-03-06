/**
 * @file fskel_format.h
 * @brief .fskel binary format constants and chunk type IDs.
 *
 * Wire format (little-endian):
 *   [4]  magic 'FSKL' (0x4C4B5346)
 *   [4]  version (3)
 *   [4]  joint_count
 *   [4]  max_constraints_per_joint
 *   [4]  ibm_count
 *   --- SKEL chunk ---
 *   [joint_count × 64]  joint_names (null-terminated, padded to 64 bytes)
 *   [joint_count × 4]   parent_indices
 *   [joint_count × 64]  rest_local (mat4_t, 16 floats)
 *   [joint_count × 64]  rest_world (mat4_t, 16 floats)
 *   --- CNST chunk ---
 *   [joint_count × 4]   constraint_counts
 *   [joint_count × max_constraints × sizeof(constraint_def_t)]  constraints
 *   --- IBM chunk ---
 *   [ibm_count × 64]    inverse bind matrices (mat4_t)
 *   --- COLL chunk (v2+) ---
 *   [4]  hull_vertex_count (total convex hull vertices)
 *   [joint_count × sizeof(bone_collider_desc_t)]  collider descriptors
 *   [hull_vertex_count × 12]  hull vertex data (float x,y,z triples)
 *   --- JNTS chunk (v2+) ---
 *   v2: [joint_count × 28]  joint descriptors (legacy)
 *   v3: [joint_count × 48]  joint descriptors (per-axis limits)
 *
 * Public types: 0
 * Public functions: 0
 */

#ifndef FERRUM_ANIMATION_FSKEL_FORMAT_H
#define FERRUM_ANIMATION_FSKEL_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Magic bytes: 'F','S','K','L' = 0x4C4B5346 little-endian. */
#define FSKEL_MAGIC   0x4C4B5346u

/** @brief Current format version.
 *  v1: SKEL + CNST + IBM
 *  v2: + COLL + JNTS (28-byte bone_joint_desc)
 *  v3: + expanded JNTS (48-byte bone_joint_desc with per-axis limits)
 */
#define FSKEL_VERSION 3u

/** @brief Header size in bytes (5 × 4 = 20). */
#define FSKEL_HEADER_SIZE 20u

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_FSKEL_FORMAT_H */
