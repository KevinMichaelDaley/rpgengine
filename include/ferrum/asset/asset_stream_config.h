/**
 * @file asset_stream_config.h
 * @brief Callback set + configuration for the asset streaming manager (rpg-nbp2).
 */
#ifndef FERRUM_ASSET_ASSET_STREAM_CONFIG_H
#define FERRUM_ASSET_ASSET_STREAM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "ferrum/asset/asset_stream_class.h"

struct job_system; /* ferrum/job/system.h */

/** Bitmask for fr_asset_stream_cbs.evict: which tier(s) to release. */
#define FR_ASSET_DROP_RAM  1
#define FR_ASSET_DROP_VRAM 2

/**
 * @brief Pluggable IO/GPU hooks the manager drives. All operate only on the
 *        per-asset @c slot_user payload, never on manager internals, so @c load
 *        is safe to run on a job fiber.
 */
typedef struct fr_asset_stream_cbs {
    /** Decode disk->RAM on a job fiber. Return RAM bytes loaded (0 = failure). */
    size_t (*load)(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user);
    /** Upload RAM->VRAM (e.g. queue GPU_CMD_CUSTOM), main thread. Return VRAM
     *  bytes (0 = skip). NULL, or a zero vram_budget, disables the VRAM tier. */
    size_t (*upload)(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user);
    /** Release @c drop (FR_ASSET_DROP_*) tiers of an evicted/removed asset. */
    void   (*evict)(void *user, uint64_t id, fr_asset_class_t cls, void *slot_user,
                    int drop);
} fr_asset_stream_cbs_t;

/**
 * @brief Streaming manager configuration.
 *
 * @c jobs NULL => loads run synchronously inline (headless/simple/testing).
 * @c ram_budget / @c vram_budget in bytes; 0 ram => unlimited; 0 vram => the
 * VRAM tier is disabled (headless server: RAM residency only).
 */
typedef struct fr_asset_stream_config {
    struct job_system    *jobs;
    size_t                ram_budget;
    size_t                vram_budget;
    uint32_t              max_in_flight;  /**< bounded concurrent loads (>=1). */
    uint32_t              capacity;       /**< max registered assets. */
    fr_asset_stream_cbs_t cbs;
    void                 *user;           /**< passed back to every callback. */
} fr_asset_stream_config_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ASSET_ASSET_STREAM_CONFIG_H */
