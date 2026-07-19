/**
 * @file probe_file.h
 * @brief On-disk .probes format save/load (rpg-ft0g). Headless binary IO in the
 *        style of lm_sdf_file.c: a small header then raw positions and optional
 *        baked SH. Used to ship a level's manually-placed / prebaked probes.
 */
#ifndef FERRUM_PROBE_PROBE_FILE_H
#define FERRUM_PROBE_PROBE_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ferrum/probe/probe_set.h"

struct arena; /* ferrum/memory/arena.h */

/**
 * @brief Write a probe set to a .probes file.
 *
 * Serialises count, grid layout (origin/cell/dim), sh_coeffs, then the position
 * array and (when sh_coeffs>0 and sh!=NULL) the SH array. Host byte order.
 *
 * @return true on success; false on NULL/invalid set or any IO error.
 */
bool probe_set_save(const char *path, const probe_set_t *set);

/**
 * @brief Load a probe set from a .probes file into a caller arena.
 *
 * @p out->positions (and @p out->sh when present) are allocated from @p arena.
 *
 * @return true on success; false if the file is missing/short, has a bad magic,
 *         a corrupt count, or the arena is exhausted. Never crashes.
 */
bool probe_set_load(const char *path, struct arena *arena, probe_set_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROBE_PROBE_FILE_H */
