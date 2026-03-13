/**
 * @file scene_panel_persist.c
 * @brief Panel layout save/load — persists divider positions and visibility.
 *
 * File format (plain text):
 *   dividers <left> <right> <bottom>
 *   visible <outliner> <viewport> <inspector> <tui>
 */

#include "ferrum/editor/scene/scene_panel.h"

#include <stdio.h>

bool panel_layout_save(const panel_layout_t *layout, const char *path) {
    if (!layout || !path) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "dividers %.6f %.6f %.6f\n",
            layout->divider_pos[DIVIDER_LEFT],
            layout->divider_pos[DIVIDER_RIGHT],
            layout->divider_pos[DIVIDER_BOTTOM]);
    fprintf(f, "visible %d %d %d %d\n",
            (int)layout->visible[PANEL_OUTLINER],
            (int)layout->visible[PANEL_VIEWPORT],
            (int)layout->visible[PANEL_INSPECTOR],
            (int)layout->visible[PANEL_TUI]);

    fclose(f);
    return true;
}

bool panel_layout_load(panel_layout_t *layout, const char *path) {
    if (!layout || !path) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    float left = 0, right = 0, bottom = 0;
    int v0 = 0, v1 = 0, v2 = 0, v3 = 0;

    /* Parse dividers line */
    if (fscanf(f, "dividers %f %f %f ", &left, &right, &bottom) != 3) {
        fclose(f);
        return false;
    }

    /* Parse visible line */
    if (fscanf(f, "visible %d %d %d %d", &v0, &v1, &v2, &v3) != 4) {
        fclose(f);
        return false;
    }

    fclose(f);

    /* Validate ranges */
    if (left < 0.0f || left > 1.0f) return false;
    if (right < 0.0f || right > 1.0f) return false;
    if (bottom < 0.0f || bottom > 1.0f) return false;

    layout->divider_pos[DIVIDER_LEFT]   = left;
    layout->divider_pos[DIVIDER_RIGHT]  = right;
    layout->divider_pos[DIVIDER_BOTTOM] = bottom;
    layout->visible[PANEL_OUTLINER]  = (v0 != 0);
    layout->visible[PANEL_VIEWPORT]  = (v1 != 0);
    layout->visible[PANEL_INSPECTOR] = (v2 != 0);
    layout->visible[PANEL_TUI]       = (v3 != 0);

    return true;
}
