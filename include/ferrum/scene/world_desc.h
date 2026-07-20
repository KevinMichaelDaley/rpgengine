/**
 * @file world_desc.h
 * @brief Headless world descriptor: a world is a set of ZONES (rpg-da8c / rpg-yrnu).
 *
 * A world/level is not one scene but many zones -- large spatial regions, each an
 * IRREGULAR, variable-size world box (NOT a regular grid) that may span several
 * light-data chunks. Each zone names its own scene descriptor and, optionally, its
 * own render config, so the config + scene subsystems load PER ZONE: as the player
 * moves, the active zone (world_desc_zone_at) selects which scene + render_config
 * to (stream and) build. This descriptor is the coarse map the future zone streamer
 * (rpg-yrnu) pages over; today a single-zone world is the trivial case.
 *
 * Ownership: the zones array is arena-allocated by the parse call; the world_desc_t
 * borrows it (valid while the arena lives). Fixed-size string fields are inline.
 */
#ifndef FERRUM_SCENE_WORLD_DESC_H
#define FERRUM_SCENE_WORLD_DESC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct arena;

#define WORLD_DESC_NAME_CAP 64
#define WORLD_DESC_PATH_CAP 256

/** One zone: an irregular world box + the scene (and optional config) to load. */
typedef struct world_zone {
    char  name[WORLD_DESC_NAME_CAP];
    float box_min[3];   /**< zone world-box min corner (irregular; may span chunks). */
    float box_max[3];   /**< zone world-box max corner. */
    char  scene[WORLD_DESC_PATH_CAP];         /**< scene descriptor path (rel. to world dir). */
    char  render_config[WORLD_DESC_PATH_CAP]; /**< per-zone render config path ("" => world default). */
} world_zone_t;

/** A world: a named set of zones + a world-level default render config. */
typedef struct world_desc {
    char          name[WORLD_DESC_NAME_CAP];
    world_zone_t *zones;                            /**< arena-allocated [zone_count]. */
    uint32_t      zone_count;
    char          default_render_config[WORLD_DESC_PATH_CAP]; /**< "" => engine default. */
} world_desc_t;

/**
 * @brief Parse a world JSON object into @p out (zones arena-allocated). Requires a
 *        non-empty "zones" array; each zone needs "min","max","scene". @return
 *        false on NULL args, non-object root, missing/empty zones, or OOM.
 */
bool world_desc_parse(const char *json, size_t len, struct arena *arena,
                      world_desc_t *out);

/** @brief Load + parse a world JSON file. @p arena backs the buffer + zones. */
bool world_desc_load(const char *path, struct arena *arena, world_desc_t *out);

/**
 * @brief Index of the first zone whose box contains @p p, or -1 if none. Zones are
 *        irregular and MAY overlap; the first match (declaration order) wins.
 */
int world_desc_zone_at(const world_desc_t *w, const float p[3]);

/**
 * @brief The effective render-config path for zone @p i: its own if set, else the
 *        world default (which may be "" => engine default). NULL if @p i is out of
 *        range. The returned pointer aliases the descriptor (valid while it lives).
 */
const char *world_desc_zone_config(const world_desc_t *w, uint32_t i);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_WORLD_DESC_H */
