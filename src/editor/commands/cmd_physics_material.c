/**
 * @file cmd_physics_material.c
 * @brief Set physics material properties (friction, restitution) on an entity.
 *
 * JSON args: {"entity_id": N_or_"name", "friction": f, "restitution": r}
 * At least one of friction/restitution must be present.
 * Stores values as entity attrs and calls bridge on_set_material.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/entity/entity_attrs.h"

bool cmd_physics_material(edit_dispatch_t *d, const json_value_t *args,
                          json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Resolve entity. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;
    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Read friction and restitution from args. */
    const json_value_t *fric_val = json_object_get(args, "friction");
    const json_value_t *rest_val = json_object_get(args, "restitution");
    if (!fric_val && !rest_val) return false;

    /* Defaults from current entity attrs or body_init defaults. */
    float friction    = 0.5f;
    float restitution = 0.0f;

    /* Read existing values if present. */
    {
        uint8_t at = 0, as = 0;
        const void *fv = entity_attrs_get(&e->attrs, SCRIPT_KEY_FRICTION,
                                          &at, &as);
        if (fv && as == 4) memcpy(&friction, fv, 4);
    }
    {
        uint8_t at = 0, as = 0;
        const void *rv = entity_attrs_get(&e->attrs, SCRIPT_KEY_RESTITUTION,
                                          &at, &as);
        if (rv && as == 4) memcpy(&restitution, rv, 4);
    }

    /* Override with provided values. */
    if (fric_val && fric_val->type == JSON_NUMBER) {
        friction = (float)fric_val->number;
    }
    if (rest_val && rest_val->type == JSON_NUMBER) {
        restitution = (float)rest_val->number;
    }

    /* Store on entity attrs. */
    entity_attrs_set(&e->attrs, SCRIPT_KEY_FRICTION,
                     SCRIPT_ATTR_F32, &friction, 4);
    entity_attrs_set(&e->attrs, SCRIPT_KEY_RESTITUTION,
                     SCRIPT_ATTR_F32, &restitution, 4);

    /* Notify physics via bridge. */
    if (e->body_index != 0 && ctx->bridge && ctx->bridge->on_set_material) {
        ctx->bridge->on_set_material(ctx->bridge->user_data,
                                     e->body_index, friction, restitution);
    }

    return true;
}
