/**
 * @file cmd_physics_pause.c
 * @brief Physics pause and resume editor commands.
 *
 * Both commands delegate to the physics controller interface
 * (edit_physics_ctrl_t) which the host wires to the tick runner.
 * When no controller is attached (NULL), commands succeed as no-ops.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_physics_ctrl.h"
#include "ferrum/editor/edit_dispatch.h"

/* ── cmd_physics_pause ────────────────────────────────────────────── */

bool cmd_physics_pause(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena) {
    (void)args; (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    edit_physics_ctrl_t *ctrl = ctx->physics;

    if (ctrl && ctrl->on_pause) {
        ctrl->on_pause(ctrl->user_data);
    }
    return true;
}

/* ── cmd_physics_resume ───────────────────────────────────────────── */

bool cmd_physics_resume(edit_dispatch_t *d, const json_value_t *args,
                        json_value_t *result, json_arena_t *arena) {
    (void)args; (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    edit_physics_ctrl_t *ctrl = ctx->physics;

    if (ctrl && ctrl->on_resume) {
        ctrl->on_resume(ctrl->user_data);
    }
    return true;
}
