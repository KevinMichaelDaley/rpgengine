/**
 * @file cmd_unparent.c
 * @brief Unparent command — detach entity to root, children stay attached.
 *
 * JSON args: {"entity_id": <id_or_name>}
 * Records undo entry with old parent for reversal.
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_unparent
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_scene_tree.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/entity/entity_attrs.h"

#include <string.h>

bool cmd_unparent(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;

    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    edit_scene_tree_t *tree = ctx->entities->tree;
    if (!tree) return false;

    /* Save old parent for undo. */
    uint32_t old_parent = edit_scene_tree_get_parent(tree, eid);
    if (old_parent == EDIT_SCENE_TREE_NONE) {
        /* Already a root — no-op success. */
        result->type    = JSON_BOOL;
        result->boolean = true;
        return true;
    }

    edit_scene_tree_detach(tree, eid);

    /* Clear parent_id attribute. */
    uint32_t none = EDIT_SCENE_TREE_NONE;
    edit_entity_t *ent = edit_entity_store_get_mut(ctx->entities, eid);
    if (ent) {
        entity_attrs_set(&ent->attrs, SCRIPT_KEY_PARENT_ID,
                          SCRIPT_ATTR_U32, &none, sizeof(none));
    }

    /* Version stamp so sync picks up the change. */
    if (ctx->version) edit_version_stamp(ctx->version, eid);

    /* Record undo: store old parent for re-attach. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_REPARENT;
        entry.inverse_type = EDIT_CMD_TYPE_REPARENT;
        entry.entity_id    = eid;
        /* Inverse: re-attach to old parent. */
        memcpy(&entry.delta[0], &old_parent, sizeof(uint32_t));
        /* Forward: detach (NONE). */
        memcpy(&entry.delta[1], &none, sizeof(uint32_t));
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    result->type    = JSON_BOOL;
    result->boolean = true;
    return true;
}
