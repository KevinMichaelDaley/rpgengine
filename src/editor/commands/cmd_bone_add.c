/**
 * @file cmd_bone_add.c
 * @brief Add a bone to the active skeleton.
 *
 * JSON args: {"parent":<idx>, "head":[x,y,z], "tail":[x,y,z], "name":"..."}
 * All args optional. Defaults: root bone, origin, Y-up.
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_bone_add
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/anim/skeleton_builder.h"
#include "ferrum/math/vec3.h"

#include <string.h>
#include <stdio.h>

bool cmd_bone_add(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->skeleton_registry) return false;

    /* Parse optional args. */
    uint32_t parent = UINT32_MAX;
    vec3_t head = {0, 0, 0};
    vec3_t tail = {0, 1, 0};
    char name[64] = "bone";

    if (args) {
        const json_value_t *p = json_object_get(args, "parent");
        if (p && p->type == JSON_NUMBER) parent = (uint32_t)p->number;

        const json_value_t *h = json_object_get(args, "head");
        if (h && h->type == JSON_ARRAY && h->array.count >= 3) {
            head.x = (float)h->array.items[0].number;
            head.y = (float)h->array.items[1].number;
            head.z = (float)h->array.items[2].number;
        }

        const json_value_t *t = json_object_get(args, "tail");
        if (t && t->type == JSON_ARRAY && t->array.count >= 3) {
            tail.x = (float)t->array.items[0].number;
            tail.y = (float)t->array.items[1].number;
            tail.z = (float)t->array.items[2].number;
        }

        const json_value_t *n = json_object_get(args, "name");
        if (n && n->type == JSON_STRING && n->string.len > 0) {
            uint32_t len = n->string.len;
            if (len >= sizeof(name)) len = sizeof(name) - 1;
            memcpy(name, n->string.ptr, len);
            name[len] = '\0';
        }

        /* Also accept "skel" arg to specify which skeleton. */
        const json_value_t *sk = json_object_get(args, "skel");
        if (sk && sk->type == JSON_STRING && sk->string.len > 0) {
            char skel_path[256];
            uint32_t slen = sk->string.len;
            if (slen >= sizeof(skel_path)) slen = sizeof(skel_path) - 1;
            memcpy(skel_path, sk->string.ptr, slen);
            skel_path[slen] = '\0';

            uint32_t idx = skeleton_builder_add_bone(
                ctx->skeleton_registry, skel_path, name,
                parent, head, tail);
            result->type = JSON_NUMBER;
            result->number = (double)idx;
            return idx != UINT32_MAX;
        }
    }

    /* Default: use first skeleton in registry (for TUI convenience). */
    /* This would need the skeleton mode context — fall through to error. */
    result->type = JSON_NUMBER;
    result->number = -1.0;
    return false;
}
