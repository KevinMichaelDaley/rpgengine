/**
 * @file engine_settings.c
 * @brief Global engine settings — single static instance.
 *
 * Non-static functions (4):
 *   1. fr_engine_settings_init
 *   2. fr_engine_settings_mut
 *   3. fr_engine_settings_freeze
 *   4. fr_engine_settings_get
 */

#include "ferrum/engine_settings.h"

#include <string.h>

/* ── Static state ────────────────────────────────────────────── */

/** Global settings instance. */
static fr_engine_settings_t g_settings;

/** 1 = initialized (settings writable), 0 = not yet initialized. */
static int g_initialized = 0;

/** 1 = frozen (settings read-only), 0 = still mutable. */
static int g_frozen = 0;

/* ── Lifecycle ────────────────────────────────────────────────── */

int fr_engine_settings_init(void) {
    if (g_frozen) { return -1; }
    memset(&g_settings, 0, sizeof(g_settings));
    g_initialized = 1;
    g_frozen = 0;
    return 0;
}

fr_engine_settings_t *fr_engine_settings_mut(void) {
    if (!g_initialized || g_frozen) { return NULL; }
    return &g_settings;
}

int fr_engine_settings_freeze(void) {
    if (!g_initialized) { return -1; }
    g_frozen = 1;
    return 0;
}

const fr_engine_settings_t *fr_engine_settings_get(void) {
    if (!g_frozen) { return NULL; }
    return &g_settings;
}
