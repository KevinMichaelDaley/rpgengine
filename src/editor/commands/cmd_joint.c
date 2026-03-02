/**
 * @file cmd_joint.c
 * @brief Editor command: joint — create a physics joint between two entities.
 *
 * Syntax:
 *   joint <type> <entity_a> <entity_b> <anchor_x> <anchor_y> <anchor_z>
 *         [axis_x axis_y axis_z]
 *
 * JSON args:
 *   {
 *     "joint_type": "hinge" | "ball" | "distance",
 *     "entity_a":   <id|name>,
 *     "entity_b":   <id|name>,
 *     "anchor":     [x, y, z],      (world-space attach point)
 *     "axis":       [ax, ay, az]    (optional, default [0,1,0] for hinge)
 *   }
 *
 * The command resolves both entities, computes local-space anchors by
 * subtracting each entity's position from the world anchor, and calls
 * the bridge on_joint callback.
 *
 * Non-static functions: cmd_joint (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"

#include <string.h>
#include <stdio.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

/**
 * @brief Extract a float[3] from a JSON array.
 */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

/* ── Command handler ──────────────────────────────────────────────── */

bool cmd_joint(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena) {
    (void)arena;
    if (!d || !args) return false;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->bridge || !ctx->bridge->on_joint) {
        result->type = JSON_NULL;
        return false;
    }

    /* Parse joint type string. */
    const json_value_t *jt = json_object_get(args, "joint_type");
    if (!jt || jt->type != JSON_STRING) {
        result->type = JSON_NULL;
        return false;
    }

    int joint_type = -1;
    if (strcmp(jt->string.ptr, "hinge") == 0 ||
        strcmp(jt->string.ptr, "revolute") == 0) {
        joint_type = 2; /* PHYS_JOINT_HINGE */
    } else if (strcmp(jt->string.ptr, "ball") == 0) {
        joint_type = 1; /* PHYS_JOINT_BALL */
    } else if (strcmp(jt->string.ptr, "distance") == 0) {
        joint_type = 0; /* PHYS_JOINT_DISTANCE */
    } else {
        result->type = JSON_NULL;
        return false;
    }

    /* Resolve entities. */
    const json_value_t *ea = json_object_get(args, "entity_a");
    const json_value_t *eb = json_object_get(args, "entity_b");
    if (!ea || !eb) {
        result->type = JSON_NULL;
        return false;
    }

    uint32_t id_a = edit_cmd_resolve_entity(ctx, ea);
    uint32_t id_b = edit_cmd_resolve_entity(ctx, eb);
    if (id_a == UINT32_MAX || id_b == UINT32_MAX) {
        result->type = JSON_NULL;
        return false;
    }

    /* Look up entities to get positions and body indices. */
    const edit_entity_t *ent_a = edit_entity_store_get(ctx->entities, id_a);
    const edit_entity_t *ent_b = edit_entity_store_get(ctx->entities, id_b);
    if (!ent_a || !ent_b) {
        result->type = JSON_NULL;
        return false;
    }
    if (ent_a->body_index == UINT32_MAX || ent_b->body_index == UINT32_MAX) {
        result->type = JSON_NULL;
        return false;
    }

    /* Parse world-space anchor point. */
    float anchor[3] = {0.0f, 0.0f, 0.0f};
    const json_value_t *anch = json_object_get(args, "anchor");
    if (anch) {
        extract_vec3_(anch, anchor);
    }

    /* Parse hinge axis (default: Y-up). */
    float axis[3] = {0.0f, 1.0f, 0.0f};
    const json_value_t *ax = json_object_get(args, "axis");
    if (ax) {
        extract_vec3_(ax, axis);
    }

    /* Compute local-space anchors: world_anchor - entity_position. */
    float local_a[3] = {
        anchor[0] - ent_a->pos[0],
        anchor[1] - ent_a->pos[1],
        anchor[2] - ent_a->pos[2],
    };
    float local_b[3] = {
        anchor[0] - ent_b->pos[0],
        anchor[1] - ent_b->pos[1],
        anchor[2] - ent_b->pos[2],
    };

    /* Call bridge to create the joint in the physics world. */
    uint32_t ji = ctx->bridge->on_joint(
        ctx->bridge->user_data,
        ent_a->body_index, ent_b->body_index,
        joint_type, local_a, local_b, axis);

    if (ji == UINT32_MAX) {
        result->type = JSON_NULL;
        return false;
    }

    result->type   = JSON_NUMBER;
    result->number = (double)ji;
    return true;
}
