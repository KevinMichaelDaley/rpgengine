/**
 * @file editor_tui.c
 * @brief Standalone editor TUI controller.
 *
 * Terminal-based editor UI that connects to the editor server
 * and sends commands. Uses raw terminal mode for key input.
 *
 * Usage: ./editor_tui [host:port]
 * Default: 127.0.0.1:9100
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <poll.h>

#include "ferrum/editor/ctrl_conn.h"
#include "ferrum/editor/ctrl_tui.h"

static atomic_bool g_running = true;
static struct termios g_orig_termios;

static void sigint_handler_(int sig) {
    (void)sig;
    g_running = false;
}

static void restore_terminal_(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    /* Show cursor and reset. */
    printf("\033[?25h\033[0m\n");
}

static bool raw_mode_(void) {
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) < 0) return false;
    atexit(restore_terminal_);

    struct termios raw = g_orig_termios;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_cflag |= (unsigned)(CS8);
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    uint16_t port = 9100;

    if (argc > 1) {
        /* Parse host:port */
        char *colon = strchr(argv[1], ':');
        if (colon) {
            static char host_buf[256];
            size_t hlen = (size_t)(colon - argv[1]);
            if (hlen >= sizeof(host_buf)) hlen = sizeof(host_buf) - 1;
            memcpy(host_buf, argv[1], hlen);
            host_buf[hlen] = '\0';
            host = host_buf;
            port = (uint16_t)atoi(colon + 1);
        } else {
            port = (uint16_t)atoi(argv[1]);
        }
    }

    signal(SIGINT, sigint_handler_);
    signal(SIGTERM, sigint_handler_);

    /* Connect to editor server. */
    ctrl_conn_t conn;
    ctrl_conn_init(&conn);

    printf("Connecting to %s:%u...\n", host, port);
    if (!ctrl_conn_connect(&conn, host, port)) {
        fprintf(stderr, "Failed to connect to %s:%u\n", host, port);
        return 1;
    }
    printf("Connected!\n");
    usleep(200000);

    /* Init TUI. */
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    {
        char msg[256];
        snprintf(msg, sizeof(msg), "Connected to %s:%u", host, port);
        ctrl_log_add(&tui.log, 0, msg);
    }
    ctrl_log_add(&tui.log, 0, "Type :command and press Enter");
    ctrl_log_add(&tui.log, 0, "Commands: spawn, delete, move, rotate, scale, save, load");
    ctrl_log_add(&tui.log, 0, "Press 'q' to quit, ':' to enter command mode");

    /* Enter raw terminal mode. */
    if (!raw_mode_()) {
        fprintf(stderr, "Failed to set raw terminal mode\n");
        ctrl_conn_disconnect(&conn);
        return 1;
    }

    /* Get terminal size. */
    {
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
            tui.cols = ws.ws_col;
            tui.rows = ws.ws_row;
        }
    }

    /* Main loop. */
    while (g_running) {
        /* Poll stdin + server socket. */
        struct pollfd fds[2];
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = conn.fd;
        fds[1].events = POLLIN;

        int ready = poll(fds, 2, 50);  /* 50ms timeout → ~20 Hz refresh */

        /* Handle keyboard input. */
        if (ready > 0 && (fds[0].revents & POLLIN)) {
            char ch;
            while (read(STDIN_FILENO, &ch, 1) == 1) {
                /* Quit on 'q' in normal mode. */
                if (tui.mode == CTRL_MODE_NORMAL && ch == 'q') {
                    g_running = false;
                    break;
                }

                const char *cmd = ctrl_tui_feed_key(&tui, ch);
                if (cmd && cmd[0] != '\0') {
                    /* User submitted a command — send to server. */
                    char msg[512];
                    snprintf(msg, sizeof(msg), "> %s", cmd);
                    ctrl_log_add(&tui.log, 0, msg);
                    ctrl_conn_send_cmd(&conn, cmd);
                }
            }
        }

        /* Handle server responses. */
        if (ready > 0 && (fds[1].revents & POLLIN)) {
            if (ctrl_conn_recv(&conn)) {
                char line[4096];
                uint32_t len;
                while ((len = ctrl_conn_pop_line(&conn, line, sizeof(line))) > 0) {
                    char msg[4200];
                    snprintf(msg, sizeof(msg), "< %s", line);
                    ctrl_log_add(&tui.log, 0, msg);
                }
            } else if (conn.state == CTRL_CONN_ERROR) {
                ctrl_log_add(&tui.log, 2, "Server disconnected");
                g_running = false;
            }
        }

        /* Render TUI. */
        ctrl_tui_render(&tui);
    }

    ctrl_tui_destroy(&tui);
    ctrl_conn_disconnect(&conn);
    return 0;
}
