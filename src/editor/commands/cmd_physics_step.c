/**
 * @file cmd_physics_step.c
 * @brief Physics step and reset editor commands.
 *
 * step: advances exactly one tick while paused (fails if running).
 * reset: zeroes all velocities and pauses the simulation.
 *
 * Both commands delegate to edit_physics_ctrl_t callbacks.
 * When no controller is attached (NULL), commands succeed as no-ops.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_physics_ctrl.h"
#include "ferrum/editor/edit_dispatch.h"

/* ── cmd_physics_step ─────────────────────────────────────────────── */

bool cmd_physics_step(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)args;
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    edit_physics_ctrl_t *ctrl = ctx->physics;

    /* No controller → succeed as no-op. */
    if (!ctrl) { return true; }

    /* Step only works when paused. */
    if (ctrl->is_paused && !ctrl->is_paused(ctrl->user_data)) {
        return false;
    }

    if (ctrl->on_step) {
        ctrl->on_step(ctrl->user_data);
    }
    return true;
}

/* ── cmd_physics_reset ────────────────────────────────────────────── */

bool cmd_physics_reset(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena) {
    (void)args;
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    edit_physics_ctrl_t *ctrl = ctx->physics;

    if (!ctrl) { return true; }

    /* Reset zeroes velocities. */
    if (ctrl->on_reset) {
        ctrl->on_reset(ctrl->user_data);
    }

    /* Also pause after reset. */
    if (ctrl->on_pause) {
        ctrl->on_pause(ctrl->user_data);
    }
    return true;
}
