/**
 * @file cmd_mesh_commit.c
 * @brief mesh_commit command — bake editable mesh to world entity.
 *
 * JSON args: {"entity_name":"room_01", "material_override":"textures/brick.png"}
 * Both args are optional.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_commit.h"
#include <stdlib.h>
#include <string.h>

bool cmd_mesh_commit(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh || !ctx->entities) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot || slot->vertex_count == 0) return false;

    mesh_commit_args_t cargs;
    memset(&cargs, 0, sizeof(cargs));
    cargs.clear_slot = true;

    if (args) {
        const json_value_t *name = json_object_get(args, "entity_name");
        if (name && name->type == JSON_STRING && name->string.len > 0) {
            uint32_t len = name->string.len;
            if (len >= sizeof(cargs.entity_name)) len = sizeof(cargs.entity_name) - 1;
            memcpy(cargs.entity_name, name->string.ptr, len);
            cargs.entity_name[len] = '\0';
        }

        const json_value_t *mat = json_object_get(args, "material_override");
        if (mat && mat->type == JSON_STRING && mat->string.len > 0) {
            uint32_t len = mat->string.len;
            if (len >= sizeof(cargs.material_override)) len = sizeof(cargs.material_override) - 1;
            memcpy(cargs.material_override, mat->string.ptr, len);
            cargs.material_override[len] = '\0';
        }

        const json_value_t *keep = json_object_get(args, "keep_slot");
        if (keep && keep->type == JSON_BOOL && keep->boolean) cargs.clear_slot = false;
    }

    mesh_commit_result_t cresult;
    if (!mesh_commit(slot, ctx->entities, &cargs, &cresult)) {
        return false;
    }

    /* Notify physics bridge if available */
    if (ctx->bridge && ctx->bridge->on_spawn) {
        const edit_entity_t *ent = edit_entity_store_get(ctx->entities,
                                                          cresult.entity_id);
        if (ent) {
            uint32_t body = ctx->bridge->on_spawn(ctx->bridge->user_data,
                                                    cresult.entity_id, ent);
            edit_entity_t *mut = edit_entity_store_get_mut(ctx->entities,
                                                            cresult.entity_id);
            if (mut) mut->body_index = body;

            /* Send mesh data to the bridge so it can forward to clients. */
            if (ctx->bridge->on_mesh_data && cresult.fvma_data) {
                ctx->bridge->on_mesh_data(ctx->bridge->user_data, body,
                                          cresult.fvma_data,
                                          (uint32_t)cresult.fvma_size);
            }
        }
    }

    free(cresult.fvma_data);

    result->type = JSON_NUMBER; result->number = (double)cresult.entity_id; (void)arena;
    return true;
}
