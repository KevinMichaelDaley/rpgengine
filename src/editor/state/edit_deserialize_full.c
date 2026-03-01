/**
 * @file edit_deserialize_full.c
 * @brief Full level deserialization — entities + groups from JSON.
 *
 * Supports version 1 (entities only) and version 2 (entities + groups).
 *
 * Non-static functions: 1 (edit_level_deserialize_full).
 */

#include "ferrum/editor/edit_serialize.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/json_parse.h"

#include <stdlib.h>
#include <string.h>

/** @brief Extract a float[3] from a JSON array. */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

/** @brief Determine entity type from a JSON string. */
static uint32_t parse_type_(const json_value_t *type_val) {
    if (!type_val || type_val->type != JSON_STRING) return EDIT_ENTITY_TYPE_BOX;
    char name[32];
    uint32_t len = type_val->string.len;
    if (len >= sizeof(name)) len = sizeof(name) - 1;
    memcpy(name, type_val->string.ptr, len);
    name[len] = '\0';
    uint32_t id = edit_entity_type_by_name(name);
    return (id != UINT32_MAX) ? id : EDIT_ENTITY_TYPE_BOX;
}

/** @brief Copy a JSON string into a fixed-size buffer. */
static void copy_json_str_(const json_value_t *v, char *out, uint32_t cap) {
    out[0] = '\0';
    if (!v || v->type != JSON_STRING || v->string.len == 0) return;
    uint32_t n = v->string.len;
    if (n >= cap) n = cap - 1;
    memcpy(out, v->string.ptr, n);
    out[n] = '\0';
}

bool edit_level_deserialize_full(struct edit_entity_store *store,
                                struct edit_cmd_ctx *ctx,
                                const char *json, size_t json_len) {
    if (!store || !json || json_len == 0) return false;

    /* Parse JSON. */
    size_t arena_size = json_len * 4 + 65536;
    uint8_t *arena_buf = (uint8_t *)malloc(arena_size);
    if (!arena_buf) return false;

    json_arena_t arena;
    json_arena_init(&arena, arena_buf, arena_size);

    json_value_t root;
    if (!json_parse(json, json_len, &arena, &root)) {
        free(arena_buf);
        return false;
    }

    /* ── Entities ──────────────────────────────────────────────── */
    const json_value_t *entities = json_object_get(&root, "entities");
    if (!entities || entities->type != JSON_ARRAY) {
        free(arena_buf);
        return false;
    }

    /* Clear store. */
    for (uint32_t i = 0; i < store->capacity; i++) {
        store->entities[i].active = false;
    }

    for (uint32_t i = 0; i < entities->array.count; i++) {
        const json_value_t *ent = &entities->array.items[i];
        if (ent->type != JSON_OBJECT) continue;

        const json_value_t *id_val = json_object_get(ent, "id");
        uint32_t target_id = EDIT_ENTITY_INVALID_ID;
        if (id_val && id_val->type == JSON_NUMBER) {
            target_id = (uint32_t)id_val->number;
        }

        uint32_t type = parse_type_(json_object_get(ent, "type"));

        float pos[3] = {0, 0, 0};
        float rot[3] = {0, 0, 0};
        float scale[3] = {1, 1, 1};
        extract_vec3_(json_object_get(ent, "pos"), pos);
        extract_vec3_(json_object_get(ent, "rot"), rot);
        extract_vec3_(json_object_get(ent, "scale"), scale);

        char ent_name[EDIT_ENTITY_NAME_MAX] = {0};
        copy_json_str_(json_object_get(ent, "name"),
                       ent_name, sizeof(ent_name));

        if (target_id < store->capacity &&
            !store->entities[target_id].active) {
            edit_entity_t *e = &store->entities[target_id];
            memset(e, 0, sizeof(*e));
            e->active = true;
            e->type = type;
            memcpy(e->pos, pos, sizeof(pos));
            memcpy(e->rot, rot, sizeof(rot));
            memcpy(e->scale, scale, sizeof(scale));
            memcpy(e->name, ent_name, sizeof(ent_name));
            e->body_index = EDIT_ENTITY_INVALID_ID;
        } else {
            uint32_t eid = edit_entity_store_create(store, type);
            if (eid != EDIT_ENTITY_INVALID_ID) {
                edit_entity_t *e = edit_entity_store_get_mut(store, eid);
                memcpy(e->pos, pos, sizeof(pos));
                memcpy(e->rot, rot, sizeof(rot));
                memcpy(e->scale, scale, sizeof(scale));
                memcpy(e->name, ent_name, sizeof(ent_name));
            }
        }
    }

    /* ── Groups ────────────────────────────────────────────────── */
    if (ctx) {
        const json_value_t *groups = json_object_get(&root, "groups");
        if (groups && groups->type == JSON_ARRAY) {
            /* Lazy-allocate group array if needed. */
            if (!ctx->groups) {
                ctx->groups = calloc(EDIT_GROUP_MAX, sizeof(edit_group_t));
                if (ctx->groups) ctx->group_capacity = EDIT_GROUP_MAX;
            }

            if (ctx->groups) {
                uint32_t slot = 0;
                for (uint32_t i = 0; i < groups->array.count &&
                     slot < ctx->group_capacity; i++) {
                    const json_value_t *gv = &groups->array.items[i];
                    if (gv->type != JSON_OBJECT) continue;

                    edit_group_t *g = &ctx->groups[slot];
                    memset(g, 0, sizeof(*g));

                    /* Name. */
                    copy_json_str_(json_object_get(gv, "name"),
                                   g->name, sizeof(g->name));
                    if (g->name[0] == '\0') continue;

                    /* IDs. */
                    const json_value_t *ids = json_object_get(gv, "ids");
                    if (ids && ids->type == JSON_ARRAY) {
                        g->count = ids->array.count;
                        if (g->count > EDIT_GROUP_ENTRY_MAX)
                            g->count = EDIT_GROUP_ENTRY_MAX;
                        for (uint32_t j = 0; j < g->count; j++) {
                            g->ids[j] =
                                (uint32_t)ids->array.items[j].number;
                        }
                    }

                    /* Pivot. */
                    extract_vec3_(json_object_get(gv, "pivot"), g->pivot);

                    /* Parent. */
                    copy_json_str_(json_object_get(gv, "parent"),
                                   g->parent, sizeof(g->parent));

                    g->active = true;
                    slot++;
                }
            }
        }
    }

    free(arena_buf);
    return true;
}
