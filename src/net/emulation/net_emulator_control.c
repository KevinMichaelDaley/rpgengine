/**
 * @file net_emulator_control.c
 * @brief Runtime control for the network emulator.
 *
 * Non-static functions (4):
 *   1. net_emulator_configure
 *   2. net_emulator_set_enabled
 *   3. net_emulator_is_enabled
 *   4. net_emulator_pending
 */

#include "ferrum/net/emulation/net_emulator.h"

/* ── Runtime control ──────────────────────────────────────────── */

int net_emulator_configure(net_emulator_t *emu,
                           const net_emu_config_t *config) {
    if (!emu || !config) { return NET_EMU_ERR_INVALID; }
    emu->config = *config;
    return NET_EMU_OK;
}

void net_emulator_set_enabled(net_emulator_t *emu, int enabled) {
    if (!emu) { return; }
    emu->enabled = enabled ? 1 : 0;
}

int net_emulator_is_enabled(const net_emulator_t *emu) {
    if (!emu) { return 0; }
    return emu->enabled;
}

uint32_t net_emulator_pending(const net_emulator_t *emu) {
    if (!emu) { return 0; }
    return emu->queue_count;
}
