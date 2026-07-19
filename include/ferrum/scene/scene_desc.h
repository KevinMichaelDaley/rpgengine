/**
 * @file scene_desc.h
 * @brief Headless scene/level descriptor + loader (rpg-51nf).
 *
 * A parsed, GL-free enumeration of every asset class in a level: ordered mesh
 * objects (order == lightmap bake order), the material table, the baker's
 * chunked light-data references, and the probe-placement spec. Both the client
 * render-world builder and the server level loader consume this; the asset
 * streamer resolves its light-data prefixes to chunk ids + priorities.
 *
 * Ownership: the caller supplies an arena_t; all variable-length storage (the
 * object array) is allocated from it, so the descriptor is valid only for the
 * arena's lifetime. No malloc/free, no GL. Not thread-safe (single arena).
 */
#ifndef FERRUM_SCENE_SCENE_DESC_H
#define FERRUM_SCENE_SCENE_DESC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/scene/scene_desc_object.h"
#include "ferrum/scene/scene_desc_material.h"
#include "ferrum/scene/scene_desc_collider.h"
#include "ferrum/scene/scene_desc_lightdata.h"
#include "ferrum/scene/scene_desc_probes.h"

struct arena; /* forward decl: ferrum/memory/arena.h */

/** Max distinct materials in a level's material table. */
#define SCENE_DESC_MAX_MATERIALS 128u
/** Fixed capacity for a material name (incl. null terminator). */
#define SCENE_DESC_MAT_NAME_CAP 64u

/**
 * @brief A whole level's asset manifest.
 *
 * @c objects points into the caller's arena and holds @c object_count entries in
 * bake order. @c materials is a fixed table of @c material_count names that
 * object material indices reference.
 */
typedef struct scene_desc {
    char                    name[SCENE_DESC_OBJ_NAME_CAP];
    uint32_t                material_count;
    scene_desc_material_t   materials[SCENE_DESC_MAX_MATERIALS]; /**< full PBR defs. */
    uint32_t                object_count;
    scene_desc_object_t    *objects;    /**< arena-allocated [object_count], bake order. */
    uint32_t                collider_count;
    scene_desc_collider_t  *colliders;  /**< arena-allocated [collider_count] (may be NULL). */
    scene_desc_lightdata_t  lightdata;  /**< baked light-data refs (empty if omitted). */
    scene_desc_probes_t     probes;     /**< probe placement spec (defaults if omitted). */
} scene_desc_t;

/**
 * @brief Parse a descriptor from an in-memory JSON buffer.
 *
 * @param json   JSON text (need not be null-terminated; @p len is explicit).
 * @param len    length of @p json in bytes.
 * @param arena  arena for the descriptor's variable-length storage (and scratch).
 * @param out    zeroed then filled on success; left partially written on failure.
 * @return true on success; false on malformed JSON, non-object root, a missing
 *         required "objects" array, or arena exhaustion. Never crashes.
 *
 * Side effects: consumes space from @p arena. @p out->objects aliases the arena.
 */
bool scene_desc_parse(const char *json, size_t len, struct arena *arena,
                      scene_desc_t *out);

/**
 * @brief Load and parse a descriptor from a file path.
 *
 * Reads the whole file into @p arena, then calls scene_desc_parse().
 *
 * @param path   filesystem path to the .scene descriptor.
 * @param arena  arena for the file buffer + descriptor storage.
 * @param out    zeroed then filled on success.
 * @return true on success; false if the file is unreadable or parsing fails.
 */
bool scene_desc_load(const char *path, struct arena *arena, scene_desc_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_H */
