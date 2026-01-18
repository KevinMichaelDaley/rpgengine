#include "internal.h"

uint64_t job_system_jobs_started(const job_system_t *sys) {
    if (!sys) {
        return 0;
    }
    return atomic_load_explicit(&sys->jobs_started, memory_order_relaxed);
}

uint64_t job_system_jobs_completed(const job_system_t *sys) {
    if (!sys) {
        return 0;
    }
    return atomic_load_explicit(&sys->jobs_completed, memory_order_relaxed);
}
