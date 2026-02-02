#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <time.h>

#include "ferrum/job/system.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/server/repl_server.h"

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <port> <max_clients> <duration_ms> <tick_hz> <workers>\n"
            "  duration_ms=0 runs until SIGINT/SIGTERM\n",
            argv0);
}

static volatile sig_atomic_t g_stop = 0;

static void handle_stop_signal(int signum) {
    (void)signum;
    g_stop = 1;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        usage(argv[0]);
        return 2;
    }

    long port_l = strtol(argv[1], NULL, 10);
    long max_clients_l = strtol(argv[2], NULL, 10);
    long duration_ms_l = strtol(argv[3], NULL, 10);
    long tick_hz_l = strtol(argv[4], NULL, 10);
    long workers_l = strtol(argv[5], NULL, 10);

    if (port_l < 0 || port_l > 65535 || max_clients_l <= 0 || duration_ms_l < 0 || tick_hz_l <= 0 || workers_l <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }

    server_repl_config_t cfg;
    cfg.max_clients = (uint16_t)max_clients_l;
    cfg.tick_hz = (uint16_t)tick_hz_l;
    cfg.max_entities = (uint16_t)max_clients_l;
    cfg.resend_interval_ms = 50u;

    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }

    net_udp_addr_t bind_addr;
    if (net_udp_addr_ipv4(&bind_addr, 0u, 0u, 0u, 0u, (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build bind address\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (net_udp_socket_bind(&sock, &bind_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to bind\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    (void)net_udp_socket_set_nonblocking(&sock, 1);

    job_system_t *jobs = job_system_create((uint32_t)workers_l, 4096u, 1u << 16, 0);
    if (!jobs) {
        fprintf(stderr, "Failed to create job system\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (job_system_start(jobs) != 0) {
        fprintf(stderr, "Failed to start job system\n");
        job_system_shutdown(jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    server_repl_server_t *srv = server_repl_server_create(&cfg, &sock, jobs);
    if (!srv) {
        fprintf(stderr, "Failed to create repl server\n");
        job_system_shutdown(jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    fprintf(stdout, "P008_REPL_SERVER_READY\n");
    fflush(stdout);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_stop_signal;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);

    const uint64_t start = now_ms();
    const uint64_t end = (duration_ms_l == 0) ? UINT64_MAX : (start + (uint64_t)duration_ms_l);
    const uint64_t tick_ms = (uint64_t)(1000u / (uint32_t)tick_hz_l);
    uint64_t next_tick = start;

    while (!g_stop && now_ms() < end) {
        uint64_t now = now_ms();
        (void)server_repl_server_pump(srv, now);
        if (now >= next_tick) {
            (void)server_repl_server_tick(srv, now);
            next_tick += tick_ms;
        }
        sleep_ms(1u);
    }

    server_repl_stats_t st = server_repl_server_stats(srv);
    fprintf(stdout, "p008 stats: clients=%u pps_out=%llu pps_in=%llu bytes_out=%llu bytes_in=%llu\n",
            (unsigned)st.clients_connected,
            (unsigned long long)st.packets_sent,
            (unsigned long long)st.packets_recv,
            (unsigned long long)st.bytes_sent,
            (unsigned long long)st.bytes_recv);

    server_repl_server_destroy(srv);
    job_system_shutdown(jobs);
    net_udp_socket_close(&sock);
    return 0;
}
