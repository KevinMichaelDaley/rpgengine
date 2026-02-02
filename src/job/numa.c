#include "internal.h"

int job_system_enable_numa(job_system_t *sys, uint32_t node_count) {
    if (!sys || node_count == 0) {
        return -1;
    }
    sys->numa_node_count = node_count;
    sys->numa_enabled = (node_count > 1) ? 1 : 0;
    return 0;
}

int job_system_numa_enabled(const job_system_t *sys) {
    if (!sys) return 0;
    return sys->numa_enabled ? 1 : 0;
}
