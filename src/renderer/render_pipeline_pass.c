/**
 * @file render_pipeline_pass.c
 * @brief Per-pass accessors and draw list operations for render_pipeline_t.
 */

#include "ferrum/renderer/render_pipeline.h"
#include "ferrum/renderer/draw/draw_list.h"

/* ── render_pipeline_get_pass ─────────────────────────────────────── */

render_pass_t *render_pipeline_get_pass(render_pipeline_t *pipeline,
                                        render_pass_type_t type)
{
    if (!pipeline || !pipeline->passes) { return NULL; }
    if ((int)type < 0 || (int)type >= (int)pipeline->pass_count) {
        return NULL;
    }
    return &pipeline->passes[(int)type];
}

/* ── render_pipeline_clear_draw_lists ─────────────────────────────── */

void render_pipeline_clear_draw_lists(render_pipeline_t *pipeline)
{
    if (!pipeline || !pipeline->passes) { return; }
    for (size_t i = 0; i < pipeline->pass_count; ++i) {
        if (pipeline->passes[i].draw_list) {
            draw_list_clear(pipeline->passes[i].draw_list);
        }
    }
}
