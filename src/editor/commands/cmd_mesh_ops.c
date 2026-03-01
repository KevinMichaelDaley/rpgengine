/**
 * @file cmd_mesh_ops.c
 * @brief Mesh editing operation commands: mode, extrude, inset, bevel.
 *
 * JSON args:
 *   mesh_mode:     {"mode":"vertex"|"edge"|"face"|"polygroup"|"object"}
 *   extrude:       {"distance":1.0, "direction":[0,1,0]}
 *   inset:         {"amount":0.5, "depth":0.0}
 *   bevel:         {"amount":0.5}
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_extrude.h"
#include "ferrum/editor/mesh/mesh_inset.h"
#include "ferrum/editor/mesh/mesh_bevel.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* mesh_mode                                                           */
/* ------------------------------------------------------------------ */

bool cmd_mesh_mode(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_sel_mode_t mode = MESH_SEL_MODE_FACE; /* default */
    if (args) {
        const json_value_t *m = json_object_get(args, "mode");
        if (m && m->type == JSON_STRING) {
            if (m->string.len == 6 && memcmp(m->string.ptr, "vertex", 6) == 0)
                mode = MESH_SEL_MODE_VERTEX;
            else if (m->string.len == 4 && memcmp(m->string.ptr, "edge", 4) == 0)
                mode = MESH_SEL_MODE_EDGE;
            else if (m->string.len == 4 && memcmp(m->string.ptr, "face", 4) == 0)
                mode = MESH_SEL_MODE_FACE;
            else if (m->string.len == 9 && memcmp(m->string.ptr, "polygroup", 9) == 0)
                mode = MESH_SEL_MODE_POLYGROUP;
            else if (m->string.len == 6 && memcmp(m->string.ptr, "object", 6) == 0)
                mode = MESH_SEL_MODE_OBJECT;
        }
        /* Also accept numeric mode */
        if (m && m->type == JSON_NUMBER) {
            int v = (int)m->number;
            if (v >= 0 && v < MESH_SEL_MODE_COUNT) mode = (mesh_sel_mode_t)v;
        }
    }

    mesh_edit_set_mode(ctx->mesh, mode);
    return true;
}

/* ------------------------------------------------------------------ */
/* extrude                                                             */
/* ------------------------------------------------------------------ */

bool cmd_extrude(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    float distance = 1.0f;
    float dir[3] = {0, 0, 0};
    bool has_dir = false;

    if (args) {
        const json_value_t *dist = json_object_get(args, "distance");
        if (dist && dist->type == JSON_NUMBER) distance = (float)dist->number;

        const json_value_t *d_arr = json_object_get(args, "direction");
        if (d_arr && d_arr->type == JSON_ARRAY && d_arr->array.count >= 3) {
            for (int i = 0; i < 3; i++)
                dir[i] = (float)d_arr->array.items[i].number;
            has_dir = true;
        }
    }

    return mesh_extrude(slot, &ctx->mesh->sel_faces, distance,
                        has_dir ? dir : NULL);
}

/* ------------------------------------------------------------------ */
/* inset                                                               */
/* ------------------------------------------------------------------ */

bool cmd_inset(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    float amount = 0.5f;
    float depth = 0.0f;
    if (args) {
        const json_value_t *a = json_object_get(args, "amount");
        if (a && a->type == JSON_NUMBER) amount = (float)a->number;
        const json_value_t *dp = json_object_get(args, "depth");
        if (dp && dp->type == JSON_NUMBER) depth = (float)dp->number;
    }

    return mesh_inset(slot, &ctx->mesh->sel_faces, amount, depth);
}

/* ------------------------------------------------------------------ */
/* bevel                                                               */
/* ------------------------------------------------------------------ */

bool cmd_bevel(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena) {
    (void)result; (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    float amount = 0.5f;
    if (args) {
        const json_value_t *a = json_object_get(args, "amount");
        if (a && a->type == JSON_NUMBER) amount = (float)a->number;
    }

    /* Vertex bevel in vertex mode, otherwise not yet supported */
    if (ctx->mesh->mode == MESH_SEL_MODE_VERTEX) {
        return mesh_bevel_vertices(slot, &ctx->mesh->sel_vertices, amount);
    }
    return false;
}
