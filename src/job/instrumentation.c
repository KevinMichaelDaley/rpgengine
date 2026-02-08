#define _GNU_SOURCE
#include <stdint.h>

#include "internal.h" /* for g_current_fiber + fiber debug trace */

#if FR_JOB_INSTRUMENTATION

#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static atomic_bool g_instr_on = ATOMIC_VAR_INIT(false);
static atomic_bool g_instr_verbose = ATOMIC_VAR_INIT(false);
static atomic_uint_least64_t g_instr_count = ATOMIC_VAR_INIT(0);

void job_instrument_enable(int on) {
    atomic_store(&g_instr_on, (on != 0));
}

uint64_t job_instrument_count(void) {
    return atomic_load(&g_instr_count);
}

#include <dlfcn.h>
#include <inttypes.h>

static void instrument_symbol_(void *addr) {
    if (!addr) {
        return;
    }
    Dl_info info;
    if (dladdr(addr, &info) == 0) {
        return;
    }
    if (info.dli_sname && info.dli_saddr) {
        uintptr_t off = (uintptr_t)addr - (uintptr_t)info.dli_saddr;
        fprintf(stderr, " %s+0x%" PRIxPTR, info.dli_sname, off);
    } else if (info.dli_sname) {
        fprintf(stderr, " %s", info.dli_sname);
    }
}

static void instrument_fiber_trace_(const job_fiber_t *f) {
    if (!f) {
        return;
    }

    /* 'prev_fiber' is a lightweight breadcrumb trail across fibers on the same
       worker; printing a short chain helps approximate a call stack. */
    fprintf(stderr, " stack=[");

    const job_fiber_t *seen[8];
    uint32_t seen_count = 0;

    for (uint32_t depth = 0; f && depth < 6; ++depth) {
        bool cycle = false;
        for (uint32_t i = 0; i < seen_count; ++i) {
            if (seen[i] == f) {
                cycle = true;
                break;
            }
        }
        if (cycle) {
            fprintf(stderr, " <cycle>");
            break;
        }
        if (seen_count < (uint32_t)(sizeof(seen) / sizeof(seen[0]))) {
            seen[seen_count++] = f;
        }

        int waiting = (int)atomic_load_explicit(&((job_fiber_t *)f)->waiting, memory_order_relaxed);
        fprintf(stderr, " {id=%llu w=%d site=%s caller=%p", (unsigned long long)f->id, waiting,
                f->swap_site ? f->swap_site : "?", f->swap_caller);
        instrument_symbol_(f->swap_caller);
        fprintf(stderr, " lastw=%u}", (unsigned)f->last_worker);

        f = f->prev_fiber;
        if (f) {
            fprintf(stderr, " ->");
        }
    }
    fprintf(stderr, "]");
}

void job_instrument_event(const char *event,
                          uint64_t fiber_id,
                          uint64_t job_id,
                          uint32_t worker_id,
                          const char *file,
                          int line) {
    if (!atomic_load(&g_instr_on)) {
        return;
    }

    if (!atomic_load(&g_instr_verbose)) {
        /* Default is "focused" instrumentation to keep logs usable.
           Enable full firehose with JOB_SYS_INSTR_VERBOSE=1. */
        if (strncmp(event, "wait_counter:", 13) != 0 &&
            strncmp(event, "phys_", 5) != 0 &&
            strcmp(event, "dispatch") != 0 &&
            strcmp(event, "dispatch_to") != 0 &&
            strcmp(event, "start") != 0 &&
            strcmp(event, "complete") != 0 &&
            strcmp(event, "magic_invalid") != 0 &&
            strcmp(event, "wake_requeue") != 0 &&
            strcmp(event, "wake_requeue_cas") != 0) {
            return;
        }
    }

    atomic_fetch_add_explicit(&g_instr_count, 1, memory_order_relaxed);
    flockfile(stderr);
    fprintf(stderr, "[JOB-INSTR] %s fiber=%llu job=%llu worker=%u @ %s:%d",
            event,
            (unsigned long long)fiber_id,
            (unsigned long long)job_id,
            (unsigned)worker_id,
            file ? file : "?",
            line);
    instrument_fiber_trace_(g_current_fiber);
    fputc('\n', stderr);
    funlockfile(stderr);
}

void job_instrument_init(void) {
    const char *verbose = getenv("JOB_SYS_INSTR_VERBOSE");
    if (verbose && verbose[0] == '1') {
        atomic_store(&g_instr_verbose, true);
    }

    const char *env = getenv("JOB_SYS_INSTR");
    if (env) {
        if (env[0] == '1') {
            atomic_store(&g_instr_on, true);
            return;
        } else if (env[0] == '0') {
            atomic_store(&g_instr_on, false);
            return;
        }
    }
#ifdef JOB_SYS_INSTRUMENT_DEFAULT_ON
    atomic_store(&g_instr_on, true);
#else
    /* Default OFF unless explicitly enabled via env or macro */
    atomic_store(&g_instr_on, false);
#endif
}

#endif
