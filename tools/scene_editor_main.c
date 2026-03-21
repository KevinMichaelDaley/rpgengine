/**
 * @file scene_editor_main.c
 * @brief Scene editor launcher.
 *
 * Usage: ./build/scene_editor [--width W] [--height H] [--host H] [--port P]
 */

#include "ferrum/editor/scene/scene_main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    scene_editor_config_t config = {0};

    /* Parse command-line arguments */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            config.window_w = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            config.window_h = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            config.server_host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.server_port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            config.ui_scale = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--asset-dir") == 0 && i + 1 < argc) {
            config.asset_dir = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--width W] [--height H] "
                   "[--host HOST] [--port PORT] [--scale S] "
                   "[--asset-dir DIR]\n", argv[0]);
            return 0;
        }
    }

    scene_editor_t editor = {0};
    if (!scene_editor_init(&editor, &config)) {
        fprintf(stderr, "Failed to initialize scene editor\n");
        return 1;
    }

    scene_editor_run(&editor);
    scene_editor_shutdown(&editor);

    return 0;
}
