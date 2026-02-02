#include <stdio.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "ferrum/job/instrumentation.h"

static atomic_bool g_instr_on = ATOMIC_VAR_INIT(false);
static atomic_uint_least64_t g_instr_count = ATOMIC_VAR_INIT(0);

void job_instrument_enable(int on) {
    atomic_store(&g_instr_on, (on != 0));
}

uint64_t job_instrument_count(void) {
    return atomic_load(&g_instr_count);
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
    atomic_fetch_add_explicit(&g_instr_count, 1, memory_order_relaxed);
    fprintf(stderr, "[JOB-INSTR] %s fiber=%llu job=%llu worker=%u @ %s:%d\n",
            event,
            (unsigned long long)fiber_id,
            (unsigned long long)job_id,
            (unsigned)worker_id,
            file ? file : "?",
            line);
}

void job_instrument_init(void) {
    const char *env = getenv("JOB_SYS_INSTR");
    if (env) {
        if (env[0] == '1') {
            atomic_store(&g_instr_on, true);
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
