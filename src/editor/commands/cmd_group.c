/**
 * @file cmd_group.c
 * @brief Selection group commands — save, delete, list.
 *
 * Groups are named sets of entity IDs, prefixed with '&'.
 * group_save:   snapshot current selection into a named group.
 * group_delete: remove a named group.
 * group_list:   list all groups with their entity counts.
 *
 * Non-static functions: 4 per file (this file + cmd_group_mask.c).
 * This file: cmd_group_save, cmd_group_delete, cmd_group_list,
 *   edit_cmd_find_group.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/json_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Group lookup ────────────────────────────────────────────────── */

edit_group_t *edit_cmd_find_group(const edit_cmd_ctx_t *ctx,
                                  const char *name) {
    if (!ctx || !ctx->groups || !name) return NULL;
    for (uint32_t i = 0; i < ctx->group_capacity; i++) {
        if (ctx->groups[i].active &&
            strcmp(ctx->groups[i].name, name) == 0) {
            return &ctx->groups[i];
        }
    }
    return NULL;
}

/** Find or allocate a group slot (for saving). */
static edit_group_t *find_or_alloc_slot_(edit_cmd_ctx_t *ctx,
                                          const char *name) {
    /* Check for existing group with same name. */
    edit_group_t *existing = edit_cmd_find_group(ctx, name);
    if (existing) return existing;

    /* Lazy-allocate group array. */
    if (!ctx->groups) {
        ctx->groups = calloc(EDIT_GROUP_MAX, sizeof(edit_group_t));
        if (!ctx->groups) return NULL;
        ctx->group_capacity = EDIT_GROUP_MAX;
    }

    /* Find first inactive slot. */
    for (uint32_t i = 0; i < ctx->group_capacity; i++) {
        if (!ctx->groups[i].active) return &ctx->groups[i];
    }
    return NULL; /* All slots full. */
}

/* ── group_save ──────────────────────────────────────────────────── */

bool cmd_group_save(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->selection || !args) return false;

    /* Extract name (required, must start with &). */
    const json_value_t *name_val = json_object_get(args, "name");
    if (!name_val || name_val->type != JSON_STRING) return false;
    if (name_val->string.len == 0 || name_val->string.ptr[0] != '&')
        return false;

    /* Must have at least one entity selected. */
    uint32_t sel_count = edit_selection_count(ctx->selection);
    if (sel_count == 0) return false;

    /* Copy name to a NUL-terminated buffer. */
    char name[EDIT_GROUP_NAME_MAX];
    uint32_t nlen = name_val->string.len;
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, name_val->string.ptr, nlen);
    name[nlen] = '\0';

    /* Find or allocate slot. */
    edit_group_t *grp = find_or_alloc_slot_(ctx, name);
    if (!grp) return false;

    /* Snapshot current selection into the group. */
    const uint32_t *ids = edit_selection_ids(ctx->selection);
    uint32_t count = sel_count;
    if (count > EDIT_GROUP_ENTRY_MAX) count = EDIT_GROUP_ENTRY_MAX;
    memcpy(grp->ids, ids, count * sizeof(uint32_t));
    grp->count = count;
    memcpy(grp->name, name, nlen + 1);
    grp->active = true;

    result->type = JSON_NUMBER;
    result->number = (double)count;
    return true;
}

/* ── group_delete ────────────────────────────────────────────────── */

bool cmd_group_delete(edit_dispatch_t *d, const json_value_t *args,
                      json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !args) return false;

    const json_value_t *name_val = json_object_get(args, "name");
    if (!name_val || name_val->type != JSON_STRING) return false;

    char name[EDIT_GROUP_NAME_MAX];
    uint32_t nlen = name_val->string.len;
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, name_val->string.ptr, nlen);
    name[nlen] = '\0';

    edit_group_t *grp = edit_cmd_find_group(ctx, name);
    if (!grp) return false;

    grp->active = false;
    grp->name[0] = '\0';
    grp->count = 0;
    return true;
}

/* ── group_list ──────────────────────────────────────────────────── */

bool cmd_group_list(edit_dispatch_t *d, const json_value_t *args,
                    json_value_t *result, json_arena_t *arena) {
    (void)args;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx) return false;

    /* Count active groups. */
    uint32_t active = 0;
    if (ctx->groups) {
        for (uint32_t i = 0; i < ctx->group_capacity; i++) {
            if (ctx->groups[i].active) active++;
        }
    }

    if (active == 0) {
        /* Return empty array. */
        result->type = JSON_ARRAY;
        result->array.items = NULL;
        result->array.count = 0;
        return true;
    }

    /* Allocate array of group info strings. */
    size_t items_sz = active * sizeof(json_value_t);
    if (arena->used + items_sz > arena->cap) return false;
    json_value_t *elems = (json_value_t *)(arena->buf + arena->used);
    arena->used += items_sz;

    uint32_t idx = 0;
    for (uint32_t i = 0; i < ctx->group_capacity && idx < active; i++) {
        if (!ctx->groups[i].active) continue;

        /* Build a simple string with "name(count)" format. */
        char buf[128];
        int n = snprintf(buf, sizeof(buf), "%s(%u)",
                         ctx->groups[i].name, ctx->groups[i].count);
        if (n <= 0) continue;

        size_t str_sz = (size_t)n + 1;
        if (arena->used + str_sz > arena->cap) break;
        char *str = (char *)(arena->buf + arena->used);
        arena->used += str_sz;
        memcpy(str, buf, str_sz);

        elems[idx].type = JSON_STRING;
        elems[idx].string.ptr = str;
        elems[idx].string.len = (uint32_t)n;
        idx++;
    }

    result->type = JSON_ARRAY;
    result->array.items = elems;
    result->array.count = idx;
    return true;
}
