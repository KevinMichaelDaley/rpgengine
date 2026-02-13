/**
 * @file net_emulator.c
 * @brief In-process network condition emulator — lifecycle and delay sampling.
 *
 * Non-static functions (3):
 *   1. net_emu_config_default
 *   2. net_emulator_init
 *   3. net_emulator_destroy
 */

#include "ferrum/net/emulation/net_emulator.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Lifecycle ────────────────────────────────────────────────── */

net_emu_config_t net_emu_config_default(void) {
    net_emu_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.distribution = NET_EMU_DIST_UNIFORM;
    return cfg;
}

int net_emulator_init(net_emulator_t *emu,
                      const net_emu_config_t *config,
                      uint32_t seed) {
    if (!emu || !config) { return NET_EMU_ERR_INVALID; }

    memset(emu, 0, sizeof(*emu));
    emu->config = *config;
    emu->queue_cap = NET_EMU_QUEUE_CAPACITY;
    emu->enabled = 1;

    /* Seed PRNG. */
    if (seed == 0) {
        seed = (uint32_t)getpid() ^ 0x9E3779B9u;
    }
    if (seed == 0) { seed = 1; }
    emu->rng_state = seed;

    emu->queue = calloc(emu->queue_cap, sizeof(net_emu_packet_t));
    if (!emu->queue) { return NET_EMU_ERR_ALLOC; }

    return NET_EMU_OK;
}

void net_emulator_destroy(net_emulator_t *emu) {
    if (!emu) { return; }
    free(emu->queue);
    emu->queue = NULL;
    emu->queue_count = 0;
}
