/**
 * @file cmd_group_mask.c
 * @brief Group mask utility functions for select commands.
 *
 * Provides helpers to resolve an optional group_mask argument and
 * check group membership. Used by select_all, select_regex,
 * select_near, select_touching, and select_fill.
 *
 * Non-static functions: 2 (edit_cmd_group_contains,
 *   edit_cmd_resolve_group_mask).
 */

#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/json_parse.h"

#include <string.h>

bool edit_cmd_group_contains(const edit_group_t *grp, uint32_t id) {
    if (!grp) return false;
    for (uint32_t i = 0; i < grp->count; i++) {
        if (grp->ids[i] == id) return true;
    }
    return false;
}

const edit_group_t *edit_cmd_resolve_group_mask(
    const edit_cmd_ctx_t *ctx, const json_value_t *args, bool *fail) {
    *fail = false;
    if (!args) return NULL;

    const json_value_t *mask_val = json_object_get(args, "group_mask");
    if (!mask_val || mask_val->type != JSON_STRING) return NULL;
    if (mask_val->string.len == 0) return NULL;

    /* Copy name to NUL-terminated buffer. */
    char name[EDIT_GROUP_NAME_MAX];
    uint32_t nlen = mask_val->string.len;
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, mask_val->string.ptr, nlen);
    name[nlen] = '\0';

    const edit_group_t *grp = edit_cmd_find_group(ctx, name);
    if (!grp) {
        *fail = true;
        return NULL;
    }
    return grp;
}
