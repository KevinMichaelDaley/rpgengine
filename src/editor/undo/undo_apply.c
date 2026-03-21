/**
 * @file undo_apply.c
 * @brief Apply undo/redo operations to editor entities.
 *
 * Non-static functions (2 / 4 limit):
 *   edit_undo_apply_inverse
 *   edit_undo_apply_forward
 */

#include "ferrum/editor/undo_apply.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/editor/edit_scene_tree.h"
#include "ferrum/entity/entity_attrs.h"
#include "ferrum/math/quat.h"
#include <string.h>

/** @brief Stamp entity version so sync_entities picks up the change. */
static void stamp_(edit_cmd_ctx_t *ctx, uint32_t entity_id) {
    if (ctx->version) edit_version_stamp(ctx->version, entity_id);
}

/* ----------------------------------------------------------------------- */
/* Internal: apply a command type to an entity                               */
/* ----------------------------------------------------------------------- */

/**
 * @brief Apply a MOVE delta to an entity.
 *
 * Adds delta[0..2] to entity position and notifies the physics bridge.
 */
static bool apply_move_(edit_cmd_ctx_t *ctx, uint32_t entity_id,
                         const float delta[4]) {
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, entity_id);
    if (!e) return false;

    e->pos[0] += delta[0];
    e->pos[1] += delta[1];
    e->pos[2] += delta[2];

    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, entity_id,
                             e->body_index, e);
    }
    stamp_(ctx, entity_id);
    return true;
}

/**
 * @brief Apply a ROTATE quaternion delta to an entity.
 *
 * Composes delta quaternion (delta[0..3] = x,y,z,w) with the entity's
 * current orientation, then syncs the euler cache.
 */
static bool apply_rotate_(edit_cmd_ctx_t *ctx, uint32_t entity_id,
                            const float delta[4]) {
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, entity_id);
    if (!e) return false;

    quat_t dq = {delta[0], delta[1], delta[2], delta[3]};
    e->orientation = quat_normalize_safe(
        quat_mul(dq, e->orientation), 1e-8f);

    /* Sync euler cache. */
    quat_t cq = e->orientation;
    if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
    quat_to_euler_yxz(cq, &e->rot[0], &e->rot[1], &e->rot[2]);
    float rad_to_deg = 180.0f / 3.14159265358979323846f;
    e->rot[0] *= rad_to_deg;
    e->rot[1] *= rad_to_deg;
    e->rot[2] *= rad_to_deg;

    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, entity_id,
                             e->body_index, e);
    }
    stamp_(ctx, entity_id);
    return true;
}

/**
 * @brief Apply a SCALE factor to an entity.
 *
 * Multiplies entity scale by delta[0..2].
 */
static bool apply_scale_(edit_cmd_ctx_t *ctx, uint32_t entity_id,
                           const float delta[4]) {
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, entity_id);
    if (!e) return false;

    e->scale[0] *= delta[0];
    e->scale[1] *= delta[1];
    e->scale[2] *= delta[2];

    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, entity_id,
                             e->body_index, e);
    }
    stamp_(ctx, entity_id);
    return true;
}

/**
 * @brief Delete an entity (undo of spawn, or forward of delete).
 */
static bool apply_delete_(edit_cmd_ctx_t *ctx, uint32_t entity_id) {
    const edit_entity_t *e = edit_entity_store_get(ctx->entities, entity_id);
    if (!e) return false;

    if (ctx->bridge && ctx->bridge->on_delete) {
        ctx->bridge->on_delete(ctx->bridge->user_data, entity_id,
                               e->body_index);
    }
    if (ctx->version) edit_version_tombstone(ctx->version, entity_id);
    edit_entity_store_remove(ctx->entities, entity_id);
    return true;
}

/**
 * @brief Restore an entity from snapshot (undo of delete, or forward of spawn).
 */
static bool apply_spawn_from_snapshot_(edit_cmd_ctx_t *ctx,
                                        uint32_t entity_id,
                                        const void *snapshot_data,
                                        uint32_t snapshot_size) {
    if (!snapshot_data || snapshot_size < sizeof(edit_entity_t)) return false;

    const edit_entity_t *snap = (const edit_entity_t *)snapshot_data;
    if (!edit_entity_store_restore(ctx->entities, entity_id, snap)) {
        return false;
    }

    /* Notify physics bridge of the restored entity. */
    if (ctx->bridge && ctx->bridge->on_spawn) {
        const edit_entity_t *restored =
            edit_entity_store_get(ctx->entities, entity_id);
        uint32_t body_idx = ctx->bridge->on_spawn(
            ctx->bridge->user_data, entity_id, restored);
        edit_entity_t *e_mut =
            edit_entity_store_get_mut(ctx->entities, entity_id);
        if (e_mut) e_mut->body_index = body_idx;
    }
    stamp_(ctx, entity_id);
    return true;
}

/**
 * @brief Apply a REPARENT operation.
 *
 * target_parent is a uint32 stored as float bits in the delta.
 */
static bool apply_reparent_(edit_cmd_ctx_t *ctx, uint32_t entity_id,
                              uint32_t target_parent) {
    edit_scene_tree_t *tree = ctx->entities->tree;
    if (!tree) return false;

    if (target_parent == EDIT_SCENE_TREE_NONE) {
        edit_scene_tree_detach(tree, entity_id);
    } else {
        if (!edit_scene_tree_attach(tree, entity_id, target_parent)) {
            return false;
        }
    }

    /* Update attr. */
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, entity_id);
    if (e) {
        entity_attrs_set(&e->attrs, SCRIPT_KEY_PARENT_ID,
                          SCRIPT_ATTR_U32, &target_parent, sizeof(target_parent));
    }
    stamp_(ctx, entity_id);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Internal: dispatch by command type                                        */
/* ----------------------------------------------------------------------- */

static bool dispatch_cmd_type_(edit_cmd_ctx_t *ctx, uint32_t cmd_type,
                                const edit_undo_entry_t *entry) {
    switch (cmd_type) {
    case EDIT_CMD_TYPE_MOVE:
        return apply_move_(ctx, entry->entity_id, entry->delta);

    case EDIT_CMD_TYPE_ROTATE:
        return apply_rotate_(ctx, entry->entity_id, entry->delta);

    case EDIT_CMD_TYPE_SCALE:
        return apply_scale_(ctx, entry->entity_id, entry->delta);

    case EDIT_CMD_TYPE_DELETE:
        return apply_delete_(ctx, entry->entity_id);

    case EDIT_CMD_TYPE_SPAWN:
        return apply_spawn_from_snapshot_(ctx, entry->entity_id,
                                           entry->snapshot_data,
                                           entry->snapshot_size);

    case EDIT_CMD_TYPE_REPARENT: {
        /* delta[0] = target parent for this direction (uint32 as float bits). */
        uint32_t target;
        memcpy(&target, &entry->delta[0], sizeof(uint32_t));
        return apply_reparent_(ctx, entry->entity_id, target);
    }

    default:
        return false;
    }
}

/* ----------------------------------------------------------------------- */
/* Public API                                                                */
/* ----------------------------------------------------------------------- */

bool edit_undo_apply_inverse(edit_cmd_ctx_t *ctx,
                              const edit_undo_entry_t *entry) {
    if (!ctx || !entry || !ctx->entities) return false;
    return dispatch_cmd_type_(ctx, entry->inverse_type, entry);
}

bool edit_undo_apply_forward(edit_cmd_ctx_t *ctx,
                              const edit_undo_entry_t *entry) {
    if (!ctx || !entry || !ctx->entities) return false;

    /* For forward application, the delta is the negation of what's stored
     * (since the stored delta is the inverse). Build a temp entry with
     * negated delta for move, conjugated quat for rotate, reciprocal
     * for scale. For spawn/delete, the forward_type dispatch handles it. */
    uint32_t fwd_type = entry->forward_type;

    if (fwd_type == EDIT_CMD_TYPE_MOVE) {
        float neg_delta[4] = {
            -entry->delta[0], -entry->delta[1],
            -entry->delta[2], 0.0f
        };
        edit_undo_entry_t tmp = *entry;
        tmp.delta[0] = neg_delta[0];
        tmp.delta[1] = neg_delta[1];
        tmp.delta[2] = neg_delta[2];
        return apply_move_(ctx, tmp.entity_id, tmp.delta);
    }

    if (fwd_type == EDIT_CMD_TYPE_ROTATE) {
        /* Forward rotate = conjugate of inverse delta. */
        edit_undo_entry_t tmp = *entry;
        quat_t inv = {entry->delta[0], entry->delta[1],
                       entry->delta[2], entry->delta[3]};
        quat_t fwd = quat_conjugate(inv);
        tmp.delta[0] = fwd.x;
        tmp.delta[1] = fwd.y;
        tmp.delta[2] = fwd.z;
        tmp.delta[3] = fwd.w;
        return apply_rotate_(ctx, tmp.entity_id, tmp.delta);
    }

    if (fwd_type == EDIT_CMD_TYPE_SCALE) {
        /* Forward scale = reciprocal of inverse factors. */
        edit_undo_entry_t tmp = *entry;
        tmp.delta[0] = (entry->delta[0] != 0.0f) ? 1.0f / entry->delta[0] : 1.0f;
        tmp.delta[1] = (entry->delta[1] != 0.0f) ? 1.0f / entry->delta[1] : 1.0f;
        tmp.delta[2] = (entry->delta[2] != 0.0f) ? 1.0f / entry->delta[2] : 1.0f;
        return apply_scale_(ctx, tmp.entity_id, tmp.delta);
    }

    if (fwd_type == EDIT_CMD_TYPE_REPARENT) {
        /* Forward reparent uses delta[1] (new parent). */
        uint32_t target;
        memcpy(&target, &entry->delta[1], sizeof(uint32_t));
        return apply_reparent_(ctx, entry->entity_id, target);
    }

    /* For SPAWN/DELETE forward, just dispatch directly. */
    return dispatch_cmd_type_(ctx, fwd_type, entry);
}
