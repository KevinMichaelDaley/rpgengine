/**
 * @file cmd_list_entities.c
 * @brief List active entities with pagination.
 *
 * Returns a paginated JSON result:
 *   {"entities":[{...},...], "total":N, "offset":O}
 *
 * Optional args:
 *   "pattern" — POSIX extended regex filter on entity names
 *   "offset"  — skip first N matching entities (default 0)
 *   "limit"   — max entities per page (default 200)
 *
 * The client iterates pages by advancing offset until
 * offset + returned_count >= total.
 *
 * Entity objects include ALL fields (static + dynamic attrs) via
 * edit_entity_json_build().
 *
 * Non-static functions: cmd_list_entities (1).
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_json.h"
#include <regex.h>
#include <string.h>

/** Default and maximum entities per page. */
#define LIST_DEFAULT_LIMIT 200
#define LIST_MAX_LIMIT     500

/** Alignment helper. */
#define ALIGN8(x) (((x) + 7u) & ~(size_t)7u)

/**
 * @brief Check if an entity matches the filter criteria.
 * @return true if entity should be included in results.
 */
static bool matches_(const edit_entity_t *ent, bool has_pattern,
                     const regex_t *regex) {
    if (!ent->active) return false;
    if (!has_pattern) return true;
    if (ent->name[0] == '\0') return false;
    return regexec(regex, ent->name, 0, NULL, 0) == 0;
}

bool cmd_list_entities(edit_dispatch_t *d, const json_value_t *args,
                       json_value_t *result, json_arena_t *arena) {
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities) return false;

    const edit_entity_store_t *store = ctx->entities;

    /* Extract optional pattern. */
    const json_value_t *pat_val = json_object_get(args, "pattern");
    regex_t regex;
    bool has_pattern = false;

    if (pat_val && pat_val->type == JSON_STRING && pat_val->string.len > 0) {
        char pat_buf[256];
        if (!json_string_copy(pat_val, pat_buf, sizeof(pat_buf))) return false;
        int rc = regcomp(&regex, pat_buf, REG_EXTENDED | REG_NOSUB);
        if (rc != 0) return false;
        has_pattern = true;
    }

    /* Extract pagination args. */
    uint32_t req_offset = 0;
    uint32_t req_limit = LIST_DEFAULT_LIMIT;
    if (args) {
        const json_value_t *off_val = json_object_get(args, "offset");
        if (off_val && off_val->type == JSON_NUMBER && off_val->number >= 0) {
            req_offset = (uint32_t)off_val->number;
        }
        const json_value_t *lim_val = json_object_get(args, "limit");
        if (lim_val && lim_val->type == JSON_NUMBER && lim_val->number > 0) {
            req_limit = (uint32_t)lim_val->number;
            if (req_limit > LIST_MAX_LIMIT) req_limit = LIST_MAX_LIMIT;
        }
    }

    /* Count total matching entities. */
    uint32_t total_count = 0;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (matches_(&store->entities[i], has_pattern, &regex)) {
            total_count++;
        }
    }

    /* Determine page size. */
    uint32_t page_count = 0;
    if (req_offset < total_count) {
        page_count = total_count - req_offset;
        if (page_count > req_limit) page_count = req_limit;
    }

    /* Allocate items array + wrapper from arena. */
    size_t items_sz     = ALIGN8(page_count * sizeof(json_value_t));
    size_t wrap_keys_sz = ALIGN8(3 * sizeof(const char *));
    size_t wrap_klens_sz = ALIGN8(3 * sizeof(uint32_t));
    size_t wrap_vals_sz = ALIGN8(3 * sizeof(json_value_t));
    size_t wrapper_total = items_sz + wrap_keys_sz + wrap_klens_sz + wrap_vals_sz;

    if (arena->used + wrapper_total > arena->cap) {
        if (has_pattern) regfree(&regex);
        return false;
    }

    json_value_t *items = (json_value_t *)(arena->buf + arena->used);
    arena->used += items_sz;
    const char **wrap_keys = (const char **)(arena->buf + arena->used);
    arena->used += wrap_keys_sz;
    uint32_t *wrap_klens = (uint32_t *)(arena->buf + arena->used);
    arena->used += wrap_klens_sz;
    json_value_t *wrap_vals = (json_value_t *)(arena->buf + arena->used);
    arena->used += wrap_vals_sz;

    /* Build entity objects for this page using shared serializer. */
    uint32_t idx = 0;
    uint32_t match_idx = 0;
    for (uint32_t i = 0; i < store->capacity && idx < page_count; i++) {
        if (!matches_(&store->entities[i], has_pattern, &regex)) continue;

        if (match_idx < req_offset) {
            match_idx++;
            continue;
        }
        match_idx++;

        if (!edit_entity_json_build(&store->entities[i], i,
                                     &items[idx], arena)) {
            if (has_pattern) regfree(&regex);
            return false;
        }
        idx++;
    }

    if (has_pattern) regfree(&regex);

    /* Build wrapper object: {"entities":[...], "total":N, "offset":N} */
    wrap_keys[0] = "entities";  wrap_klens[0] = 8;
    wrap_keys[1] = "total";     wrap_klens[1] = 5;
    wrap_keys[2] = "offset";    wrap_klens[2] = 6;

    wrap_vals[0].type = JSON_ARRAY;
    wrap_vals[0].array.items = items;
    wrap_vals[0].array.count = idx;

    wrap_vals[1].type = JSON_NUMBER;
    wrap_vals[1].number = (double)total_count;

    wrap_vals[2].type = JSON_NUMBER;
    wrap_vals[2].number = (double)req_offset;

    result->type = JSON_OBJECT;
    result->object.keys = wrap_keys;
    result->object.key_lens = wrap_klens;
    result->object.vals = wrap_vals;
    result->object.count = 3;
    return true;
}
