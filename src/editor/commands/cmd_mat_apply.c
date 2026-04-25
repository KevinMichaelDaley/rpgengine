/**
 * @file cmd_mat_apply.c
 * @brief Editor command: mat_apply — apply .fmat material to entity.
 *
 * JSON args:
 *   {
 *     "entity": 42,
 *     "path": "assets/materials/wood.fmat"
 *   }
 *
 * Returns: true on success.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/editor/def/material_def.h"

#include <string.h>

bool cmd_mat_apply(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Get entity ID. */
    const json_value_t *ev = json_object_get(args, "entity");
    if (!ev || ev->type != JSON_NUMBER) return false;
    uint32_t eid = (uint32_t)ev->number;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Get material path. */
    const json_value_t *path_val = json_object_get(args, "path");
    if (!path_val || path_val->type != JSON_STRING) return false;

    char path[256];
    uint32_t nlen = path_val->string.len;
    if (nlen >= sizeof(path)) nlen = sizeof(path) - 1;
    memcpy(path, path_val->string.ptr, nlen);
    path[nlen] = '\0';

    /* Load material definition. */
    material_def_t def;
    if (material_def_load(path, &def) != MATERIAL_DEF_OK) {
        return false;
    }

    /* Record undo: snapshot entity before change. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.entity_id = eid;
        edit_undo_record(ctx->undo, &entry, e, sizeof(*e));
    }

    /* Apply slots. */
    if (def.slot_albedo[0])    strncpy(e->materials[EDIT_MATERIAL_SLOT_ALBEDO], def.slot_albedo, EDIT_MATERIAL_PATH_MAX - 1);
    if (def.slot_normal[0])    strncpy(e->materials[EDIT_MATERIAL_SLOT_NORMAL], def.slot_normal, EDIT_MATERIAL_PATH_MAX - 1);
    if (def.slot_roughness[0]) strncpy(e->materials[EDIT_MATERIAL_SLOT_ROUGHNESS], def.slot_roughness, EDIT_MATERIAL_PATH_MAX - 1);
    if (def.slot_metallic[0])  strncpy(e->materials[EDIT_MATERIAL_SLOT_METALLIC], def.slot_metallic, EDIT_MATERIAL_PATH_MAX - 1);
    if (def.slot_emissive[0])  strncpy(e->materials[EDIT_MATERIAL_SLOT_EMISSIVE], def.slot_emissive, EDIT_MATERIAL_PATH_MAX - 1);

    /* Version stamp. */
    if (ctx->version) edit_version_stamp(ctx->version, eid);

    result->type = JSON_BOOL;
    result->boolean = true;
    return true;
}
