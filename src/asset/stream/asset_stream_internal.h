/**
 * @file asset_stream_internal.h
 * @brief Private slot type + cross-TU helpers for the streaming manager (rpg-nbp2).
 */
#ifndef FERRUM_ASSET_STREAM_INTERNAL_H
#define FERRUM_ASSET_STREAM_INTERNAL_H

#include <stdatomic.h>
#include <stdbool.h>

#include "ferrum/asset/asset_stream.h"

/** One registered asset. Addresses are stable for the manager's lifetime. */
typedef struct fr_asset_slot {
    bool                 used;        /**< false = free slot. */
    uint64_t             id;
    fr_asset_class_t     cls;
    int                  priority;
    size_t               ram_size;    /**< RAM budget estimate. */
    size_t               vram_size;   /**< VRAM budget estimate. */
    size_t               ram_loaded;  /**< RAM bytes actually charged. */
    fr_asset_residency_t residency;
    void                *user;        /**< payload for callbacks. */
    uint64_t             last_used;   /**< LRU stamp. */
    fr_asset_stream_t   *owner;       /**< back-ptr for the load fiber. */
    atomic_int           done;        /**< load fiber sets 1 on completion. */
    size_t               load_result; /**< bytes from load cb (fiber writes). */
    int                  load_ok;     /**< fiber writes success flag. */
} fr_asset_slot_t;

/** Find a used slot by id (NULL if none). */
fr_asset_slot_t *fr_asset_slot_find(fr_asset_stream_t *s, uint64_t id);

/** The load-job body: runs the load callback and flags completion. */
void fr_asset_stream_load_job(void *ud);

/** Charge a RAM-resident slot's bytes back and release it (RAM tier). */
void fr_asset_stream_release_ram(fr_asset_stream_t *s, fr_asset_slot_t *slot);
/** Release a slot's VRAM tier. */
void fr_asset_stream_release_vram(fr_asset_stream_t *s, fr_asset_slot_t *slot);

#endif /* FERRUM_ASSET_STREAM_INTERNAL_H */
