/**
 * @file engine_settings.h
 * @brief Global engine settings — set before launch, read-only after.
 *
 * Engine settings are configured before the job system and network
 * threads start.  Once frozen via fr_engine_settings_freeze(), the
 * settings become immutable for the remainder of the process lifetime.
 * This guarantees safe concurrent reads from worker threads without
 * any synchronization.
 *
 * Ownership: The engine owns a single static instance.
 * Threading: Write before freeze (main thread only), read after freeze
 *            (any thread, lock-free).
 * Nullability: All getters return a const pointer; never NULL after init.
 */

#ifndef FERRUM_ENGINE_SETTINGS_H
#define FERRUM_ENGINE_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef FR_NET_EMULATION
#include "ferrum/net/emulation/net_emulator.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Settings struct ─────────────────────────────────────────── */

/**
 * @brief Engine-wide configuration, immutable after freeze.
 */
typedef struct fr_engine_settings {
#ifdef FR_NET_EMULATION
    /** Network emulation config.  Only effective when FR_NET_EMULATION
     *  is defined at compile time. */
    net_emu_config_t net_emu;
    /** Whether the network emulator is active. */
    uint8_t          net_emu_enabled;
    /** PRNG seed for the emulator (0 = auto). */
    uint32_t         net_emu_seed;
#endif
    /** Reserved for future settings (ensures the struct is never empty). */
    uint8_t _reserved;
} fr_engine_settings_t;

/* ── Lifecycle (main thread only, before freeze) ─────────────── */

/**
 * @brief Initialize engine settings to defaults.
 *
 * Must be called before any other settings function.
 * Calling after freeze is an error (returns -1).
 *
 * @return 0 on success, -1 if already frozen.
 */
int fr_engine_settings_init(void);

/**
 * @brief Get a mutable pointer to engine settings for configuration.
 *
 * Only valid before freeze.  Returns NULL if not initialized or
 * already frozen.
 */
fr_engine_settings_t *fr_engine_settings_mut(void);

/**
 * @brief Freeze settings, making them immutable.
 *
 * After this call, fr_engine_settings_mut() returns NULL and
 * fr_engine_settings_get() becomes safe to call from any thread.
 *
 * @return 0 on success, -1 if not initialized.
 */
int fr_engine_settings_freeze(void);

/* ── Read access (any thread, after freeze) ──────────────────── */

/**
 * @brief Get a read-only pointer to the frozen settings.
 *
 * Returns NULL if settings are not yet frozen.
 */
const fr_engine_settings_t *fr_engine_settings_get(void);

/**
 * @brief Check whether settings have been frozen.
 *
 * Equivalent to `fr_engine_settings_get() != NULL`.
 */
static inline int fr_engine_settings_is_frozen(void) {
    return fr_engine_settings_get() != NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ENGINE_SETTINGS_H */
