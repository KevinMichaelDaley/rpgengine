/**
 * @file scene_desc_parse_probes.c
 * @brief Parse the optional "probes" section: base spacing, optional manual
 *        probe file, and AABB importance boxes (rpg-51nf / rpg-ft0g). Absent =
 *        engine-default auto-grid.
 */
#include "scene_desc_internal.h"

bool scene_desc_parse_probes(const json_value_t *root, scene_desc_t *out)
{
    /* Defaults (spacing/vspacing 0 => engine default, no manual, no boxes) come
     * from the caller's memset; only override what the section provides. */
    const json_value_t *p = json_object_get(root, "probes");
    if (p == NULL || p->type != JSON_OBJECT) return true; /* optional */

    out->probes.spacing = sd_field_num(p, "spacing", 0.0f);
    out->probes.vspacing = sd_field_num(p, "vspacing", 0.0f);

    const json_value_t *manual = json_object_get(p, "manual");
    if (manual != NULL && manual->type == JSON_STRING &&
        json_string_copy(manual, out->probes.manual_path, SCENE_DESC_PATH_CAP)) {
        out->probes.has_manual = (out->probes.manual_path[0] != '\0');
    }

    const json_value_t *imp = json_object_get(p, "importance");
    if (imp != NULL && imp->type == JSON_ARRAY) {
        uint32_t n = imp->array.count;
        if (n > SCENE_DESC_MAX_IMPORTANCE) n = SCENE_DESC_MAX_IMPORTANCE;
        for (uint32_t i = 0; i < n; ++i) {
            const json_value_t *b = json_array_get(imp, i);
            scene_desc_importance_box_t *box = &out->probes.boxes[i];
            box->density_mult = 1.0f;   /* neutral defaults */
            box->priority_bias = 0.0f;
            if (b != NULL && b->type == JSON_OBJECT) {
                sd_field_vec(b, "min", box->min, 3);
                sd_field_vec(b, "max", box->max, 3);
                box->density_mult = sd_field_num(b, "density", 1.0f);
                box->priority_bias = sd_field_num(b, "priority", 0.0f);
            }
            out->probes.box_count++;
        }
    }
    return true;
}
