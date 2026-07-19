/**
 * @file asset_stream_class.h
 * @brief Asset class + residency state enums for the streaming manager (rpg-nbp2).
 */
#ifndef FERRUM_ASSET_ASSET_STREAM_CLASS_H
#define FERRUM_ASSET_ASSET_STREAM_CLASS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What kind of asset a streamed slot holds.
 *
 * ONE-SHOT assets (mesh/skeleton/texture/collider/probe) are loaded once and
 * resident until unloaded. The two LIGHT-DATA chunk classes are the spatially
 * chunk-paged baked data (lightmap SH / SDF-albedo-voxel), keyed by a world box.
 */
typedef enum fr_asset_class {
    FR_ASSET_MESH = 0,
    FR_ASSET_SKELETON,
    FR_ASSET_TEXTURE,
    FR_ASSET_COLLIDER,
    FR_ASSET_LIGHTMAP_CHUNK,
    FR_ASSET_SDF_CHUNK,
    FR_ASSET_PROBE,
    FR_ASSET_GENERIC,
    FR_ASSET_CLASS_COUNT
} fr_asset_class_t;

/**
 * @brief Residency state of a streamed asset.
 */
typedef enum fr_asset_residency {
    FR_RESIDENCY_ABSENT = 0, /**< registered, not loaded (in the request queue). */
    FR_RESIDENCY_LOADING,    /**< a load job is in flight. */
    FR_RESIDENCY_RAM,        /**< decoded, resident in RAM. */
    FR_RESIDENCY_VRAM,       /**< uploaded to VRAM (RAM copy freed). */
    FR_RESIDENCY_ERROR       /**< the load failed. */
} fr_asset_residency_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ASSET_ASSET_STREAM_CLASS_H */
