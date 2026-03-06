/**
 * @file fskel_loader.h
 * @brief .fskel format write and load API.
 *
 * Public types: 0
 * Public functions: 2 (fskel_write, fskel_load)
 */

#ifndef FERRUM_ANIMATION_FSKEL_LOADER_H
#define FERRUM_ANIMATION_FSKEL_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write a skeleton + IBMs to a .fskel file.
 *
 * @param path      Output file path (non-NULL).
 * @param skel      Skeleton definition (non-NULL).
 * @param ibms      Inverse bind matrices (may be NULL if ibm_count == 0).
 * @param ibm_count Number of IBMs.
 * @return true on success, false on failure.
 *
 * @par Ownership: caller owns all inputs.
 * @par Side effects: creates/overwrites file at path.
 */
bool fskel_write(const char *path,
                 const skeleton_def_t *skel,
                 const mat4_t *ibms,
                 uint32_t ibm_count);

/**
 * @brief Load a skeleton + IBMs from a .fskel file.
 *
 * On success, allocates and populates out_skel via skeleton_def_init().
 * Caller owns the skeleton and must call skeleton_def_destroy().
 * If out_ibms is non-NULL, allocates an IBM array (caller frees with free()).
 *
 * @param path           Input file path (non-NULL).
 * @param out_skel       Output skeleton (non-NULL).
 * @param out_ibms       Output IBM array (may be NULL to skip).
 * @param out_ibm_count  Output IBM count (may be NULL to skip).
 * @return true on success, false on failure (bad format, I/O error, etc.).
 *
 * @par Ownership: caller owns out_skel and *out_ibms.
 * @par Side effects: allocates heap memory.
 */
bool fskel_load(const char *path,
                skeleton_def_t *out_skel,
                mat4_t **out_ibms,
                uint32_t *out_ibm_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_FSKEL_LOADER_H */
