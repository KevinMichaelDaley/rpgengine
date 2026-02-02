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

static pid_t spawn_with_stdout(const char *path, char *const argv[], int *out_read_fd) {
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
        execv(path, argv);
        _exit(127);
    }

    close(pipefd[1]);
    *out_read_fd = pipefd[0];
    return pid;
}

static int wait_for_line(int fd, const char *needle, uint32_t max_bytes) {
    char buf[1024];
    size_t used = 0u;
    while (used + 1u < sizeof(buf) && used < max_bytes) {
        ssize_t n = read(fd, buf + used, sizeof(buf) - 1u - used);
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
        buf[used] = '\0';
        if (strstr(buf, needle) != NULL) {
            return 1;
        }
    }
    return 0;
}

static ssize_t read_all_nonblockingish(int fd, char *out, size_t out_cap) {
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

struct client_stats {
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint32_t spawns;
    uint32_t states;
    double tx_mbps;
    double rx_mbps;
    uint64_t pos_samples;
    double pos_err_mean;
    double pos_err_max;
    uint64_t rot_samples;
    double rot_err_deg_mean;
    double rot_err_deg_max;
    uint32_t corrections;
    int parsed;
};

static void parse_client_stats(struct client_stats *out, const char *s) {
    if (!out || !s) {
        return;
    }
    memset(out, 0, sizeof(*out));

    const char *p = strstr(s, "P008_CLIENT_STATS ");
    if (!p) {
        return;
    }

    unsigned long long tx_bytes = 0, rx_bytes = 0, tx_packets = 0, rx_packets = 0;
    unsigned spawns = 0, states = 0, corrections = 0;
    double tx_mbps = 0.0, rx_mbps = 0.0;
    unsigned long long pos_samples = 0, rot_samples = 0;
    double pos_err_mean = 0.0, pos_err_max = 0.0;
    double rot_err_mean = 0.0, rot_err_max = 0.0;

    int n = sscanf(p,
                   "P008_CLIENT_STATS tx_bytes=%llu rx_bytes=%llu tx_packets=%llu rx_packets=%llu spawns=%u states=%u "
                   "tx_mbps=%lf rx_mbps=%lf pos_samples=%llu pos_err_mean=%lf pos_err_max=%lf "
                   "rot_samples=%llu rot_err_deg_mean=%lf rot_err_deg_max=%lf corrections=%u",
                   &tx_bytes,
                   &rx_bytes,
                   &tx_packets,
                   &rx_packets,
                   &spawns,
                   &states,
                   &tx_mbps,
                   &rx_mbps,
                   &pos_samples,
                   &pos_err_mean,
                   &pos_err_max,
                   &rot_samples,
                   &rot_err_mean,
                   &rot_err_max,
                   &corrections);
    if (n != 15) {
        return;
    }

    out->tx_bytes = (uint64_t)tx_bytes;
    out->rx_bytes = (uint64_t)rx_bytes;
    out->tx_packets = (uint64_t)tx_packets;
    out->rx_packets = (uint64_t)rx_packets;
    out->spawns = (uint32_t)spawns;
    out->states = (uint32_t)states;
    out->tx_mbps = tx_mbps;
    out->rx_mbps = rx_mbps;
    out->pos_samples = (uint64_t)pos_samples;
    out->pos_err_mean = pos_err_mean;
    out->pos_err_max = pos_err_max;
    out->rot_samples = (uint64_t)rot_samples;
    out->rot_err_deg_mean = rot_err_mean;
    out->rot_err_deg_max = rot_err_max;
    out->corrections = (uint32_t)corrections;
    out->parsed = 1;
}

int main(void) {
    const int client_count = 4;
    const int server_port = 40000 + (int)(getpid() % 20000); /* Reduce collision risk. */
    const int duration_ms = 1500;
    const int tick_hz = 60;
    const int workers = 2;

    char port_s[16];
    char max_clients_s[16];
    char duration_s[16];
    char tick_s[16];
    char workers_s[16];
    (void)snprintf(port_s, sizeof(port_s), "%d", server_port);
    (void)snprintf(max_clients_s, sizeof(max_clients_s), "%d", client_count);
    (void)snprintf(duration_s, sizeof(duration_s), "%d", duration_ms);
    (void)snprintf(tick_s, sizeof(tick_s), "%d", tick_hz);
    (void)snprintf(workers_s, sizeof(workers_s), "%d", workers);

    int server_out = -1;
    char *server_argv[] = {"./build/p008_net_repl_server", port_s, max_clients_s, duration_s, tick_s, workers_s, NULL};
    pid_t server_pid = spawn_with_stdout(server_argv[0], server_argv, &server_out);
    ASSERT_TRUE(server_pid > 0);
    ASSERT_TRUE(server_out >= 0);

    ASSERT_TRUE(wait_for_line(server_out, "P008_REPL_SERVER_READY", 8192u));

    /* Keep server output FD open for error reporting. */

    pid_t client_pids[client_count];
    int client_out[client_count];
    memset(client_pids, 0, sizeof(client_pids));
    memset(client_out, 0, sizeof(client_out));
    for (int i = 0; i < client_count; ++i) {
        char client_duration_s[16];
        char expected_spawns_s[16];
        char tick_hz_s[16];
        (void)snprintf(client_duration_s, sizeof(client_duration_s), "%d", duration_ms - 200);
        (void)snprintf(expected_spawns_s, sizeof(expected_spawns_s), "%d", client_count);
        (void)snprintf(tick_hz_s, sizeof(tick_hz_s), "%d", tick_hz);

        char *client_argv[] = {"./build/p008_net_repl_client", "127.0.0.1", port_s, client_duration_s, expected_spawns_s, tick_hz_s, NULL};
        int fd = -1;
        pid_t cpid = spawn_with_stdout(client_argv[0], client_argv, &fd);
        ASSERT_TRUE(cpid > 0);
        ASSERT_TRUE(fd >= 0);
        client_pids[i] = cpid;
        client_out[i] = fd;
    }

    int ok = 1;

    uint64_t sum_tx_bytes = 0u;
    uint64_t sum_rx_bytes = 0u;
    uint64_t sum_tx_packets = 0u;
    uint64_t sum_rx_packets = 0u;
    uint64_t sum_pos_samples = 0u;
    uint64_t sum_rot_samples = 0u;
    double sum_pos_err = 0.0;
    double sum_rot_err = 0.0;
    double max_pos_err = 0.0;
    double max_rot_err = 0.0;
    uint64_t sum_corrections = 0u;

    for (int i = 0; i < client_count; ++i) {
        int code = -1;
        if (!wait_exit_code(client_pids[i], &code) || code != 0) {
            fprintf(stderr, "client[%d] exit=%d\n", i, code);
            ok = 0;
        }

        char out_buf[4096];
        (void)read_all_nonblockingish(client_out[i], out_buf, sizeof(out_buf));
        close(client_out[i]);
        client_out[i] = -1;

        struct client_stats st;
        parse_client_stats(&st, out_buf);
        if (!st.parsed) {
            fprintf(stderr, "client[%d] missing stats line\n", i);
            ok = 0;
        } else {
            sum_tx_bytes += st.tx_bytes;
            sum_rx_bytes += st.rx_bytes;
            sum_tx_packets += st.tx_packets;
            sum_rx_packets += st.rx_packets;
            sum_corrections += (uint64_t)st.corrections;

            if (st.pos_samples > 0u) {
                sum_pos_samples += st.pos_samples;
                sum_pos_err += st.pos_err_mean * (double)st.pos_samples;
                if (st.pos_err_max > max_pos_err) {
                    max_pos_err = st.pos_err_max;
                }
            }
            if (st.rot_samples > 0u) {
                sum_rot_samples += st.rot_samples;
                sum_rot_err += st.rot_err_deg_mean * (double)st.rot_samples;
                if (st.rot_err_deg_max > max_rot_err) {
                    max_rot_err = st.rot_err_deg_max;
                }
            }
        }
    }

    int server_code = -1;
    if (!wait_exit_code(server_pid, &server_code) || server_code != 0) {
        fprintf(stderr, "server exit=%d\n", server_code);
        ok = 0;
    }

    if (!ok) {
        char dump[4096];
        ssize_t n = read(server_out, dump, sizeof(dump) - 1);
        if (n > 0) {
            dump[n] = '\0';
            fprintf(stderr, "server output (tail):\n%s\n", dump);
        }
    }
    close(server_out);

    if (ok) {
        const double duration_s = (double)(duration_ms - 200) / 1000.0;
        const double agg_tx_mbps = (duration_s > 0.0) ? ((double)sum_tx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;
        const double agg_rx_mbps = (duration_s > 0.0) ? ((double)sum_rx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;
        const double mean_pos_err = (sum_pos_samples > 0u) ? (sum_pos_err / (double)sum_pos_samples) : 0.0;
        const double mean_rot_err = (sum_rot_samples > 0u) ? (sum_rot_err / (double)sum_rot_samples) : 0.0;

        fprintf(stdout,
                "P008_INTEGRATION_STATS clients=%d duration_ms=%d tx_bytes=%llu rx_bytes=%llu tx_packets=%llu rx_packets=%llu "
                "tx_mbps=%.3f rx_mbps=%.3f pos_samples=%llu pos_err_mean=%.6f pos_err_max=%.6f rot_samples=%llu rot_err_deg_mean=%.6f rot_err_deg_max=%.6f corrections=%llu\n",
                client_count,
                duration_ms - 200,
                (unsigned long long)sum_tx_bytes,
                (unsigned long long)sum_rx_bytes,
                (unsigned long long)sum_tx_packets,
                (unsigned long long)sum_rx_packets,
                agg_tx_mbps,
                agg_rx_mbps,
                (unsigned long long)sum_pos_samples,
                mean_pos_err,
                max_pos_err,
                (unsigned long long)sum_rot_samples,
                mean_rot_err,
                max_rot_err,
                (unsigned long long)sum_corrections);
    }

    if (!ok) {
        for (int i = 0; i < client_count; ++i) {
            kill_if_running(client_pids[i]);
        }
        kill_if_running(server_pid);
        return 1;
    }

    return 0;
}
