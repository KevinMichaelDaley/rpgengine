/**
 * @file scene_panel.c
 * @brief Panel layout engine for the scene editor.
 *
 * Computes panel rectangles from divider fractions and window size.
 * No dynamic allocation — all state lives in panel_layout_t.
 */

#include "ferrum/editor/scene/scene_panel.h"

#include <string.h>

/* ---- Internal helpers ---- */

/**
 * @brief Clamp a float to [lo, hi].
 */
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Compute the minimum divider fraction for a given dimension.
 *
 * Ensures at least PANEL_MIN_SIZE pixels on each side.
 */
static float min_frac(int window_dim) {
    if (window_dim <= 0) return 0.0f;
    return (float)PANEL_MIN_SIZE / (float)window_dim;
}

/* ---- Lifecycle ---- */

void panel_layout_init(panel_layout_t *layout, int window_w, int window_h) {
    memset(layout, 0, sizeof(*layout));
    layout->window_w = window_w;
    layout->window_h = window_h;

    /* Default divider positions */
    layout->divider_pos[DIVIDER_LEFT]   = 0.22f;  /* 22% from left */
    layout->divider_pos[DIVIDER_RIGHT]  = 0.80f;  /* 80% from left */
    layout->divider_pos[DIVIDER_BOTTOM] = 0.65f;  /* 65% from top */

    /* All panels visible by default */
    for (int i = 0; i < PANEL_COUNT; ++i) {
        layout->visible[i] = true;
    }

    /* Default focus on viewport */
    layout->focus = PANEL_VIEWPORT;
}

/* ---- Queries ---- */

panel_rect_t panel_layout_get_rect(const panel_layout_t *layout, panel_id_t id) {
    panel_rect_t r = {0, 0, 0, 0};

    if (id < 0 || id >= PANEL_COUNT) return r;
    if (!layout->visible[id]) return r;

    int w = layout->window_w;
    int h = layout->window_h;
    if (w <= 0 || h <= 0) return r;

    /* Compute pixel positions of dividers */
    int left_x  = (int)(layout->divider_pos[DIVIDER_LEFT] * (float)w);
    int right_x = (int)(layout->divider_pos[DIVIDER_RIGHT] * (float)w);
    int bot_y   = (int)(layout->divider_pos[DIVIDER_BOTTOM] * (float)h);

    /* Adjust for hidden panels */
    if (!layout->visible[PANEL_OUTLINER]) {
        left_x = 0;
    }
    if (!layout->visible[PANEL_INSPECTOR]) {
        right_x = w;
    }

    switch (id) {
    case PANEL_OUTLINER:
        r.x = 0;
        r.y = 0;
        r.w = left_x;
        r.h = h;
        break;

    case PANEL_VIEWPORT:
        r.x = left_x;
        r.y = 0;
        r.w = right_x - left_x;
        r.h = bot_y;
        break;

    case PANEL_INSPECTOR:
        r.x = right_x;
        r.y = 0;
        r.w = w - right_x;
        r.h = h;
        break;

    case PANEL_TUI:
        r.x = left_x;
        r.y = bot_y;
        r.w = right_x - left_x;
        r.h = h - bot_y;
        break;

    default:
        break;
    }

    /* Clamp to non-negative */
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;

    return r;
}

bool panel_layout_is_visible(const panel_layout_t *layout, panel_id_t id) {
    if (id < 0 || id >= PANEL_COUNT) return false;
    return layout->visible[id];
}

panel_id_t panel_layout_get_focus(const panel_layout_t *layout) {
    return layout->focus;
}

panel_id_t panel_layout_hit_test(const panel_layout_t *layout, int x, int y) {
    /* Check each visible panel in order */
    for (int i = 0; i < PANEL_COUNT; ++i) {
        if (!layout->visible[i]) continue;
        panel_rect_t r = panel_layout_get_rect(layout, (panel_id_t)i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return (panel_id_t)i;
        }
    }
    return PANEL_VIEWPORT; /* fallback */
}

divider_id_t panel_layout_divider_hit_test(const panel_layout_t *layout,
                                            int x, int y) {
    int w = layout->window_w;
    int h = layout->window_h;
    if (w <= 0 || h <= 0) return DIVIDER_NONE;

    /* Grab zone: 5px on each side of divider */
    const int grab = 5;

    int left_x  = (int)(layout->divider_pos[DIVIDER_LEFT] * (float)w);
    int right_x = (int)(layout->divider_pos[DIVIDER_RIGHT] * (float)w);
    int bot_y   = (int)(layout->divider_pos[DIVIDER_BOTTOM] * (float)h);

    /* Left vertical divider */
    if (layout->visible[PANEL_OUTLINER] &&
        x >= left_x - grab && x <= left_x + grab) {
        return DIVIDER_LEFT;
    }

    /* Right vertical divider */
    if (layout->visible[PANEL_INSPECTOR] &&
        x >= right_x - grab && x <= right_x + grab) {
        return DIVIDER_RIGHT;
    }

    /* Horizontal divider (only in center column) */
    if (x >= left_x && x <= right_x &&
        y >= bot_y - grab && y <= bot_y + grab) {
        return DIVIDER_BOTTOM;
    }

    return DIVIDER_NONE;
}

/* ---- Mutations ---- */

void panel_layout_resize(panel_layout_t *layout, int new_w, int new_h) {
    layout->window_w = new_w;
    layout->window_h = new_h;
    /* Divider fractions are preserved — rects scale automatically. */
}

void panel_layout_drag_divider(panel_layout_t *layout, divider_id_t div,
                                int delta) {
    if (div < 0 || div >= DIVIDER_COUNT) return;

    int dim = (div == DIVIDER_BOTTOM) ? layout->window_h : layout->window_w;
    if (dim <= 0) return;

    float frac_delta = (float)delta / (float)dim;
    float new_pos = layout->divider_pos[div] + frac_delta;

    float mf = min_frac(dim);

    switch (div) {
    case DIVIDER_LEFT:
        /* Clamp: must leave PANEL_MIN_SIZE for outliner and viewport */
        new_pos = clampf(new_pos, mf,
                         layout->divider_pos[DIVIDER_RIGHT] - mf);
        break;

    case DIVIDER_RIGHT:
        /* Clamp: must leave PANEL_MIN_SIZE for viewport and inspector */
        new_pos = clampf(new_pos,
                         layout->divider_pos[DIVIDER_LEFT] + mf,
                         1.0f - mf);
        break;

    case DIVIDER_BOTTOM:
        /* Clamp: must leave space for viewport (above) and TUI (below) */
        new_pos = clampf(new_pos, mf, 1.0f - mf);
        break;

    default:
        break;
    }

    layout->divider_pos[div] = new_pos;
}

void panel_layout_toggle(panel_layout_t *layout, panel_id_t id) {
    if (id < 0 || id >= PANEL_COUNT) return;

    layout->visible[id] = !layout->visible[id];

    /* If we hid the focused panel, move focus to viewport */
    if (!layout->visible[id] && layout->focus == id) {
        layout->focus = PANEL_VIEWPORT;
    }
}

/* ---- Focus ---- */

void panel_layout_set_focus(panel_layout_t *layout, panel_id_t id) {
    if (id < 0 || id >= PANEL_COUNT) return;
    if (!layout->visible[id]) return;
    layout->focus = id;
}

void panel_layout_focus_next(panel_layout_t *layout) {
    /* Cycle through visible panels */
    int start = (int)layout->focus;
    for (int i = 1; i <= PANEL_COUNT; ++i) {
        int next = (start + i) % PANEL_COUNT;
        if (layout->visible[next]) {
            layout->focus = (panel_id_t)next;
            return;
        }
    }
    /* All hidden — stay put */
}

void panel_layout_focus_viewport(panel_layout_t *layout) {
    layout->focus = PANEL_VIEWPORT;
}
