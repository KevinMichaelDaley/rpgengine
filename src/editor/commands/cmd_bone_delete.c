/**
 * @file cmd_bone_delete.c
 * @brief Delete a bone from the active skeleton.
 *
 * JSON args: {"bone":<idx>, "skel":"<path>"}
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_bone_delete
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/anim/skeleton_builder.h"

#include <string.h>

bool cmd_bone_delete(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->skeleton_registry || !args) return false;

    const json_value_t *b = json_object_get(args, "bone");
    if (!b || b->type != JSON_NUMBER) return false;
    uint32_t bone_idx = (uint32_t)b->number;

    const json_value_t *sk = json_object_get(args, "skel");
    if (!sk || sk->type != JSON_STRING || sk->string.len == 0) return false;

    char skel_path[256];
    uint32_t slen = sk->string.len;
    if (slen >= sizeof(skel_path)) slen = sizeof(skel_path) - 1;
    memcpy(skel_path, sk->string.ptr, slen);
    skel_path[slen] = '\0';

    bool ok = skeleton_builder_remove_bone(ctx->skeleton_registry,
                                            skel_path, bone_idx);
    result->type    = JSON_BOOL;
    result->boolean = ok;
    return ok;
}
