/**
 * @file cmd_clone_id.c
 * @brief Clone by ID — duplicates a specific entity.
 *
 * JSON args: {"entity_id": N_or_"name", "offset": [dx,dy,dz]}
 * Offset is optional (defaults to [0,0,0]).
 *
 * Returns a delta sync response containing the cloned entity:
 *   {"version":V, "entities":[{...}], "tombstones":[], "full":false}
 * The client processes this as a sync response, so the new entity
 * appears immediately without a separate refresh round-trip.
 *
 * Non-static functions: 1 (cmd_clone_id).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity_version.h"

#include <string.h>

/** Fields per entity object: id, name, type, pos, orient, scale. */
#define FIELDS_PER_ENTITY 6

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
 * @brief Build a delta sync result containing a single entity.
 *
 * Result format: {"version":V, "entities":[{entity}], "tombstones":[], "full":false}
 */
static bool build_sync_result_(const edit_entity_t *ent, uint32_t eid,
                                uint64_t version,
                                json_value_t *result, json_arena_t *arena) {
    /* Alignment helper. */
    #define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)

    /* Need space for:
     * - 1 entity item (json_value_t)
     * - 6 keys (const char *), 6 key_lens (uint32_t), 6 vals (json_value_t)
     * - 2*3 vec3 items + 4 quat items (json_value_t)
     * - 4 wrapper keys, key_lens, vals
     * - 0 tombstone items (empty array)
     */
    size_t item_sz   = ALIGN8(sizeof(json_value_t));
    size_t keys_sz   = ALIGN8(FIELDS_PER_ENTITY * sizeof(const char *));
    size_t klens_sz  = ALIGN8(FIELDS_PER_ENTITY * sizeof(uint32_t));
    size_t vals_sz   = ALIGN8(FIELDS_PER_ENTITY * sizeof(json_value_t));
    size_t vec3_sz   = ALIGN8(2 * 3 * sizeof(json_value_t));
    size_t quat_sz   = ALIGN8(4 * sizeof(json_value_t));
    size_t wk_sz     = ALIGN8(4 * sizeof(const char *));
    size_t wkl_sz    = ALIGN8(4 * sizeof(uint32_t));
    size_t wv_sz     = ALIGN8(4 * sizeof(json_value_t));
    size_t total = item_sz + keys_sz + klens_sz + vals_sz + vec3_sz
                 + quat_sz + wk_sz + wkl_sz + wv_sz;

    if (arena->used + total > arena->cap) return false;

    json_value_t *item = (json_value_t *)(arena->buf + arena->used);
    arena->used += item_sz;
    const char **ekeys = (const char **)(arena->buf + arena->used);
    arena->used += keys_sz;
    uint32_t *eklens = (uint32_t *)(arena->buf + arena->used);
    arena->used += klens_sz;
    json_value_t *evals = (json_value_t *)(arena->buf + arena->used);
    arena->used += vals_sz;
    json_value_t *v3 = (json_value_t *)(arena->buf + arena->used);
    arena->used += vec3_sz;
    json_value_t *qe = (json_value_t *)(arena->buf + arena->used);
    arena->used += quat_sz;
    const char **wkeys = (const char **)(arena->buf + arena->used);
    arena->used += wk_sz;
    uint32_t *wklens = (uint32_t *)(arena->buf + arena->used);
    arena->used += wkl_sz;
    json_value_t *wvals = (json_value_t *)(arena->buf + arena->used);
    arena->used += wv_sz;

    #undef ALIGN8

    /* Entity object fields. */
    static const char *field_names[] = {
        "id", "name", "type", "pos", "orient", "scale"
    };
    static const uint32_t field_lens[] = {2, 4, 4, 3, 6, 5};
    for (int k = 0; k < FIELDS_PER_ENTITY; k++) {
        ekeys[k]  = field_names[k];
        eklens[k] = field_lens[k];
    }

    evals[0].type   = JSON_NUMBER;
    evals[0].number = (double)eid;

    evals[1].type       = JSON_STRING;
    evals[1].string.ptr = ent->name[0] ? ent->name : "";
    evals[1].string.len = (uint32_t)strlen(evals[1].string.ptr);

    const char *tname = type_name_(ent->type);
    evals[2].type       = JSON_STRING;
    evals[2].string.ptr = tname;
    evals[2].string.len = (uint32_t)strlen(tname);

    /* pos */
    for (int c = 0; c < 3; c++) {
        v3[c].type   = JSON_NUMBER;
        v3[c].number = (double)ent->pos[c];
    }
    evals[3].type        = JSON_ARRAY;
    evals[3].array.items = &v3[0];
    evals[3].array.count = 3;

    /* orient (quaternion xyzw) */
    qe[0].type = JSON_NUMBER; qe[0].number = (double)ent->orientation.x;
    qe[1].type = JSON_NUMBER; qe[1].number = (double)ent->orientation.y;
    qe[2].type = JSON_NUMBER; qe[2].number = (double)ent->orientation.z;
    qe[3].type = JSON_NUMBER; qe[3].number = (double)ent->orientation.w;
    evals[4].type        = JSON_ARRAY;
    evals[4].array.items = qe;
    evals[4].array.count = 4;

    /* scale */
    for (int c = 0; c < 3; c++) {
        v3[3 + c].type   = JSON_NUMBER;
        v3[3 + c].number = (double)ent->scale[c];
    }
    evals[5].type        = JSON_ARRAY;
    evals[5].array.items = &v3[3];
    evals[5].array.count = 3;

    /* Entity item. */
    item->type            = JSON_OBJECT;
    item->object.keys     = ekeys;
    item->object.key_lens = eklens;
    item->object.vals     = evals;
    item->object.count    = FIELDS_PER_ENTITY;

    /* Wrapper: {version, entities, tombstones, full}. */
    wkeys[0] = "version";    wklens[0] = 7;
    wkeys[1] = "entities";   wklens[1] = 8;
    wkeys[2] = "tombstones"; wklens[2] = 10;
    wkeys[3] = "full";       wklens[3] = 4;

    wvals[0].type   = JSON_NUMBER;
    wvals[0].number = (double)version;

    wvals[1].type        = JSON_ARRAY;
    wvals[1].array.items = item;
    wvals[1].array.count = 1;

    wvals[2].type        = JSON_ARRAY;
    wvals[2].array.items = NULL;
    wvals[2].array.count = 0;

    wvals[3].type    = JSON_BOOL;
    wvals[3].boolean = false;

    result->type            = JSON_OBJECT;
    result->object.keys     = wkeys;
    result->object.key_lens = wklens;
    result->object.vals     = wvals;
    result->object.count    = 4;
    return true;
}

bool cmd_clone_id(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract entity_id (required) — accepts number or name string. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;
    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    const edit_entity_t *src = edit_entity_store_get(ctx->entities, eid);
    if (!src) return false;

    /* Parse optional offset. */
    float off[3] = {0.0f, 0.0f, 0.0f};
    const json_value_t *ov = json_object_get(args, "offset");
    if (ov && ov->type == JSON_ARRAY && ov->array.count >= 3) {
        for (int i = 0; i < 3; i++) {
            if (ov->array.items[i].type == JSON_NUMBER)
                off[i] = (float)ov->array.items[i].number;
        }
    }

    /* Create a new entity slot. */
    uint32_t new_id = edit_entity_store_create(ctx->entities, src->type);
    if (new_id == EDIT_ENTITY_INVALID_ID) return false;

    edit_entity_t *dst = edit_entity_store_get_mut(ctx->entities, new_id);
    if (!dst) return false;

    /* Save the auto-assigned name before memcpy overwrites it. */
    char auto_name[EDIT_ENTITY_NAME_MAX];
    memcpy(auto_name, dst->name, EDIT_ENTITY_NAME_MAX);

    /* Copy entire entity, then fix up instance-specific fields. */
    memcpy(dst, src, sizeof(*dst));
    dst->active = true;
    dst->body_index = EDIT_ENTITY_INVALID_ID;
    dst->pending_delete = false;
    dst->refresh_gen = 0;
    /* Restore the monotonic name assigned by create(). */
    memcpy(dst->name, auto_name, EDIT_ENTITY_NAME_MAX);

    /* Apply position offset. */
    dst->pos[0] = src->pos[0] + off[0];
    dst->pos[1] = src->pos[1] + off[1];
    dst->pos[2] = src->pos[2] + off[2];

    /* Record undo. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.entity_id = new_id;
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    /* Notify physics bridge. */
    if (ctx->bridge && ctx->bridge->on_spawn) {
        uint32_t body = ctx->bridge->on_spawn(
            ctx->bridge->user_data, new_id, dst);
        dst->body_index = body;
    }

    /* Update selection: add clone to selection. */
    if (ctx->selection) {
        edit_selection_add(ctx->selection, new_id);
    }

    /* Version stamp the cloned entity. */
    if (ctx->version) edit_version_stamp(ctx->version, new_id);

    /* Return delta sync response with the new entity. */
    uint64_t ver = ctx->version ? ctx->version->version : 0;
    return build_sync_result_(dst, new_id, ver, result, arena);
}
