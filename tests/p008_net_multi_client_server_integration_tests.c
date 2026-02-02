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

static int wait_exit_ok(pid_t pid) {
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 0;
    }
    if (!WIFEXITED(status)) {
        return 0;
    }
    return WEXITSTATUS(status) == 0;
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
    memset(client_pids, 0, sizeof(client_pids));
    for (int i = 0; i < client_count; ++i) {
        char client_duration_s[16];
        char expected_spawns_s[16];
        (void)snprintf(client_duration_s, sizeof(client_duration_s), "%d", duration_ms - 200);
        (void)snprintf(expected_spawns_s, sizeof(expected_spawns_s), "%d", client_count);

        char *client_argv[] = {"./build/p008_net_repl_client", "127.0.0.1", port_s, client_duration_s, expected_spawns_s, NULL};
        pid_t cpid = fork();
        ASSERT_TRUE(cpid >= 0);
        if (cpid == 0) {
            execv(client_argv[0], client_argv);
            _exit(127);
        }
        client_pids[i] = cpid;
    }

    int ok = 1;
    for (int i = 0; i < client_count; ++i) {
        int code = -1;
        if (!wait_exit_code(client_pids[i], &code) || code != 0) {
            fprintf(stderr, "client[%d] exit=%d\n", i, code);
            ok = 0;
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

    if (!ok) {
        for (int i = 0; i < client_count; ++i) {
            kill_if_running(client_pids[i]);
        }
        kill_if_running(server_pid);
        return 1;
    }

    return 0;
}
