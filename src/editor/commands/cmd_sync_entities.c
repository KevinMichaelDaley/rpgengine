/**
 * @file cmd_sync_entities.c
 * @brief sync_entities command — delta-compressed entity sync.
 *
 * JSON args: {"since_version": V, "offset": N, "limit": N}
 *
 * Delta response (since_version > 0, tombstones haven't wrapped):
 *   {"version":V, "entities":[...changed...], "tombstones":[id,...], "full":false}
 *
 * Full response (since_version == 0 or tombstones wrapped):
 *   {"version":V, "entities":[...page...], "tombstones":[], "full":true,
 *    "total":N, "offset":O}
 *
 * Non-static functions: 1 (cmd_sync_entities).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_version.h"

#include <string.h>

/** Default and maximum entities per page for full sync. */
#define SYNC_DEFAULT_LIMIT 200
#define SYNC_MAX_LIMIT     500

/** Fields per entity object: id, name, type, pos, orient, scale. */
#define FIELDS_PER_ENTITY 6

/** Number of vec3 arrays per entity (pos, scale). */
#define VEC3_ARRAYS 2

/** @brief Look up type name from type ID. */
static const char *type_name_(uint32_t type_id) {
    uint32_t count = 0;
    const edit_entity_type_info_t *types = edit_entity_type_registry(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (types[i].type_id == type_id) return types[i].name;
    }
    return "unknown";
}

/**
 * @brief Build a single entity JSON object into pre-allocated arrays.
 *
 * @param ent       Entity to serialize.
 * @param eid       Entity ID.
 * @param keys      Output keys array (FIELDS_PER_ENTITY slots).
 * @param klens     Output key length array.
 * @param vals      Output values array.
 * @param v3        Vec3 items array (VEC3_ARRAYS * 3 slots).
 * @param qe        Quaternion items array (4 slots).
 */
static void build_entity_obj_(const edit_entity_t *ent, uint32_t eid,
                               const char **keys, uint32_t *klens,
                               json_value_t *vals, json_value_t *v3,
                               json_value_t *qe) {
    static const char *ent_key_strs[] = {
        "id", "name", "type", "pos", "orient", "scale"
    };
    static const uint32_t ent_key_lens[] = {2, 4, 4, 3, 6, 5};

    for (int k = 0; k < FIELDS_PER_ENTITY; k++) {
        keys[k]  = ent_key_strs[k];
        klens[k] = ent_key_lens[k];
    }

    vals[0].type   = JSON_NUMBER;
    vals[0].number = (double)eid;

    vals[1].type       = JSON_STRING;
    vals[1].string.ptr = ent->name[0] ? ent->name : "";
    vals[1].string.len = (uint32_t)strlen(vals[1].string.ptr);

    const char *tname = type_name_(ent->type);
    vals[2].type       = JSON_STRING;
    vals[2].string.ptr = tname;
    vals[2].string.len = (uint32_t)strlen(tname);

    /* pos → vals[3] */
    for (int c = 0; c < 3; c++) {
        v3[c].type   = JSON_NUMBER;
        v3[c].number = (double)ent->pos[c];
    }
    vals[3].type        = JSON_ARRAY;
    vals[3].array.items = &v3[0];
    vals[3].array.count = 3;

    /* orient → vals[4] (quaternion xyzw) */
    qe[0].type = JSON_NUMBER; qe[0].number = (double)ent->orientation.x;
    qe[1].type = JSON_NUMBER; qe[1].number = (double)ent->orientation.y;
    qe[2].type = JSON_NUMBER; qe[2].number = (double)ent->orientation.z;
    qe[3].type = JSON_NUMBER; qe[3].number = (double)ent->orientation.w;
    vals[4].type        = JSON_ARRAY;
    vals[4].array.items = qe;
    vals[4].array.count = 4;

    /* scale → vals[5] */
    for (int c = 0; c < 3; c++) {
        v3[3 + c].type   = JSON_NUMBER;
        v3[3 + c].number = (double)ent->scale[c];
    }
    vals[5].type        = JSON_ARRAY;
    vals[5].array.items = &v3[3];
    vals[5].array.count = 3;
}

/**
 * @brief Allocate arena space for entity page + wrapper.
 *
 * @param arena       JSON arena.
 * @param page_count  Number of entities in the page.
 * @param wrap_count  Number of wrapper keys (5 for full, 4 for delta).
 * @param ts_count    Number of tombstone entries.
 * @param[out] items       Entity items array.
 * @param[out] all_keys    Per-entity keys.
 * @param[out] all_klens   Per-entity key lengths.
 * @param[out] all_vals    Per-entity values.
 * @param[out] vec3_items  Vec3 sub-arrays.
 * @param[out] quat_items  Quaternion sub-arrays.
 * @param[out] ts_items    Tombstone ID items.
 * @param[out] wrap_keys   Wrapper keys.
 * @param[out] wrap_klens  Wrapper key lengths.
 * @param[out] wrap_vals   Wrapper values.
 * @return true if arena has enough space.
 */
static bool alloc_arena_(json_arena_t *arena, uint32_t page_count,
                          uint32_t wrap_count, uint32_t ts_count,
                          json_value_t **items, const char ***all_keys,
                          uint32_t **all_klens, json_value_t **all_vals,
                          json_value_t **vec3_items, json_value_t **quat_items,
                          json_value_t **ts_items,
                          const char ***wrap_keys, uint32_t **wrap_klens,
                          json_value_t **wrap_vals) {
    /* Align helper. */
    #define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)

    size_t items_sz    = ALIGN8(page_count * sizeof(json_value_t));
    size_t keys_sz     = ALIGN8(page_count * FIELDS_PER_ENTITY * sizeof(const char *));
    size_t klens_sz    = ALIGN8(page_count * FIELDS_PER_ENTITY * sizeof(uint32_t));
    size_t vals_sz     = ALIGN8(page_count * FIELDS_PER_ENTITY * sizeof(json_value_t));
    size_t vec3_sz     = ALIGN8(page_count * VEC3_ARRAYS * 3 * sizeof(json_value_t));
    size_t quat_sz     = ALIGN8(page_count * 4 * sizeof(json_value_t));
    size_t ts_sz       = ALIGN8(ts_count * sizeof(json_value_t));
    size_t wk_sz       = ALIGN8(wrap_count * sizeof(const char *));
    size_t wkl_sz      = ALIGN8(wrap_count * sizeof(uint32_t));
    size_t wv_sz       = ALIGN8(wrap_count * sizeof(json_value_t));

    size_t total = items_sz + keys_sz + klens_sz + vals_sz + vec3_sz
                 + quat_sz + ts_sz + wk_sz + wkl_sz + wv_sz;

    if (arena->used + total > arena->cap) return false;

    *items      = (json_value_t *)(arena->buf + arena->used); arena->used += items_sz;
    *all_keys   = (const char **)(arena->buf + arena->used);  arena->used += keys_sz;
    *all_klens  = (uint32_t *)(arena->buf + arena->used);     arena->used += klens_sz;
    *all_vals   = (json_value_t *)(arena->buf + arena->used); arena->used += vals_sz;
    *vec3_items = (json_value_t *)(arena->buf + arena->used); arena->used += vec3_sz;
    *quat_items = (json_value_t *)(arena->buf + arena->used); arena->used += quat_sz;
    *ts_items   = (json_value_t *)(arena->buf + arena->used); arena->used += ts_sz;
    *wrap_keys  = (const char **)(arena->buf + arena->used);  arena->used += wk_sz;
    *wrap_klens = (uint32_t *)(arena->buf + arena->used);     arena->used += wkl_sz;
    *wrap_vals  = (json_value_t *)(arena->buf + arena->used); arena->used += wv_sz;

    #undef ALIGN8
    return true;
}

/**
 * @brief Build the full sync response (paginated, all active entities).
 */
static bool build_full_response_(edit_cmd_ctx_t *ctx,
                                  json_value_t *result, json_arena_t *arena,
                                  uint32_t req_offset, uint32_t req_limit) {
    const edit_entity_store_t *store = ctx->entities;
    const edit_version_state_t *vs = ctx->version;

    /* Count total active entities. */
    uint32_t total_count = 0;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (store->entities[i].active) total_count++;
    }

    /* Determine page. */
    uint32_t page_count = 0;
    if (req_offset < total_count) {
        page_count = total_count - req_offset;
        if (page_count > req_limit) page_count = req_limit;
    }

    /* Wrapper: version, entities, tombstones, full, total, offset = 6 keys. */
    json_value_t *items, *all_vals, *vec3_items, *quat_items, *ts_items;
    const char **all_keys, **wrap_keys;
    uint32_t *all_klens, *wrap_klens;
    json_value_t *wrap_vals;

    if (!alloc_arena_(arena, page_count, 6, 0,
                       &items, &all_keys, &all_klens, &all_vals,
                       &vec3_items, &quat_items, &ts_items,
                       &wrap_keys, &wrap_klens, &wrap_vals)) {
        return false;
    }

    /* Build entity objects. */
    uint32_t idx = 0, match_idx = 0;
    for (uint32_t i = 0; i < store->capacity && idx < page_count; i++) {
        if (!store->entities[i].active) continue;
        if (match_idx < req_offset) { match_idx++; continue; }
        match_idx++;

        build_entity_obj_(&store->entities[i], i,
                           &all_keys[idx * FIELDS_PER_ENTITY],
                           &all_klens[idx * FIELDS_PER_ENTITY],
                           &all_vals[idx * FIELDS_PER_ENTITY],
                           &vec3_items[idx * VEC3_ARRAYS * 3],
                           &quat_items[idx * 4]);

        items[idx].type         = JSON_OBJECT;
        items[idx].object.keys  = &all_keys[idx * FIELDS_PER_ENTITY];
        items[idx].object.key_lens = &all_klens[idx * FIELDS_PER_ENTITY];
        items[idx].object.vals  = &all_vals[idx * FIELDS_PER_ENTITY];
        items[idx].object.count = FIELDS_PER_ENTITY;
        idx++;
    }

    /* Build wrapper: {version, entities, tombstones, full, total, offset}. */
    wrap_keys[0] = "version";    wrap_klens[0] = 7;
    wrap_keys[1] = "entities";   wrap_klens[1] = 8;
    wrap_keys[2] = "tombstones"; wrap_klens[2] = 10;
    wrap_keys[3] = "full";       wrap_klens[3] = 4;
    wrap_keys[4] = "total";      wrap_klens[4] = 5;
    wrap_keys[5] = "offset";     wrap_klens[5] = 6;

    wrap_vals[0].type   = JSON_NUMBER;
    wrap_vals[0].number = (double)(vs ? vs->version : 0);

    wrap_vals[1].type        = JSON_ARRAY;
    wrap_vals[1].array.items = items;
    wrap_vals[1].array.count = idx;

    /* Empty tombstones array for full sync. */
    wrap_vals[2].type        = JSON_ARRAY;
    wrap_vals[2].array.items = NULL;
    wrap_vals[2].array.count = 0;

    wrap_vals[3].type    = JSON_BOOL;
    wrap_vals[3].boolean = true;

    wrap_vals[4].type   = JSON_NUMBER;
    wrap_vals[4].number = (double)total_count;

    wrap_vals[5].type   = JSON_NUMBER;
    wrap_vals[5].number = (double)req_offset;

    result->type            = JSON_OBJECT;
    result->object.keys     = wrap_keys;
    result->object.key_lens = wrap_klens;
    result->object.vals     = wrap_vals;
    result->object.count    = 6;
    return true;
}

/**
 * @brief Count tombstones since a version.
 */
static uint32_t count_tombstones_since_(const edit_version_state_t *vs,
                                         uint64_t since_version) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < vs->tombstone_count; i++) {
        /* Walk ring from oldest to newest. */
        uint32_t ring_idx;
        if (vs->tombstone_count < vs->tombstone_capacity) {
            ring_idx = i;
        } else {
            ring_idx = (vs->tombstone_head + i) % vs->tombstone_capacity;
        }
        if (vs->tombstones[ring_idx].version > since_version) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Build the delta sync response (only changed entities + tombstones).
 */
static bool build_delta_response_(edit_cmd_ctx_t *ctx,
                                   json_value_t *result, json_arena_t *arena,
                                   uint64_t since_version) {
    const edit_entity_store_t *store = ctx->entities;
    const edit_version_state_t *vs = ctx->version;

    /* Count changed entities. */
    uint32_t changed_count = edit_version_count_changed(vs, since_version);

    /* Count tombstones since version. */
    uint32_t ts_count = count_tombstones_since_(vs, since_version);

    /* Wrapper: version, entities, tombstones, full = 4 keys. */
    json_value_t *items, *all_vals, *vec3_items, *quat_items, *ts_items;
    const char **all_keys, **wrap_keys;
    uint32_t *all_klens, *wrap_klens;
    json_value_t *wrap_vals;

    if (!alloc_arena_(arena, changed_count, 4, ts_count,
                       &items, &all_keys, &all_klens, &all_vals,
                       &vec3_items, &quat_items, &ts_items,
                       &wrap_keys, &wrap_klens, &wrap_vals)) {
        /* Arena too small for delta — fall back to full. */
        return build_full_response_(ctx, result, arena, 0, SYNC_DEFAULT_LIMIT);
    }

    /* Build changed entity objects. */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < vs->entity_capacity && idx < changed_count; i++) {
        if (vs->entity_version[i] <= since_version) continue;

        /* Entity must still be active to include in delta. */
        const edit_entity_t *ent = edit_entity_store_get(store, i);
        if (!ent) continue;

        build_entity_obj_(ent, i,
                           &all_keys[idx * FIELDS_PER_ENTITY],
                           &all_klens[idx * FIELDS_PER_ENTITY],
                           &all_vals[idx * FIELDS_PER_ENTITY],
                           &vec3_items[idx * VEC3_ARRAYS * 3],
                           &quat_items[idx * 4]);

        items[idx].type         = JSON_OBJECT;
        items[idx].object.keys  = &all_keys[idx * FIELDS_PER_ENTITY];
        items[idx].object.key_lens = &all_klens[idx * FIELDS_PER_ENTITY];
        items[idx].object.vals  = &all_vals[idx * FIELDS_PER_ENTITY];
        items[idx].object.count = FIELDS_PER_ENTITY;
        idx++;
    }

    /* Build tombstone ID array. */
    uint32_t ts_idx = 0;
    for (uint32_t i = 0; i < vs->tombstone_count && ts_idx < ts_count; i++) {
        uint32_t ring_idx;
        if (vs->tombstone_count < vs->tombstone_capacity) {
            ring_idx = i;
        } else {
            ring_idx = (vs->tombstone_head + i) % vs->tombstone_capacity;
        }
        if (vs->tombstones[ring_idx].version > since_version) {
            ts_items[ts_idx].type   = JSON_NUMBER;
            ts_items[ts_idx].number = (double)vs->tombstones[ring_idx].entity_id;
            ts_idx++;
        }
    }

    /* Build wrapper: {version, entities, tombstones, full}. */
    wrap_keys[0] = "version";    wrap_klens[0] = 7;
    wrap_keys[1] = "entities";   wrap_klens[1] = 8;
    wrap_keys[2] = "tombstones"; wrap_klens[2] = 10;
    wrap_keys[3] = "full";       wrap_klens[3] = 4;

    wrap_vals[0].type   = JSON_NUMBER;
    wrap_vals[0].number = (double)vs->version;

    wrap_vals[1].type        = JSON_ARRAY;
    wrap_vals[1].array.items = items;
    wrap_vals[1].array.count = idx;

    wrap_vals[2].type        = JSON_ARRAY;
    wrap_vals[2].array.items = ts_items;
    wrap_vals[2].array.count = ts_idx;

    wrap_vals[3].type    = JSON_BOOL;
    wrap_vals[3].boolean = false;

    result->type            = JSON_OBJECT;
    result->object.keys     = wrap_keys;
    result->object.key_lens = wrap_klens;
    result->object.vals     = wrap_vals;
    result->object.count    = 4;
    return true;
}

bool cmd_sync_entities(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    /* Extract since_version (default 0 → full sync). */
    uint64_t since_version = 0;
    uint32_t req_offset = 0;
    uint32_t req_limit = SYNC_DEFAULT_LIMIT;

    if (args) {
        const json_value_t *sv = json_object_get(args, "since_version");
        if (sv && sv->type == JSON_NUMBER && sv->number >= 0) {
            since_version = (uint64_t)sv->number;
        }
        const json_value_t *ov = json_object_get(args, "offset");
        if (ov && ov->type == JSON_NUMBER && ov->number >= 0) {
            req_offset = (uint32_t)ov->number;
        }
        const json_value_t *lv = json_object_get(args, "limit");
        if (lv && lv->type == JSON_NUMBER && lv->number > 0) {
            req_limit = (uint32_t)lv->number;
            if (req_limit > SYNC_MAX_LIMIT) req_limit = SYNC_MAX_LIMIT;
        }
    }

    /* Decide: full or delta? */
    bool need_full = (since_version == 0);
    if (!need_full && ctx->version) {
        need_full = edit_version_needs_full_resync(ctx->version, since_version);
    }
    if (!need_full && !ctx->version) {
        need_full = true; /* No version tracking → always full. */
    }

    if (need_full) {
        return build_full_response_(ctx, result, arena, req_offset, req_limit);
    }

    return build_delta_response_(ctx, result, arena, since_version);
}
