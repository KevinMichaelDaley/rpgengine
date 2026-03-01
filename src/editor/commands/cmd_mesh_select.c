/**
 * @file cmd_mesh_select.c
 * @brief Mesh element selection commands.
 *
 * JSON args:
 *   mesh_select:       {"indices":[0,1,2,...]}
 *   mesh_deselect:     {"indices":[0,1,2,...]}
 *   mesh_select_all:   {}
 *   mesh_deselect_all: {}
 *   mesh_info:         {}
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Get the active selection bitset based on current mode. */
static mesh_sel_bitset_t *active_sel_(edit_cmd_ctx_t *ctx) {
    if (!ctx || !ctx->mesh) return NULL;
    switch (ctx->mesh->mode) {
    case MESH_SEL_MODE_VERTEX:    return &ctx->mesh->sel_vertices;
    case MESH_SEL_MODE_EDGE:      return &ctx->mesh->sel_edges;
    case MESH_SEL_MODE_FACE:
    case MESH_SEL_MODE_POLYGROUP: return &ctx->mesh->sel_faces;
    default:                      return &ctx->mesh->sel_faces;
    }
}

/* ------------------------------------------------------------------ */
/* mesh_select                                                         */
/* ------------------------------------------------------------------ */

bool cmd_mesh_select(edit_dispatch_t *d, const json_value_t *args,
                     json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    mesh_sel_bitset_t *sel = active_sel_(ctx);
    if (!sel) return false;

    if (args) {
        const json_value_t *indices = json_object_get(args, "indices");
        if (indices && indices->type == JSON_ARRAY) {
            for (uint32_t i = 0; i < indices->array.count; i++) {
                if (indices->array.items[i].type == JSON_NUMBER) {
                    uint32_t idx = (uint32_t)indices->array.items[i].number;
                    mesh_sel_bitset_set(sel, idx);
                }
            }
        }
    }

    result->type = JSON_NUMBER; result->number = (double)sel->count; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_deselect                                                       */
/* ------------------------------------------------------------------ */

bool cmd_mesh_deselect(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    mesh_sel_bitset_t *sel = active_sel_(ctx);
    if (!sel) return false;

    if (args) {
        const json_value_t *indices = json_object_get(args, "indices");
        if (indices && indices->type == JSON_ARRAY) {
            for (uint32_t i = 0; i < indices->array.count; i++) {
                if (indices->array.items[i].type == JSON_NUMBER) {
                    uint32_t idx = (uint32_t)indices->array.items[i].number;
                    mesh_sel_bitset_unset(sel, idx);
                }
            }
        }
    }

    result->type = JSON_NUMBER; result->number = (double)sel->count; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_select_all                                                     */
/* ------------------------------------------------------------------ */

bool cmd_mesh_select_all(edit_dispatch_t *d, const json_value_t *args,
                         json_value_t *result, json_arena_t *arena) {
    (void)args;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    mesh_sel_bitset_t *sel = active_sel_(ctx);
    if (!sel || !slot) return false;

    uint32_t count = 0;
    switch (ctx->mesh->mode) {
    case MESH_SEL_MODE_VERTEX:    count = slot->vertex_count; break;
    case MESH_SEL_MODE_FACE:
    case MESH_SEL_MODE_POLYGROUP: count = slot->index_count / 3; break;
    default:                      count = slot->index_count / 3; break;
    }

    for (uint32_t i = 0; i < count; i++) {
        mesh_sel_bitset_set(sel, i);
    }

    result->type = JSON_NUMBER; result->number = (double)sel->count; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_deselect_all                                                   */
/* ------------------------------------------------------------------ */

bool cmd_mesh_deselect_all(edit_dispatch_t *d, const json_value_t *args,
                           json_value_t *result, json_arena_t *arena) {
    (void)args;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_sel_bitset_clear_all(&ctx->mesh->sel_vertices);
    mesh_sel_bitset_clear_all(&ctx->mesh->sel_edges);
    mesh_sel_bitset_clear_all(&ctx->mesh->sel_faces);

    result->type = JSON_NULL; (void)arena;
    return true;
}

/* ------------------------------------------------------------------ */
/* mesh_info — return current mesh stats                               */
/* ------------------------------------------------------------------ */

bool cmd_mesh_info(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)args;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->mesh) return false;

    mesh_slot_t *slot = mesh_edit_get_active_slot(ctx->mesh);
    if (!slot) return false;

    /* Return a simple string with stats — allocate from arena. */
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "verts=%u tris=%u mode=%d sel_v=%u sel_e=%u sel_f=%u",
                     slot->vertex_count, slot->index_count / 3,
                     (int)ctx->mesh->mode,
                     ctx->mesh->sel_vertices.count,
                     ctx->mesh->sel_edges.count,
                     ctx->mesh->sel_faces.count);
    /* Copy string into arena so it outlives the stack buffer. */
    if ((size_t)n + arena->used > arena->cap) return false;
    char *dst = (char *)(arena->buf + arena->used);
    memcpy(dst, buf, (size_t)n);
    arena->used += (size_t)n;
    result->type       = JSON_STRING;
    result->string.ptr = dst;
    result->string.len = (uint32_t)n;
    return true;
}
