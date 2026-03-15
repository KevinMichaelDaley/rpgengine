/**
 * @file cmd_pivot_id.c
 * @brief Set pivot_offset on an entity by ID, adjusting pos to keep geometry.
 *
 * JSON args: {"entity_id": N_or_"name", "pivot": [x, y, z]}
 *
 * Adjusts entity.pos so the geometry center remains unchanged:
 *   pos_new = pos_old + R * S * (pivot_new - pivot_old)
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_pivot_id
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_pivot.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/**
 * @brief Extract a 3-element float array from a JSON array value.
 */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

bool cmd_pivot_id(edit_dispatch_t *d, const json_value_t *args,
                  json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Resolve entity ID. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;
    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Extract new pivot offset. */
    const json_value_t *piv_val = json_object_get(args, "pivot");
    float new_pivot[3];
    if (!extract_vec3_(piv_val, new_pivot)) return false;

    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Compute pos adjustment so geometry center stays fixed.
     * geo = pos + R * S * (-pivot)
     * We want geo_old == geo_new:
     *   pos_old + R*S*(-old_pivot) = pos_new + R*S*(-new_pivot)
     *   pos_new = pos_old + R*S*(new_pivot - old_pivot) */
    float dpivot[3] = {
        new_pivot[0] - e->pivot_offset[0],
        new_pivot[1] - e->pivot_offset[1],
        new_pivot[2] - e->pivot_offset[2],
    };
    vec3_t scaled = {
        dpivot[0] * e->scale[0],
        dpivot[1] * e->scale[1],
        dpivot[2] * e->scale[2],
    };
    vec3_t world_delta = quat_rotate_vec3(e->orientation, scaled);

    e->pivot_offset[0] = new_pivot[0];
    e->pivot_offset[1] = new_pivot[1];
    e->pivot_offset[2] = new_pivot[2];
    e->pos[0] += world_delta.x;
    e->pos[1] += world_delta.y;
    e->pos[2] += world_delta.z;

    /* Notify physics bridge. */
    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, eid,
                             e->body_index, e);
    }

    /* Version stamp. */
    if (ctx->version) edit_version_stamp(ctx->version, eid);

    return true;
}
