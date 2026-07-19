/**
 * @file scene_desc_parse.c
 * @brief Top-level scene-descriptor parse (rpg-51nf): JSON -> scene_desc_t via
 *        the per-section sub-parsers. Carves the JSON tree arena from the
 *        caller's arena so no malloc is used.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "scene_desc_internal.h"

bool scene_desc_parse(const char *json, size_t len, struct arena *arena,
                      scene_desc_t *out)
{
    if (json == NULL || arena == NULL || out == NULL) return false;
    memset(out, 0, sizeof *out);

    /* JSON node tree can be several times the text size; carve scratch from the
     * caller arena (bounded, no malloc). Sized generously for node overhead. */
    size_t jsize = len * 12u + 4096u;
    void *jbuf = arena_alloc((arena_t *)arena, 16u, jsize);
    if (jbuf == NULL) return false;   /* arena too small -> clean failure */
    json_arena_t ja;
    json_arena_init(&ja, jbuf, jsize);

    json_value_t root;
    if (!json_parse(json, len, &ja, &root)) return false;
    if (root.type != JSON_OBJECT) return false;   /* level root must be an object */

    /* Accept both the pipeline key ("name") and the Blender exporter key. */
    sd_field_str(&root, "name", out->name, SCENE_DESC_OBJ_NAME_CAP);
    if (out->name[0] == '\0')
        sd_field_str(&root, "collection", out->name, SCENE_DESC_OBJ_NAME_CAP);

    if (!scene_desc_parse_materials(&root, out)) return false;
    if (!scene_desc_parse_objects(&root, arena, out)) return false; /* required */
    if (!scene_desc_parse_lightdata(&root, out)) return false;
    if (!scene_desc_parse_probes(&root, out)) return false;
    return true;
}
