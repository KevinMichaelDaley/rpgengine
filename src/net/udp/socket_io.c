#include "ferrum/net/udp_socket.h"

#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ── Legacy env-var impairment (used when FR_NET_EMULATION is NOT defined) ── */

#ifndef FR_NET_EMULATION

static uint32_t fr__impair_rng_state = 0u;
static int fr__impair_inited = 0;
static int fr__impair_drop_pct = -1;
static int fr__impair_jitter_ms = -1;

static uint32_t fr__xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void fr__impair_init_once(void) {
    if (fr__impair_inited) {
        return;
    }
    fr__impair_inited = 1;

    const char *drop_s = getenv("P008_NET_DROP_PCT");
    const char *jitter_s = getenv("P008_NET_JITTER_MS");
    const char *seed_s = getenv("P008_NET_SEED");

    if (drop_s && *drop_s) {
        long v = strtol(drop_s, NULL, 10);
        if (v >= 0 && v <= 100) {
            fr__impair_drop_pct = (int)v;
        }
    }
    if (jitter_s && *jitter_s) {
        long v = strtol(jitter_s, NULL, 10);
        if (v >= 0 && v <= 60000) {
            fr__impair_jitter_ms = (int)v;
        }
    }

    uint32_t seed = (uint32_t)getpid();
    if (seed_s && *seed_s) {
        unsigned long v = strtoul(seed_s, NULL, 10);
        seed ^= (uint32_t)v;
    }
    seed ^= 0x9E3779B9u;
    if (seed == 0u) {
        seed = 1u;
    }
    fr__impair_rng_state = seed;
}

static int fr__impair_should_drop(void) {
    fr__impair_init_once();
    if (fr__impair_drop_pct <= 0) {
        return 0;
    }
    uint32_t r = fr__xorshift32(&fr__impair_rng_state);
    return (int)(r % 100u) < fr__impair_drop_pct;
}

static void fr__impair_maybe_jitter(void) {
    fr__impair_init_once();
    if (fr__impair_jitter_ms <= 0) {
        return;
    }
    uint32_t r = fr__xorshift32(&fr__impair_rng_state);
    uint32_t delay_ms = (uint32_t)(r % (uint32_t)(fr__impair_jitter_ms + 1));
    if (delay_ms == 0u) {
        return;
    }
    struct timespec ts;
    ts.tv_sec = (time_t)(delay_ms / 1000u);
    ts.tv_nsec = (long)((delay_ms % 1000u) * 1000u * 1000u);
    (void)nanosleep(&ts, NULL);
}

#endif /* !FR_NET_EMULATION */

/* ── New engine-settings-driven emulator (FR_NET_EMULATION) ──── */

#ifdef FR_NET_EMULATION

#include "ferrum/engine_settings.h"
#include "ferrum/net/emulation/net_emulator.h"

/** Single global emulator instance, lazily initialized from engine settings. */
static net_emulator_t g_net_emu;
static int g_net_emu_inited = 0;

/**
 * @brief Return the monotonic clock in microseconds.
 */
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/**
 * @brief Lazily initialize the global emulator from frozen engine settings.
 *
 * Returns the emulator if emulation is enabled, NULL otherwise.
 */
static net_emulator_t *fr__get_emulator(void) {
    if (g_net_emu_inited) {
        return g_net_emu.enabled ? &g_net_emu : NULL;
    }

    const fr_engine_settings_t *s = fr_engine_settings_get();
    if (!s || !s->net_emu_enabled) {
        /* Not frozen yet or emulation disabled — skip. */
        return NULL;
    }

    /* Initialize from frozen settings (only happens once). */
    net_emulator_init(&g_net_emu, &s->net_emu, s->net_emu_seed);
    g_net_emu_inited = 1;
    return &g_net_emu;
}

#endif /* FR_NET_EMULATION */

/* ── sendto ──────────────────────────────────────────────────── */

int net_udp_socket_sendto(net_udp_socket_t *sock, const net_udp_addr_t *to, const void *data, size_t size) {
    if (!sock || !sock->initialized || !to || (!data && size != 0u)) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    if (to->len == 0u || to->len > sizeof(to->storage)) {
        return NET_UDP_SOCKET_ERR_ADDR;
    }

#ifdef FR_NET_EMULATION
    net_emulator_t *emu = fr__get_emulator();
    if (emu) {
        /* Queue the packet through the emulator instead of sending immediately. */
        int erc = net_emulator_submit(emu, to, data, size, now_us());
        if (erc == NET_EMU_ERR_FULL) {
            /* Queue full — drop silently (same as real network congestion). */
            return NET_UDP_SOCKET_OK;
        }
        /* Flush any packets whose release time has passed. */
        uint8_t flush_buf[NET_EMU_MAX_PACKET_SIZE];
        size_t flush_size;
        net_udp_addr_t flush_addr;
        uint64_t t = now_us();
        while (net_emulator_pop(emu, &flush_addr, flush_buf,
                                sizeof(flush_buf), &flush_size, t) == NET_EMU_OK) {
            (void)sendto(sock->fd, flush_buf, flush_size, 0,
                         (const struct sockaddr *)flush_addr.storage,
                         (socklen_t)flush_addr.len);
        }
        return NET_UDP_SOCKET_OK;
    }
#else
    if (fr__impair_should_drop()) {
        return NET_UDP_SOCKET_OK;
    }
    fr__impair_maybe_jitter();
#endif

    ssize_t rc = sendto(sock->fd, data, size, 0, (const struct sockaddr *)to->storage, (socklen_t)to->len);
    if (rc < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

/* ── recvfrom ────────────────────────────────────────────────── */

int net_udp_socket_recvfrom(net_udp_socket_t *sock,
                            net_udp_addr_t *out_from,
                            void *out_data,
                            size_t out_capacity,
                            size_t *out_size) {
    if (!sock || !sock->initialized || !out_from || !out_data || !out_size) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    for (;;) {
        struct sockaddr_storage ss;
        socklen_t len = (socklen_t)sizeof(ss);

        ssize_t rc = recvfrom(sock->fd, out_data, out_capacity, 0, (struct sockaddr *)&ss, &len);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                int nonblocking = 0;
                int nb_rc = net_udp_socket__internal_is_nonblocking(sock, &nonblocking);
                if (nb_rc != NET_UDP_SOCKET_OK) {
                    return nb_rc;
                }

                if (nonblocking) {
                    return NET_UDP_SOCKET_EMPTY;
                }
                return NET_UDP_SOCKET_TIMEOUT;
            }
            return NET_UDP_SOCKET_ERR_SYS;
        }

        if ((size_t)rc > out_capacity) {
            return NET_UDP_SOCKET_ERR_SYS;
        }

        if ((size_t)len > sizeof(out_from->storage)) {
            return NET_UDP_SOCKET_ERR_ADDR;
        }

#ifndef FR_NET_EMULATION
        if (fr__impair_should_drop()) {
            continue;
        }
        fr__impair_maybe_jitter();
#endif
        /* When FR_NET_EMULATION is defined, recv-side impairment is
         * handled by the send-side emulator (delay queue).  The recv
         * path passes packets through unmodified. */

        memcpy(out_from->storage, &ss, (size_t)len);
        out_from->len = (uint32_t)len;

        *out_size = (size_t)rc;
        return NET_UDP_SOCKET_OK;
    }
}
