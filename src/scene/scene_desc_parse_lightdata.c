/**
 * @file scene_desc_parse_lightdata.c
 * @brief Parse the optional "lightmap" and "sdf" sections naming the baker's
 *        chunked light data (rpg-51nf). Absent sections leave empty prefixes.
 */
#include "scene_desc_internal.h"

bool scene_desc_parse_lightdata(const json_value_t *root, scene_desc_t *out)
{
    /* out was memset by the caller, so absent fields stay empty ("none"). */
    const json_value_t *lm = json_object_get(root, "lightmap");
    if (lm != NULL && lm->type == JSON_OBJECT) {
        sd_field_str(lm, "prefix", out->lightdata.lightmap_prefix, SCENE_DESC_PATH_CAP);
        out->lightdata.lightmap_perchunk = sd_field_bool(lm, "perchunk", false);
        sd_field_str(lm, "manifest", out->lightdata.lightmap_manifest, SCENE_DESC_PATH_CAP);
    }
    const json_value_t *sdf = json_object_get(root, "sdf");
    if (sdf != NULL && sdf->type == JSON_OBJECT) {
        sd_field_str(sdf, "prefix", out->lightdata.sdf_prefix, SCENE_DESC_PATH_CAP);
    }
    return true;
}
