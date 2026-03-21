/**
 * @file cmd_unparent.c
 * @brief Unparent command — detach entity to root, children stay attached.
 *
 * JSON args: {"entity_id": <id_or_name>}
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_unparent
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_scene_tree.h"
#include "ferrum/entity/entity_attrs.h"

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

    edit_scene_tree_detach(tree, eid);

    /* Remove parent_id attribute. */
    edit_entity_t *ent = edit_entity_store_get_mut(ctx->entities, eid);
    if (ent) {
        uint32_t none = EDIT_SCENE_TREE_NONE;
        entity_attrs_set(&ent->attrs, SCRIPT_KEY_PARENT_ID,
                          SCRIPT_ATTR_U32, &none, sizeof(none));
    }

    result->type    = JSON_BOOL;
    result->boolean = true;
    return true;
}
