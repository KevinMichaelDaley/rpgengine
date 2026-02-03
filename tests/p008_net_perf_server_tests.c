#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s <port> <max_clients> <duration_ms> <tick_hz> <workers> [--drop-pct N] [--jitter-ms N]\n"
            "\n"
            "Notes:\n"
            "  - Runs ./build/p008_net_repl_server with optional packet impairment via env vars\n"
            "    P008_NET_DROP_PCT and P008_NET_JITTER_MS.\n",
            argv0);
}

static int parse_i32(const char *s, int *out) {
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

int main(int argc, char **argv) {
    if (argc < 6) {
        usage(argv[0]);
        return 2;
    }

    int drop_pct = -1;
    int jitter_ms = -1;

    int argi = 6;
    while (argi < argc) {
        const char *k = argv[argi++];
        if (argi >= argc) {
            fprintf(stderr, "Missing value for %s\n", k);
            return 2;
        }
        const char *v = argv[argi++];

        if (strcmp(k, "--drop-pct") == 0) {
            if (!parse_i32(v, &drop_pct) || drop_pct < 0 || drop_pct > 100) {
                fprintf(stderr, "Invalid --drop-pct\n");
                return 2;
            }
        } else if (strcmp(k, "--jitter-ms") == 0) {
            if (!parse_i32(v, &jitter_ms) || jitter_ms < 0 || jitter_ms > 60000) {
                fprintf(stderr, "Invalid --jitter-ms\n");
                return 2;
            }
        } else {
            fprintf(stderr, "Unknown flag: %s\n", k);
            return 2;
        }
    }

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

    char *server_argv[] = {"./build/p008_net_repl_server", argv[1], argv[2], argv[3], argv[4], argv[5], NULL};
    execv(server_argv[0], server_argv);
    fprintf(stderr, "execv failed: %s\n", strerror(errno));
    return 127;
}
