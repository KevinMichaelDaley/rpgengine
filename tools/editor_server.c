/**
 * @file editor_server.c
 * @brief Standalone editor server — runs editor context with tick loop.
 *
 * Usage: ./editor_server [--port PORT]
 * Default port: 9100
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/editor/editor_ctx.h"

static atomic_bool g_running = true;

static void sigint_handler_(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char **argv) {
    uint16_t port = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--port PORT]\n", argv[0]);
            return 0;
        }
    }

    signal(SIGINT, sigint_handler_);
    signal(SIGTERM, sigint_handler_);

    editor_ctx_config_t config = {.edit_port = port};
    editor_ctx_t ctx;

    if (!editor_ctx_init(&ctx, &config)) {
        fprintf(stderr, "editor_ctx_init failed\n");
        return 1;
    }

    printf("Editor server listening on port %u\n", ctx.io_thread.port);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    /* Main tick loop at ~60 Hz. */
    while (g_running) {
        uint32_t processed = editor_tick_drain(&ctx);
        if (processed > 0) {
            printf("  tick: processed %u commands\n", processed);
            fflush(stdout);
        }
        usleep(16000);  /* ~60 Hz */
    }

    printf("\nShutting down...\n");
    editor_ctx_shutdown(&ctx);
    return 0;
}
