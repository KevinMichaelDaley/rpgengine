#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

struct kv_u64 {
    const char *key;
    uint64_t *out;
};

struct kv_double {
    const char *key;
    double *out;
};

struct client_stats {
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t rx_packets;

    uint64_t pos_samples;
    double pos_err_mean;
    double pos_err_max;

    uint64_t rot_samples;
    double rot_err_deg_mean;
    double rot_err_deg_max;

    uint32_t corrections;

    double state_inter_ms_mean;
    double state_inter_ms_max;
    double state_lag_ms_mean;
    double state_lag_ms_max;

    double tx_mbps;
    double rx_mbps;

    int parsed;
};

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <server_ipv4> <port> <clients> <duration_ms> [tick_hz] [--drop-pct N] [--jitter-ms N]\n"
            "     [--max-pos-err M] [--max-rot-err-deg M] [--max-state-lag-ms M]\n"
            "\n"
            "Notes:\n"
            "  - Spawns <clients> child processes of ./build/p008_net_repl_client\n"
            "  - Child clients set expected_spawns=0 (auto from WELCOME)\n"
            "  - Packet impairment uses env vars P008_NET_DROP_PCT and P008_NET_JITTER_MS\n",
            argv0);
}

static pid_t spawn_with_stdout_and_env(const char *path,
                                      char *const argv[],
                                      int *out_read_fd,
                                      int drop_pct,
                                      int jitter_ms,
                                      uint32_t seed) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        (void)dup2(pipefd[1], STDOUT_FILENO);
        (void)dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (drop_pct >= 0) {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d", drop_pct);
            (void)setenv("P008_NET_DROP_PCT", buf, 1);
        }
        if (jitter_ms >= 0) {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d", jitter_ms);
            (void)setenv("P008_NET_JITTER_MS", buf, 1);
        }
        {
            char buf[32];
            (void)snprintf(buf, sizeof(buf), "%u", (unsigned)seed);
            (void)setenv("P008_NET_SEED", buf, 1);
        }

        execv(path, argv);
        _exit(127);
    }

    close(pipefd[1]);
    *out_read_fd = pipefd[0];
    return pid;
}

static ssize_t read_all(int fd, char *out, size_t out_cap) {
    if (!out || out_cap == 0u) {
        return -1;
    }
    size_t used = 0u;
    while (used + 1u < out_cap) {
        ssize_t n = read(fd, out + used, out_cap - 1u - used);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        used += (size_t)n;
    }
    out[used] = '\0';
    return (ssize_t)used;
}

static int wait_exit_code(pid_t pid, int *out_code) {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 0;
    }
    if (WIFEXITED(status)) {
        *out_code = WEXITSTATUS(status);
        return 1;
    }
    if (WIFSIGNALED(status)) {
        *out_code = 128 + WTERMSIG(status);
        return 1;
    }
    return 0;
}

static void kill_if_running(pid_t pid) {
    if (pid > 0) {
        (void)kill(pid, SIGTERM);
        (void)waitpid(pid, NULL, 0);
    }
}

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !out) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return 0;
    }
    *out = (uint64_t)v;
    return 1;
}

static int parse_double(const char *s, double *out) {
    if (!s || !out) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    double v = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

static void parse_client_stats_line(struct client_stats *out, const char *buf) {
    if (!out || !buf) {
        return;
    }
    memset(out, 0, sizeof(*out));

    const char *p = strstr(buf, "P008_CLIENT_STATS ");
    if (!p) {
        return;
    }
    p += strlen("P008_CLIENT_STATS ");

    char *scratch = strdup(p);
    if (!scratch) {
        return;
    }

    struct kv_u64 u64_map[] = {
        {"tx_bytes", &out->tx_bytes},
        {"rx_bytes", &out->rx_bytes},
        {"tx_packets", &out->tx_packets},
        {"rx_packets", &out->rx_packets},
        {"pos_samples", &out->pos_samples},
        {"rot_samples", &out->rot_samples},
        {NULL, NULL},
    };

    struct kv_double dbl_map[] = {
        {"pos_err_mean", &out->pos_err_mean},
        {"pos_err_max", &out->pos_err_max},
        {"rot_err_deg_mean", &out->rot_err_deg_mean},
        {"rot_err_deg_max", &out->rot_err_deg_max},
        {"state_inter_ms_mean", &out->state_inter_ms_mean},
        {"state_inter_ms_max", &out->state_inter_ms_max},
        {"state_lag_ms_mean", &out->state_lag_ms_mean},
        {"state_lag_ms_max", &out->state_lag_ms_max},
        {"tx_mbps", &out->tx_mbps},
        {"rx_mbps", &out->rx_mbps},
        {NULL, NULL},
    };

    int saw_any = 0;

    for (char *tok = strtok(scratch, " \r\n\t"); tok; tok = strtok(NULL, " \r\n\t")) {
        char *eq = strchr(tok, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *k = tok;
        const char *v = eq + 1;

        if (strcmp(k, "corrections") == 0) {
            uint64_t tmp = 0;
            if (parse_u64(v, &tmp)) {
                out->corrections = (uint32_t)tmp;
                saw_any = 1;
            }
            continue;
        }

        for (size_t i = 0; u64_map[i].key; ++i) {
            if (strcmp(k, u64_map[i].key) == 0) {
                (void)parse_u64(v, u64_map[i].out);
                saw_any = 1;
            }
        }
        for (size_t i = 0; dbl_map[i].key; ++i) {
            if (strcmp(k, dbl_map[i].key) == 0) {
                (void)parse_double(v, dbl_map[i].out);
                saw_any = 1;
            }
        }
    }

    free(scratch);

    if (saw_any && out->rx_packets > 0u) {
        out->parsed = 1;
    }
}

static int parse_i32_arg(const char *s, int *out) {
    if (!s || !out) {
        return 0;
    }
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return 0;
    }
    if (v < INT32_MIN || v > INT32_MAX) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int parse_u32_arg(const char *s, uint32_t *out) {
    uint64_t tmp = 0;
    if (!parse_u64(s, &tmp) || tmp > 0xFFFFFFFFull) {
        return 0;
    }
    *out = (uint32_t)tmp;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        usage(argv[0]);
        return 2;
    }

    const char *server_ipv4 = argv[1];
    const char *port_s = argv[2];
    uint32_t clients = 0;
    uint32_t duration_ms = 0;
    uint32_t tick_hz = 60;

    if (!parse_u32_arg(argv[3], &clients) || !parse_u32_arg(argv[4], &duration_ms)) {
        fprintf(stderr, "Invalid clients/duration\n");
        return 2;
    }

    int argi = 5;
    if (argc >= 6 && argv[5][0] != '-') {
        if (!parse_u32_arg(argv[5], &tick_hz) || tick_hz == 0u || tick_hz > 1000u) {
            fprintf(stderr, "Invalid tick_hz\n");
            return 2;
        }
        argi = 6;
    }

    int drop_pct = -1;
    int jitter_ms = -1;
    double max_pos_err = 0.001; /* ~1mm */
    double max_rot_err_deg = 0.5;
    double max_state_lag_ms = 1000.0;

    while (argi < argc) {
        const char *k = argv[argi++];
        if (argi >= argc) {
            fprintf(stderr, "Missing value for %s\n", k);
            return 2;
        }
        const char *v = argv[argi++];

        if (strcmp(k, "--drop-pct") == 0) {
            if (!parse_i32_arg(v, &drop_pct) || drop_pct < 0 || drop_pct > 100) {
                fprintf(stderr, "Invalid --drop-pct\n");
                return 2;
            }
        } else if (strcmp(k, "--jitter-ms") == 0) {
            if (!parse_i32_arg(v, &jitter_ms) || jitter_ms < 0 || jitter_ms > 60000) {
                fprintf(stderr, "Invalid --jitter-ms\n");
                return 2;
            }
        } else if (strcmp(k, "--max-pos-err") == 0) {
            if (!parse_double(v, &max_pos_err) || max_pos_err < 0.0) {
                fprintf(stderr, "Invalid --max-pos-err\n");
                return 2;
            }
        } else if (strcmp(k, "--max-rot-err-deg") == 0) {
            if (!parse_double(v, &max_rot_err_deg) || max_rot_err_deg < 0.0) {
                fprintf(stderr, "Invalid --max-rot-err-deg\n");
                return 2;
            }
        } else if (strcmp(k, "--max-state-lag-ms") == 0) {
            if (!parse_double(v, &max_state_lag_ms) || max_state_lag_ms < 0.0) {
                fprintf(stderr, "Invalid --max-state-lag-ms\n");
                return 2;
            }
        } else {
            fprintf(stderr, "Unknown flag: %s\n", k);
            return 2;
        }
    }

    if (clients == 0u || clients > 4096u || duration_ms < 100u) {
        fprintf(stderr, "Invalid clients/duration range\n");
        return 2;
    }

    pid_t *pids = (pid_t *)calloc((size_t)clients, sizeof(*pids));
    int *fds = (int *)calloc((size_t)clients, sizeof(*fds));
    if (!pids || !fds) {
        free(pids);
        free(fds);
        fprintf(stderr, "OOM\n");
        return 1;
    }

    int ok = 1;

    uint64_t sum_tx_bytes = 0u;
    uint64_t sum_rx_bytes = 0u;
    uint64_t sum_tx_packets = 0u;
    uint64_t sum_rx_packets = 0u;

    uint64_t sum_pos_samples = 0u;
    double sum_pos_err = 0.0;
    double max_pos_err_obs = 0.0;

    uint64_t sum_rot_samples = 0u;
    double sum_rot_err = 0.0;
    double max_rot_err_obs = 0.0;

    uint64_t sum_corrections = 0u;

    uint64_t sum_lag_samples = 0u;
    double sum_state_lag_ms = 0.0;
    double max_state_lag_ms_obs = 0.0;

    for (uint32_t i = 0; i < clients; ++i) {
        char duration_s[16];
        char expected_spawns_s[16];
        char tick_hz_s[16];
        (void)snprintf(duration_s, sizeof(duration_s), "%u", (unsigned)duration_ms);
        (void)snprintf(expected_spawns_s, sizeof(expected_spawns_s), "%u", 0u);
        (void)snprintf(tick_hz_s, sizeof(tick_hz_s), "%u", (unsigned)tick_hz);

        char *client_argv[] = {"./build/p008_net_repl_client", (char *)server_ipv4, (char *)port_s, duration_s, expected_spawns_s, tick_hz_s, NULL};

        int fd = -1;
        pid_t pid = spawn_with_stdout_and_env(client_argv[0], client_argv, &fd, drop_pct, jitter_ms, (uint32_t)(0xA5A5u + i * 101u));
        if (pid <= 0 || fd < 0) {
            fprintf(stderr, "Failed to spawn client %u\n", (unsigned)i);
            ok = 0;
            break;
        }
        pids[i] = pid;
        fds[i] = fd;
    }

    for (uint32_t i = 0; i < clients; ++i) {
        if (pids[i] == 0) {
            continue;
        }
        int code = -1;
        if (!wait_exit_code(pids[i], &code) || code != 0) {
            fprintf(stderr, "client[%u] exit=%d\n", (unsigned)i, code);
            ok = 0;
        }

        char out_buf[8192];
        (void)read_all(fds[i], out_buf, sizeof(out_buf));
        close(fds[i]);
        fds[i] = -1;

        struct client_stats st;
        parse_client_stats_line(&st, out_buf);
        if (!st.parsed) {
            fprintf(stderr, "client[%u] missing/invalid stats line\n", (unsigned)i);
            if (out_buf[0] != '\0') {
                fprintf(stderr, "client[%u] output:\n%s\n", (unsigned)i, out_buf);
            }
            ok = 0;
            continue;
        }

        sum_tx_bytes += st.tx_bytes;
        sum_rx_bytes += st.rx_bytes;
        sum_tx_packets += st.tx_packets;
        sum_rx_packets += st.rx_packets;
        sum_corrections += (uint64_t)st.corrections;

        if (st.pos_samples > 0u) {
            sum_pos_samples += st.pos_samples;
            sum_pos_err += st.pos_err_mean * (double)st.pos_samples;
            if (st.pos_err_max > max_pos_err_obs) {
                max_pos_err_obs = st.pos_err_max;
            }
        }
        if (st.rot_samples > 0u) {
            sum_rot_samples += st.rot_samples;
            sum_rot_err += st.rot_err_deg_mean * (double)st.rot_samples;
            if (st.rot_err_deg_max > max_rot_err_obs) {
                max_rot_err_obs = st.rot_err_deg_max;
            }
        }

        if (st.state_lag_ms_max > max_state_lag_ms_obs) {
            max_state_lag_ms_obs = st.state_lag_ms_max;
        }
        if (st.pos_samples > 0u) {
            sum_lag_samples += st.pos_samples;
            sum_state_lag_ms += st.state_lag_ms_mean * (double)st.pos_samples;
        }
    }

    for (uint32_t i = 0; i < clients; ++i) {
        if (!ok) {
            kill_if_running(pids[i]);
        }
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }

    const double duration_s = (double)duration_ms / 1000.0;
    const double agg_tx_mbps = (duration_s > 0.0) ? ((double)sum_tx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;
    const double agg_rx_mbps = (duration_s > 0.0) ? ((double)sum_rx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;

    const double mean_pos_err = (sum_pos_samples > 0u) ? (sum_pos_err / (double)sum_pos_samples) : 0.0;
    const double mean_rot_err = (sum_rot_samples > 0u) ? (sum_rot_err / (double)sum_rot_samples) : 0.0;

    const double mean_state_lag_ms = (sum_lag_samples > 0u) ? (sum_state_lag_ms / (double)sum_lag_samples) : 0.0;

    int thresholds_ok = 1;
    if (max_pos_err_obs > max_pos_err) {
        thresholds_ok = 0;
    }
    if (max_rot_err_obs > max_rot_err_deg) {
        thresholds_ok = 0;
    }
    if (max_state_lag_ms_obs > max_state_lag_ms) {
        thresholds_ok = 0;
    }

    fprintf(stdout,
            "P008_PERF_SUMMARY server=%s:%s clients=%u duration_ms=%u tick_hz=%u "
            "tx_bytes=%llu rx_bytes=%llu tx_packets=%llu rx_packets=%llu tx_mbps=%.3f rx_mbps=%.3f "
            "pos_samples=%llu pos_err_mean=%.6f pos_err_max=%.6f "
            "rot_samples=%llu rot_err_deg_mean=%.6f rot_err_deg_max=%.6f "
            "state_lag_ms_mean=%.3f state_lag_ms_max=%.3f corrections=%llu "
            "thresholds_ok=%d\n",
            server_ipv4,
            port_s,
            (unsigned)clients,
            (unsigned)duration_ms,
            (unsigned)tick_hz,
            (unsigned long long)sum_tx_bytes,
            (unsigned long long)sum_rx_bytes,
            (unsigned long long)sum_tx_packets,
            (unsigned long long)sum_rx_packets,
            agg_tx_mbps,
            agg_rx_mbps,
            (unsigned long long)sum_pos_samples,
            mean_pos_err,
            max_pos_err_obs,
            (unsigned long long)sum_rot_samples,
            mean_rot_err,
            max_rot_err_obs,
            mean_state_lag_ms,
            max_state_lag_ms_obs,
            (unsigned long long)sum_corrections,
            thresholds_ok);

    free(pids);
    free(fds);

    if (!ok || !thresholds_ok) {
        return 1;
    }
    return 0;
}
