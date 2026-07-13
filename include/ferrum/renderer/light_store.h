#ifndef FERRUM_RENDERER_LIGHT_STORE_H
#define FERRUM_RENDERER_LIGHT_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/light.h"

/** @file
 * @brief A scene light store the pipeline and editor consume.
 *
 * A flat, caller-backed array of @ref render_light (no internal allocation:
 * callers provide the backing storage + capacity). @ref render_light_store_pack
 * flattens the realtime punctual lights into the parallel arrays the PBR shader
 * expects (type/pos/dir/colour/range/cone), applying colour*intensity and
 * skipping baked-only and area lights, ready to upload with glUniform*fv.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** A borrowed, fixed-capacity array of scene lights. */
typedef struct render_light_store {
    render_light_t *lights;  /**< caller-owned backing array. */
    uint32_t        count;   /**< number of lights currently held. */
    uint32_t        capacity;/**< backing array capacity. */
} render_light_store_t;

/**
 * @brief Initialise a store over caller-provided backing storage.
 */
void render_light_store_init(render_light_store_t *store,
                             render_light_t *backing, uint32_t capacity);

/**
 * @brief Append a light. Returns false if the store is full.
 */
bool render_light_add(render_light_store_t *store, const render_light_t *light);

/**
 * @brief Remove all lights (capacity/backing unchanged).
 */
void render_light_store_clear(render_light_store_t *store);

/**
 * @brief Flatten the store's REALTIME punctual lights (point/dir/spot) into the
 *        PBR shader's parallel arrays (colour premultiplied by intensity).
 * @param store     Source store.
 * @param out_type  int[max]  light type (0/1/2).
 * @param out_pos   float[max*3] world position.
 * @param out_dir   float[max*3] forward axis.
 * @param out_color float[max*3] colour * intensity.
 * @param out_range float[max]  range.
 * @param out_ci    float[max]  spot inner cone cosine.
 * @param out_co    float[max]  spot outer cone cosine.
 * @param max       Output capacity (in lights).
 * @return Number of lights packed (<= max).
 */
uint32_t render_light_store_pack(const render_light_store_t *store,
                                 int32_t *out_type, float *out_pos,
                                 float *out_dir, float *out_color,
                                 float *out_range, float *out_ci, float *out_co,
                                 uint32_t max);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_LIGHT_STORE_H */
