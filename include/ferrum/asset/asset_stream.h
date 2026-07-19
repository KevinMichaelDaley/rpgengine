/**
 * @file asset_stream.h
 * @brief Prioritized two-tier asset streaming manager (rpg-nbp2).
 *
 * The streamer OWNS the priority: it keeps a priority-ordered set of registered
 * assets and admits them into a RAM residency budget (disk->RAM decode) and a
 * VRAM residency budget (RAM->VRAM upload), always working from the highest
 * priority down and evicting the lowest-priority resident under pressure. The
 * job system is only the async executor for individual loads (bounded in-flight,
 * refilled from the top => effective completion order == priority order). This
 * manager has no GL: GPU work is done in the caller's upload/evict callbacks.
 *
 * Threading: fr_asset_stream_tick() must be called from a single owner thread;
 * load callbacks run on job fibers. Slot addresses are stable (never moved), so
 * an in-flight load's slot pointer stays valid.
 */
#ifndef FERRUM_ASSET_ASSET_STREAM_H
#define FERRUM_ASSET_ASSET_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/asset/asset_stream_class.h"
#include "ferrum/asset/asset_stream_config.h"

struct fr_asset_slot; /* internal (asset_stream_internal.h) */

/**
 * @brief The streaming manager. Treat fields as read-only from outside.
 */
typedef struct fr_asset_stream {
    fr_asset_stream_config_t cfg;
    struct fr_asset_slot    *slots;     /**< [cfg.capacity], owned (malloc). */
    uint32_t                 count;      /**< used slots. */
    size_t                   ram_used;   /**< bytes of RAM-resident payloads. */
    size_t                   ram_reserved; /**< bytes reserved for in-flight loads. */
    size_t                   vram_used;  /**< bytes of VRAM-resident payloads. */
    uint32_t                 in_flight;  /**< loads currently dispatched. */
    uint64_t                 clock;      /**< monotonic LRU stamp. */
} fr_asset_stream_t;

/** Initialize (allocates the slot table). @return false on bad cfg / OOM. */
bool fr_asset_stream_init(fr_asset_stream_t *s, const fr_asset_stream_config_t *cfg);
/** Free the slot table (does NOT call evict on residents; caller drains first). */
void fr_asset_stream_destroy(fr_asset_stream_t *s);

/** Register an asset (ABSENT). @c ram_size/@c vram_size are budget estimates,
 *  @c slot_user is passed to the callbacks. @return false if full or id exists. */
bool fr_asset_stream_add(fr_asset_stream_t *s, uint64_t id, fr_asset_class_t cls,
                         size_t ram_size, size_t vram_size, int priority,
                         void *slot_user);
/** Unregister + evict an asset. @return false if unknown or currently LOADING. */
bool fr_asset_stream_remove(fr_asset_stream_t *s, uint64_t id);

/** Change an asset's priority (re-orders admission / eviction). */
bool fr_asset_stream_set_priority(fr_asset_stream_t *s, uint64_t id, int priority);
/** Mark an asset used this frame (refreshes its LRU stamp). */
bool fr_asset_stream_touch(fr_asset_stream_t *s, uint64_t id);

/** Drive one step: harvest completed loads, admit top-priority within budget,
 *  evict as needed. Owner thread only. */
void fr_asset_stream_tick(fr_asset_stream_t *s);

/** Query residency of an asset (FR_RESIDENCY_ABSENT if unknown). */
fr_asset_residency_t fr_asset_stream_residency(const fr_asset_stream_t *s, uint64_t id);
/** Bytes currently RAM-resident. */
size_t fr_asset_stream_ram_used(const fr_asset_stream_t *s);
/** Bytes currently VRAM-resident. */
size_t fr_asset_stream_vram_used(const fr_asset_stream_t *s);
/** Count of assets in RAM or VRAM residency. */
uint32_t fr_asset_stream_resident_count(const fr_asset_stream_t *s);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ASSET_ASSET_STREAM_H */
