/**
 * @file cmd_select_near.c
 * @brief Select entities within a distance of a point.
 *
 * JSON args: {"pos": [x,y,z], "dist": r}
 * If "pos" is omitted, uses the position of the entity named "@cursor" (if any).
 * Selects all active entities (excluding @cursor itself) within Euclidean
 * distance r of the given point.
 * Returns the number of entities selected.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include <math.h>
#include <string.h>

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

bool cmd_select_near(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection || !args) return false;

    /* Extract distance (required). */
    const json_value_t *dist_val = json_object_get(args, "dist");
    if (!dist_val || dist_val->type != JSON_NUMBER) return false;
    float dist = (float)dist_val->number;
    if (dist <= 0.0f) return false;
    float dist_sq = dist * dist;

    /* Extract position — optional; default to @cursor position. */
    float pos[3] = {0, 0, 0};
    const json_value_t *pos_val = json_object_get(args, "pos");
    uint32_t cursor_id = EDIT_ENTITY_INVALID_ID;

    if (pos_val && pos_val->type == JSON_ARRAY) {
        if (!extract_vec3_(pos_val, pos)) return false;
    } else {
        /* Look for @cursor entity. */
        cursor_id = edit_entity_store_find_by_name(ctx->entities, "@cursor");
        if (cursor_id == EDIT_ENTITY_INVALID_ID) return false;
        const edit_entity_t *cursor = edit_entity_store_get(ctx->entities,
                                                             cursor_id);
        if (!cursor) return false;
        pos[0] = cursor->pos[0];
        pos[1] = cursor->pos[1];
        pos[2] = cursor->pos[2];
    }

    /* Iterate all entities, select those within distance. */
    uint32_t matched = 0;
    for (uint32_t i = 0; i < ctx->entities->capacity; i++) {
        const edit_entity_t *e = edit_entity_store_get(ctx->entities, i);
        if (!e) continue;

        /* Skip all @ entities (cursor, aliases). */
        if (i == cursor_id) continue;
        if (e->name[0] == '@') continue;

        float dx = e->pos[0] - pos[0];
        float dy = e->pos[1] - pos[1];
        float dz = e->pos[2] - pos[2];
        float d2 = dx * dx + dy * dy + dz * dz;

        if (d2 <= dist_sq) {
            edit_selection_add(ctx->selection, i);
            matched++;
        }
    }

    /* Return count of matched entities. */
    result->type = JSON_NUMBER;
    result->number = (double)matched;
    return true;
}
