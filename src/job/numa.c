#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
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

static int count_nodes_in_sysfs(const char *root) {
    int count = 0;
    DIR *d = opendir(root);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "node", 4) != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", root, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            count++;
        }
    }
    closedir(d);
    return count;
}

int job_system_enable_numa_auto(job_system_t *sys) {
    if (!sys) return -1;
    const char *override = getenv("JOB_SYS_NUMA_SYSFS");
    const char *root = override && override[0] ? override : "/sys/devices/system/node";
    int n = count_nodes_in_sysfs(root);
    if (n >= 2) {
        sys->numa_node_count = (uint32_t)n;
        sys->numa_enabled = 1;
    } else {
        sys->numa_node_count = 1u;
        sys->numa_enabled = 0;
    }
    return 0;
}
