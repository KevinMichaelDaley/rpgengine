/**
 * @file cmd_group_ops.c
 * @brief Group operations — group, ungroup, select_group, group_info.
 *
 * Non-static functions: 4 (cmd_group, cmd_ungroup,
 *   cmd_select_group, cmd_group_info).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

/** @brief Extract a NUL-terminated group name from JSON. Returns false if
 *         name is missing, empty, or doesn't start with '&'. */
static bool extract_name_(const json_value_t *args, char *out, uint32_t cap) {
    const json_value_t *v = json_object_get(args, "name");
    if (!v || v->type != JSON_STRING || v->string.len == 0) return false;
    if (v->string.ptr[0] != '&') return false;
    uint32_t n = v->string.len;
    if (n >= cap) n = cap - 1;
    memcpy(out, v->string.ptr, n);
    out[n] = '\0';
    return true;
}

/** @brief Find or allocate a group slot. */
static edit_group_t *find_or_alloc_(edit_cmd_ctx_t *ctx, const char *name) {
    edit_group_t *existing = edit_cmd_find_group(ctx, name);
    if (existing) return existing;

    /* Lazy-allocate group array. */
    if (!ctx->groups) {
        ctx->groups = calloc(EDIT_GROUP_MAX, sizeof(edit_group_t));
        if (!ctx->groups) return NULL;
        ctx->group_capacity = EDIT_GROUP_MAX;
    }

    for (uint32_t i = 0; i < ctx->group_capacity; i++) {
        if (!ctx->groups[i].active) return &ctx->groups[i];
    }
    return NULL;
}

/** @brief Compute the centroid of the entities in ids[0..count). */
static void compute_pivot_(const edit_entity_store_t *store,
                           const uint32_t *ids, uint32_t count,
                           float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    uint32_t valid = 0;
    for (uint32_t i = 0; i < count; i++) {
        const edit_entity_t *e = edit_entity_store_get(store, ids[i]);
        if (!e) continue;
        out[0] += e->pos[0];
        out[1] += e->pos[1];
        out[2] += e->pos[2];
        valid++;
    }
    if (valid > 0) {
        float inv = 1.0f / (float)valid;
        out[0] *= inv;
        out[1] *= inv;
        out[2] *= inv;
    }
}

/* ── cmd_group ───────────────────────────────────────────────────── */

bool cmd_group(edit_dispatch_t *d, const json_value_t *args,
               json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->selection || !ctx->entities || !args) return false;

    /* Extract name (required, must start with &). */
    char name[EDIT_GROUP_NAME_MAX];
    if (!extract_name_(args, name, sizeof(name))) return false;

    /* Must have at least one entity selected. */
    uint32_t sel_count = edit_selection_count(ctx->selection);
    if (sel_count == 0) return false;

    /* Find or allocate slot. */
    edit_group_t *grp = find_or_alloc_(ctx, name);
    if (!grp) return false;

    /* Snapshot selection into the group. */
    const uint32_t *ids = edit_selection_ids(ctx->selection);
    uint32_t count = sel_count;
    if (count > EDIT_GROUP_ENTRY_MAX) count = EDIT_GROUP_ENTRY_MAX;
    memcpy(grp->ids, ids, count * sizeof(uint32_t));
    grp->count = count;
    memcpy(grp->name, name, strlen(name) + 1);
    grp->active = true;

    /* Pivot: explicit or computed from member positions. */
    const json_value_t *pivot_val = json_object_get(args, "pivot");
    if (pivot_val && pivot_val->type == JSON_ARRAY &&
        pivot_val->array.count >= 3) {
        grp->pivot[0] = (float)pivot_val->array.items[0].number;
        grp->pivot[1] = (float)pivot_val->array.items[1].number;
        grp->pivot[2] = (float)pivot_val->array.items[2].number;
    } else {
        compute_pivot_(ctx->entities, grp->ids, grp->count, grp->pivot);
    }

    /* Parent (optional). */
    grp->parent[0] = '\0';
    const json_value_t *parent_val = json_object_get(args, "parent");
    if (parent_val && parent_val->type == JSON_STRING &&
        parent_val->string.len > 0) {
        uint32_t plen = parent_val->string.len;
        if (plen >= sizeof(grp->parent)) plen = sizeof(grp->parent) - 1;
        memcpy(grp->parent, parent_val->string.ptr, plen);
        grp->parent[plen] = '\0';
    }

    /* Record undo entry. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {
            .forward_type  = EDIT_CMD_TYPE_GROUP_CREATE,
            .inverse_type  = EDIT_CMD_TYPE_GROUP_DELETE,
            .group_id      = 0,
            .entity_id     = 0,
            .snapshot_data = NULL,
            .snapshot_size = sizeof(edit_group_t),
        };
        memset(entry.delta, 0, sizeof(entry.delta));
        edit_undo_record(ctx->undo, &entry, grp, sizeof(edit_group_t));
    }

    result->type = JSON_NUMBER;
    result->number = (double)count;
    return true;
}

/* ── cmd_ungroup ─────────────────────────────────────────────────── */

bool cmd_ungroup(edit_dispatch_t *d, const json_value_t *args,
                 json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !args) return false;

    char name[EDIT_GROUP_NAME_MAX];
    if (!extract_name_(args, name, sizeof(name))) return false;

    edit_group_t *grp = edit_cmd_find_group(ctx, name);
    if (!grp) return false;

    /* Record undo entry before dissolving. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {
            .forward_type  = EDIT_CMD_TYPE_GROUP_DELETE,
            .inverse_type  = EDIT_CMD_TYPE_GROUP_CREATE,
            .group_id      = 0,
            .entity_id     = 0,
            .snapshot_data = NULL,
            .snapshot_size = sizeof(edit_group_t),
        };
        memset(entry.delta, 0, sizeof(entry.delta));
        edit_undo_record(ctx->undo, &entry, grp, sizeof(edit_group_t));
    }

    /* Dissolve. */
    grp->active = false;
    grp->name[0] = '\0';
    grp->count = 0;

    result->type = JSON_BOOL;
    result->boolean = true;
    return true;
}

/* ── cmd_select_group ────────────────────────────────────────────── */

bool cmd_select_group(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->selection || !args) return false;

    char name[EDIT_GROUP_NAME_MAX];
    if (!extract_name_(args, name, sizeof(name))) return false;

    edit_group_t *grp = edit_cmd_find_group(ctx, name);
    if (!grp) return false;

    /* Add all group members to selection. */
    for (uint32_t i = 0; i < grp->count; i++) {
        edit_selection_add(ctx->selection, grp->ids[i]);
    }

    result->type = JSON_NUMBER;
    result->number = (double)grp->count;
    return true;
}

/* ── cmd_group_info ──────────────────────────────────────────────── */

bool cmd_group_info(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !args) return false;

    char name[EDIT_GROUP_NAME_MAX];
    if (!extract_name_(args, name, sizeof(name))) return false;

    edit_group_t *grp = edit_cmd_find_group(ctx, name);
    if (!grp) return false;

    /* Build JSON string with info. */
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "{\"name\":\"%s\",\"count\":%u,"
                     "\"pivot\":[%.6g,%.6g,%.6g],"
                     "\"parent\":\"%s\"}",
                     grp->name, grp->count,
                     (double)grp->pivot[0], (double)grp->pivot[1],
                     (double)grp->pivot[2],
                     grp->parent);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return false;

    /* Copy to arena. */
    size_t str_sz = (size_t)n + 1;
    if (arena->used + str_sz > arena->cap) return false;
    char *str = (char *)(arena->buf + arena->used);
    arena->used += str_sz;
    memcpy(str, buf, str_sz);

    result->type = JSON_STRING;
    result->string.ptr = str;
    result->string.len = (uint32_t)n;
    return true;
}
