/**
 * @file net_emulator_io.c
 * @brief Packet submission and retrieval for the network emulator.
 *
 * Non-static functions (2):
 *   1. net_emulator_submit
 *   2. net_emulator_pop
 */

#include "ferrum/net/emulation/net_emulator.h"

#include <math.h>
#include <string.h>

/* ── PRNG (duplicated from net_emulator.c to keep files independent) ── */

static uint32_t xorshift32_(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rand_uniform_(uint32_t *state) {
    return (float)(xorshift32_(state) & 0x7FFFFFFFu) / (float)0x80000000u;
}

static float rand_normal_(uint32_t *state) {
    float u1 = rand_uniform_(state);
    float u2 = rand_uniform_(state);
    if (u1 < 1e-10f) { u1 = 1e-10f; }
    return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}

/* ── Delay sampling ───────────────────────────────────────────── */

static uint64_t sample_delay_us_(const net_emu_config_t *cfg, uint32_t *rng) {
    float delay_ms;
    switch (cfg->distribution) {
    case NET_EMU_DIST_NORMAL: {
        float sample = cfg->delay_ms + cfg->jitter_ms * rand_normal_(rng);
        float max_ms = cfg->delay_ms + 4.0f * cfg->jitter_ms;
        if (sample < 0.0f) { sample = 0.0f; }
        if (sample > max_ms) { sample = max_ms; }
        delay_ms = sample;
        break;
    }
    case NET_EMU_DIST_LOG_NORMAL: {
        float d = cfg->delay_ms;
        float j = cfg->jitter_ms;
        if (d < 0.01f) { d = 0.01f; }
        if (j < 0.001f) { j = 0.001f; }
        float ratio = j / d;
        float sigma2 = logf(1.0f + ratio * ratio);
        float sigma = sqrtf(sigma2);
        float mu = logf(d) - sigma2 * 0.5f;
        float sample = expf(mu + sigma * rand_normal_(rng));
        float max_ms = d + 6.0f * j;
        if (sample > max_ms) { sample = max_ms; }
        if (sample < 0.0f) { sample = 0.0f; }
        delay_ms = sample;
        break;
    }
    default: {
        float lo = cfg->delay_ms - cfg->jitter_ms;
        float hi = cfg->delay_ms + cfg->jitter_ms;
        if (lo < 0.0f) { lo = 0.0f; }
        delay_ms = lo + (hi - lo) * rand_uniform_(rng);
        break;
    }
    }
    return (uint64_t)(delay_ms * 1000.0f);
}

/* ── Find a free slot in the queue ────────────────────────────── */

static net_emu_packet_t *find_free_slot(net_emulator_t *emu) {
    if (!emu->queue || emu->queue_count >= emu->queue_cap) {
        return NULL;
    }
    for (uint32_t i = 0; i < emu->queue_cap; i++) {
        if (!emu->queue[i].occupied) {
            return &emu->queue[i];
        }
    }
    return NULL;
}

/* ── Enqueue a single packet ──────────────────────────────────── */

static int enqueue_packet(net_emulator_t *emu,
                          const net_udp_addr_t *addr,
                          const void *data, size_t size,
                          uint64_t release_us) {
    net_emu_packet_t *slot = find_free_slot(emu);
    if (!slot) { return NET_EMU_ERR_FULL; }

    memcpy(slot->data, data, size);
    slot->size = size;
    slot->addr = *addr;
    slot->release_us = release_us;
    slot->occupied = 1;
    emu->queue_count++;
    return NET_EMU_OK;
}

/* ── Submit ───────────────────────────────────────────────────── */

int net_emulator_submit(net_emulator_t *emu,
                        const net_udp_addr_t *addr,
                        const void *data,
                        size_t size,
                        uint64_t now_us) {
    if (!emu || !addr || (!data && size != 0)) {
        return NET_EMU_ERR_INVALID;
    }
    if (size > NET_EMU_MAX_PACKET_SIZE) {
        return NET_EMU_ERR_INVALID;
    }

    /* Random packet loss. */
    if (emu->config.loss_pct > 0.0f) {
        float r = rand_uniform_(&emu->rng_state) * 100.0f;
        if (r < emu->config.loss_pct) {
            return NET_EMU_OK; /* silently dropped */
        }
    }

    /* Compute release time. */
    uint64_t delay = sample_delay_us_(&emu->config, &emu->rng_state);
    uint64_t release = now_us + delay;

    /* Reorder: randomly push release time earlier to cause
     * out-of-order arrival. */
    if (emu->config.reorder_pct > 0.0f) {
        float r = rand_uniform_(&emu->rng_state) * 100.0f;
        if (r < emu->config.reorder_pct) {
            float frac = 0.5f + rand_uniform_(&emu->rng_state);
            uint64_t shift = (uint64_t)((float)delay * frac);
            if (shift > release) { release = 0; }
            else { release -= shift; }
        }
    }

    /* Enqueue the packet. */
    int rc = enqueue_packet(emu, addr, data, size, release);
    if (rc != NET_EMU_OK) { return rc; }

    /* Duplicate: enqueue a second copy with independent delay. */
    if (emu->config.duplicate_pct > 0.0f) {
        float r = rand_uniform_(&emu->rng_state) * 100.0f;
        if (r < emu->config.duplicate_pct) {
            uint64_t dup_delay = sample_delay_us_(&emu->config, &emu->rng_state);
            /* Duplicate might fail if queue is full — that's OK. */
            (void)enqueue_packet(emu, addr, data, size, now_us + dup_delay);
        }
    }

    return NET_EMU_OK;
}

/* ── Pop ──────────────────────────────────────────────────────── */

int net_emulator_pop(net_emulator_t *emu,
                     net_udp_addr_t *out_addr,
                     void *out_data,
                     size_t out_cap,
                     size_t *out_size,
                     uint64_t now_us) {
    if (!emu || !out_addr || !out_data || !out_size) {
        return NET_EMU_ERR_INVALID;
    }
    if (!emu->queue || emu->queue_count == 0) {
        return NET_EMU_ERR_INVALID;
    }

    /* Find the earliest-release packet that is due. */
    net_emu_packet_t *best = NULL;
    for (uint32_t i = 0; i < emu->queue_cap; i++) {
        net_emu_packet_t *p = &emu->queue[i];
        if (!p->occupied) { continue; }
        if (p->release_us > now_us) { continue; }
        if (!best || p->release_us < best->release_us) {
            best = p;
        }
    }

    if (!best) {
        return NET_EMU_ERR_INVALID; /* nothing due yet */
    }

    if (best->size > out_cap) {
        return NET_EMU_ERR_INVALID; /* buffer too small */
    }

    /* Copy out. */
    memcpy(out_data, best->data, best->size);
    *out_size = best->size;
    *out_addr = best->addr;

    /* Free the slot. */
    best->occupied = 0;
    emu->queue_count--;

    return NET_EMU_OK;
}
