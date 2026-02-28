/**
 * @file edit_deserialize.c
 * @brief Level deserialization — load entities from JSON.
 */

#include "ferrum/editor/edit_serialize.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/json_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Check path for directory traversal (".."). */
static bool path_is_safe_(const char *path) {
    if (!path) return false;
    if (strstr(path, "..") != NULL) return false;
    return true;
}

/** @brief Extract a float[3] from a JSON array. */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

/** @brief Determine entity type from a JSON string value using registry. */
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

/** @brief Clear all entities in the store. */
static void clear_store_(edit_entity_store_t *store) {
    for (uint32_t i = 0; i < store->capacity; i++) {
        store->entities[i].active = false;
    }
}

bool edit_level_deserialize(struct edit_entity_store *store,
                            const char *json, size_t json_len) {
    if (!store || !json || json_len == 0) return false;

    /* Parse JSON. Arena sized for ~4096 entities. */
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

    /* Validate structure. */
    const json_value_t *entities = json_object_get(&root, "entities");
    if (!entities || entities->type != JSON_ARRAY) {
        free(arena_buf);
        return false;
    }

    /* Clear existing entities. */
    clear_store_(store);

    /* Populate entities. */
    for (uint32_t i = 0; i < entities->array.count; i++) {
        const json_value_t *ent = &entities->array.items[i];
        if (ent->type != JSON_OBJECT) continue;

        /* Get entity ID (for slot placement). */
        const json_value_t *id_val = json_object_get(ent, "id");
        uint32_t target_id;
        if (id_val && id_val->type == JSON_NUMBER) {
            target_id = (uint32_t)id_val->number;
        } else {
            /* No explicit ID; append. */
            target_id = EDIT_ENTITY_INVALID_ID;
        }

        /* Parse type. */
        uint32_t type = parse_type_(json_object_get(ent, "type"));

        /* Parse transform. */
        float pos[3] = {0, 0, 0};
        float rot[3] = {0, 0, 0};
        float scale[3] = {1, 1, 1};
        extract_vec3_(json_object_get(ent, "pos"), pos);
        extract_vec3_(json_object_get(ent, "rot"), rot);
        extract_vec3_(json_object_get(ent, "scale"), scale);

        /* Parse optional name. */
        char ent_name[EDIT_ENTITY_NAME_MAX] = {0};
        const json_value_t *name_val = json_object_get(ent, "name");
        if (name_val && name_val->type == JSON_STRING &&
            name_val->string.len > 0) {
            uint32_t nlen = name_val->string.len;
            if (nlen >= sizeof(ent_name)) nlen = sizeof(ent_name) - 1;
            memcpy(ent_name, name_val->string.ptr, nlen);
            ent_name[nlen] = '\0';
        }

        /* Place entity at target slot or first available. */
        if (target_id < store->capacity && !store->entities[target_id].active) {
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
            /* Fallback: create at first free slot. */
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

    free(arena_buf);
    return true;
}

bool edit_level_load(struct edit_entity_store *store, const char *path) {
    if (!store || !path || !path_is_safe_(path)) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return false; }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';

    bool ok = edit_level_deserialize(store, buf, nread);
    free(buf);
    return ok;
}
