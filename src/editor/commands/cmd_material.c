/**
 * @file cmd_material.c
 * @brief Material assignment commands — set/get entity material slots.
 *
 * JSON protocol:
 *   Set: {"cmd":"material","args":{"sub":"set","entity":<id>,"slot":"albedo","path":"textures/brick.png"}}
 *   Get: {"cmd":"material","args":{"sub":"get","entity":<id>}}
 *
 * Non-static functions: 1 (cmd_material).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"

#include <string.h>
#include <stdio.h>

/** Map slot name to slot index. Returns -1 if invalid. */
static int slot_from_name_(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "albedo")    == 0) return EDIT_MATERIAL_SLOT_ALBEDO;
    if (strcmp(name, "normal")    == 0) return EDIT_MATERIAL_SLOT_NORMAL;
    if (strcmp(name, "roughness") == 0) return EDIT_MATERIAL_SLOT_ROUGHNESS;
    if (strcmp(name, "metallic")  == 0) return EDIT_MATERIAL_SLOT_METALLIC;
    if (strcmp(name, "emissive")  == 0) return EDIT_MATERIAL_SLOT_EMISSIVE;
    return -1;
}

/** Get slot name from index. */
static const char *slot_name_(int idx) {
    switch (idx) {
        case EDIT_MATERIAL_SLOT_ALBEDO:    return "albedo";
        case EDIT_MATERIAL_SLOT_NORMAL:    return "normal";
        case EDIT_MATERIAL_SLOT_ROUGHNESS: return "roughness";
        case EDIT_MATERIAL_SLOT_METALLIC:  return "metallic";
        case EDIT_MATERIAL_SLOT_EMISSIVE:  return "emissive";
        default: return "unknown";
    }
}

bool cmd_material(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Get sub-command: "set" or "get". */
    char sub[16] = "";
    const json_value_t *sv = json_object_get(args, "sub");
    if (sv && sv->type == JSON_STRING && sv->string.len < sizeof(sub)) {
        memcpy(sub, sv->string.ptr, sv->string.len);
        sub[sv->string.len] = '\0';
    }

    /* Get entity ID. */
    const json_value_t *ev = json_object_get(args, "entity");
    if (!ev || ev->type != JSON_NUMBER) return false;
    uint32_t entity_id = (uint32_t)ev->number;

    edit_entity_t *ent = edit_entity_store_get_mut(ctx->entities, entity_id);
    if (!ent) return false;

    if (strcmp(sub, "set") == 0) {
        /* Get slot name. */
        char slot_name[32] = "";
        const json_value_t *slv = json_object_get(args, "slot");
        if (slv && slv->type == JSON_STRING && slv->string.len < sizeof(slot_name)) {
            memcpy(slot_name, slv->string.ptr, slv->string.len);
            slot_name[slv->string.len] = '\0';
        }
        int slot = slot_from_name_(slot_name);
        if (slot < 0) return false;

        /* Get material path. */
        char path[EDIT_MATERIAL_PATH_MAX] = "";
        const json_value_t *pv = json_object_get(args, "path");
        if (pv && pv->type == JSON_STRING && pv->string.len > 0) {
            uint32_t n = pv->string.len;
            if (n >= sizeof(path)) n = sizeof(path) - 1;
            memcpy(path, pv->string.ptr, n);
            path[n] = '\0';
        }

        /* Record undo: snapshot entity before change. */
        if (ctx->undo) {
            edit_undo_entry_t entry = {
                .forward_type  = 0,
                .inverse_type  = 0,
                .entity_id     = entity_id,
            };
            edit_undo_record(ctx->undo, &entry, ent, sizeof(*ent));
        }

        /* Set the material slot. */
        memcpy(ent->materials[slot], path, sizeof(path));

        /* Return true. */
        result->type = JSON_BOOL;
        result->boolean = true;
        return true;

    } else if (strcmp(sub, "get") == 0) {
        /* Build result object with slot names → paths. */
        /* Count non-empty slots. */
        uint32_t filled = 0;
        for (int i = 0; i < EDIT_MATERIAL_SLOT_COUNT; i++) {
            if (ent->materials[i][0] != '\0') filled++;
        }

        if (filled == 0) {
            result->type = JSON_NULL;
            return true;
        }

        /* Build JSON string manually into arena. */
        char buf[2048];
        int pos = 0;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "{");
        bool first = true;
        for (int i = 0; i < EDIT_MATERIAL_SLOT_COUNT; i++) {
            if (ent->materials[i][0] != '\0') {
                if (!first) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",");
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                                "\"%s\":\"%s\"", slot_name_(i), ent->materials[i]);
                first = false;
            }
        }
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "}");

        /* Copy into arena as a string value. */
        size_t len = (size_t)pos;
        if (arena->used + len + 1 > arena->cap) return false;
        char *str = (char *)(arena->buf + arena->used);
        arena->used += len + 1;
        memcpy(str, buf, len);
        str[len] = '\0';

        result->type = JSON_STRING;
        result->string.ptr = str;
        result->string.len = (uint32_t)len;
        return true;
    }

    return false;
}
