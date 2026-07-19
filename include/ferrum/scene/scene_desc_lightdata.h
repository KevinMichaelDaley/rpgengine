/**
 * @file scene_desc_lightdata.h
 * @brief Descriptor references to the baker-generated CHUNKED light data (rpg-51nf).
 *
 * The baker emits, per spatial chunk, a lightmap SH set (<prefix>.flm and/or
 * <prefix>_cNNN.flm + <prefix>_manifest.bin) and an SDF/albedo-voxel set
 * (<prefix>_cNNN.sdf, RGBA32F, rgb=albedo / a=distance), with the far-field
 * distant-reflector contribution folded in so each chunk is self-contained. This
 * struct only names those on-disk sets; residency/streaming is handled elsewhere
 * (the asset streamer). Pure data, no GL.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_LIGHTDATA_H
#define FERRUM_SCENE_SCENE_DESC_LIGHTDATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ferrum/scene/scene_desc_object.h" /* SCENE_DESC_PATH_CAP */

/**
 * @brief On-disk references to a level's baked light data.
 *
 * All paths are relative to the descriptor's directory and always
 * null-terminated; an empty string means "absent" (the section was omitted).
 */
typedef struct scene_desc_lightdata {
    char lightmap_prefix[SCENE_DESC_PATH_CAP];   /**< <prefix>.flm / <prefix>_cNNN.flm (empty = none). */
    bool lightmap_perchunk;                      /**< true = per-chunk .flm pages + manifest. */
    char lightmap_manifest[SCENE_DESC_PATH_CAP]; /**< per-mesh layer/atlas-rect table (empty = none). */
    char sdf_prefix[SCENE_DESC_PATH_CAP];        /**< <prefix>_cNNN.sdf albedo-voxel/SDF chunks (empty = none). */
} scene_desc_lightdata_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_LIGHTDATA_H */
