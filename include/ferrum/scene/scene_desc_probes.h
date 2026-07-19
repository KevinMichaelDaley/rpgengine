/**
 * @file scene_desc_probes.h
 * @brief Descriptor probe-placement spec (rpg-51nf / rpg-ft0g).
 *
 * The probe grid stays AUTO-generated from a resolution by default; this spec
 * only carries the level's optional overrides: a base horizontal/vertical
 * spacing, optional manually-placed probes (a sidecar file), and AABB importance
 * boxes that raise probe density / streaming priority inside a region. Pure data.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_PROBES_H
#define FERRUM_SCENE_SCENE_DESC_PROBES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/scene/scene_desc_object.h" /* SCENE_DESC_PATH_CAP */

/** Max AABB importance regions per level. */
#define SCENE_DESC_MAX_IMPORTANCE 16u

/**
 * @brief An axis-aligned region that biases probe density and stream priority.
 */
typedef struct scene_desc_importance_box {
    float min[3];         /**< region min corner (world). */
    float max[3];         /**< region max corner (world). */
    float density_mult;   /**< probe-density multiplier inside the box (1 = no change). */
    float priority_bias;  /**< streaming-priority bump inside the box (0 = none). */
} scene_desc_importance_box_t;

/**
 * @brief Probe placement spec for a level.
 *
 * @c spacing / @c vspacing <= 0 mean "use the engine default". @c has_manual
 * gates @c manual_path (a sidecar probe file). Strings are null-terminated.
 */
typedef struct scene_desc_probes {
    float    spacing;      /**< horizontal auto-grid spacing, m (<=0 = engine default). */
    float    vspacing;     /**< vertical auto-grid spacing, m (<=0 = engine default). */
    bool     has_manual;   /**< true if manual_path names a manual probe file. */
    char     manual_path[SCENE_DESC_PATH_CAP]; /**< manual probe positions sidecar. */
    uint32_t box_count;    /**< entries used in boxes[]. */
    scene_desc_importance_box_t boxes[SCENE_DESC_MAX_IMPORTANCE]; /**< importance regions. */
} scene_desc_probes_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_PROBES_H */
