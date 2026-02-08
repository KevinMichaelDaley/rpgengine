#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "runtime_internal.h"

static fr_topic_channel_t *make_topic_(uint32_t capacity) {
    fr_topic_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.capacity = (capacity == 0u) ? 64u : capacity;
    return fr_topic_channel_create(&cfg);
}

static uint32_t out_topic_default_cap_(void) {
    /* Default higher than legacy 128 to tolerate bursts.
       Still bounded to avoid per-client memory blowups.
     */
    return 2048u;
}

fr_server_net_runtime_t *fr_server_net_runtime_create(const fr_server_net_runtime_config_t *cfg) {
    if (!cfg || cfg->max_clients == 0u || !cfg->inbound_topic) {
        return NULL;
    }
    if (!cfg->recvfrom_cb && !cfg->socket) {
        return NULL;
    }
    if (!cfg->sendto_cb && !cfg->socket) {
        return NULL;
    }

    fr_server_net_runtime_t *rt = (fr_server_net_runtime_t *)calloc(1u, sizeof(*rt));
    if (!rt) {
        return NULL;
    }
    rt->cfg = *cfg;

    /* Tests may omit cfg.jobs; create an internal job system. */
    rt->owns_jobs = 0u;
    if (!rt->cfg.jobs) {
        const uint32_t queue_capacity = 256u;
        const size_t fiber_stack_size = 128u * 1024u;
        const size_t fiber_count_max = 2048u;
        /* Non-deterministic mode so long-lived fibers can run without wait_idle deadlocks. */
        job_system_create_status_t st = job_system_create(&rt->owned_jobs, 1u, queue_capacity, fiber_stack_size, fiber_count_max, 0);
        if (st != JOB_CREATE_OK) {
            free(rt);
            return NULL;
        }
        if (job_system_start(&rt->owned_jobs) != 0) {
            job_system_shutdown(&rt->owned_jobs);
            free(rt);
            return NULL;
        }
        rt->cfg.jobs = &rt->owned_jobs;
        rt->owns_jobs = 1u;
    }

    rt->clients = (fr_server_client_t *)calloc((size_t)cfg->max_clients, sizeof(*rt->clients));
    if (!rt->clients) {
        free(rt);
        return NULL;
    }

    for (uint16_t i = 0u; i < cfg->max_clients; ++i) {
        rt->clients[i].active = 0u;
        atomic_init(&rt->clients[i].inbox_ptr, (uintptr_t)0);
        atomic_init(&rt->clients[i].pending_used, false);
        rt->clients[i].pending_size = 0u;
        const uint32_t rel_cap = (rt->cfg.out_reliable_capacity == 0u) ? out_topic_default_cap_() : rt->cfg.out_reliable_capacity;
        const uint32_t unrel_cap = (rt->cfg.out_unreliable_capacity == 0u) ? out_topic_default_cap_() : rt->cfg.out_unreliable_capacity;

        rt->clients[i].out_reliable = make_topic_(rel_cap);
        rt->clients[i].out_unreliable = make_topic_(unrel_cap);
        if (!rt->clients[i].out_reliable || !rt->clients[i].out_unreliable) {
            fr_server_net_runtime_destroy(rt);
            return NULL;
        }
        atomic_init(&rt->clients[i].stop, false);
        atomic_init(&rt->clients[i].now_ms, 0u);
    }

    atomic_init(&rt->packets_in, 0u);
    atomic_init(&rt->packets_out, 0u);
    atomic_init(&rt->bytes_in, 0u);
    atomic_init(&rt->bytes_out, 0u);

    return rt;
}

void fr_server_net_runtime_destroy(fr_server_net_runtime_t *rt) {
    if (!rt) {
        return;
    }

    if (rt->clients) {
        for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
            atomic_store_explicit(&rt->clients[i].stop, true, memory_order_release);
        }

        /* If we own the job system, shut it down to ensure fibers exit before freeing. */
        if (rt->owns_jobs) {
            job_system_shutdown(&rt->owned_jobs);
            rt->owns_jobs = 0u;
        }

        for (uint16_t i = 0u; i < rt->cfg.max_clients; ++i) {
            fr_topic_channel_destroy(rt->clients[i].out_reliable);
            fr_topic_channel_destroy(rt->clients[i].out_unreliable);
        }
        free(rt->clients);
    }

    free(rt);
}

bool fr_server_net_runtime_client_out_topics(fr_server_net_runtime_t *rt,
                                             uint16_t client_id,
                                             fr_topic_channel_t **out_reliable,
                                             fr_topic_channel_t **out_unreliable) {
    if (!rt || client_id >= rt->cfg.max_clients || !out_reliable || !out_unreliable) {
        return false;
    }
    if (!rt->clients[client_id].out_reliable || !rt->clients[client_id].out_unreliable) {
        return false;
    }
    *out_reliable = rt->clients[client_id].out_reliable;
    *out_unreliable = rt->clients[client_id].out_unreliable;
    return true;
}

bool fr_server_net_runtime_run_fibers(fr_server_net_runtime_t *rt, uint32_t max_spins) {
    if (!rt) {
        return false;
    }
    if (max_spins == 0u) {
        max_spins = 1u;
    }

    /* Best-effort: give workers time to run long-lived fibers. */
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1 * 1000 * 1000; /* 1ms */
    for (uint32_t i = 0u; i < max_spins; ++i) {
        nanosleep(&ts, NULL);
    }
    return true;
}
