/**
 * @file render_pipeline_init.c
 * @brief Full 9-pass pipeline init and destroy.
 */

#include "ferrum/renderer/render_pipeline.h"
#include "ferrum/renderer/draw/draw_list.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ── render_pipeline_init ─────────────────────────────────────────── */

int render_pipeline_init(render_pipeline_t *pipeline,
                         uint32_t draw_list_capacity)
{
    if (!pipeline || draw_list_capacity == 0) {
        return RENDER_PIPELINE_ERR_INVALID;
    }

    /*
     * Single allocation:
     *   passes[]     — RENDER_PASS_TYPE_COUNT × render_pass_t
     *   draw_lists[] — RENDER_PASS_TYPE_COUNT × draw_list_t
     */
    size_t sz_passes = RENDER_PASS_TYPE_COUNT * sizeof(render_pass_t);
    size_t sz_lists  = RENDER_PASS_TYPE_COUNT * sizeof(draw_list_t);
    uint8_t *block = (uint8_t *)malloc(sz_passes + sz_lists);
    if (!block) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    memset(block, 0, sz_passes + sz_lists);

    render_pass_t *passes = (render_pass_t *)block;
    draw_list_t *lists    = (draw_list_t *)(block + sz_passes);

    /* Initialize each pass and its draw list. */
    for (int i = 0; i < RENDER_PASS_TYPE_COUNT; ++i) {
        passes[i].name      = render_pass_type_name((render_pass_type_t)i);
        passes[i].pass_type = (render_pass_type_t)i;
        passes[i].draw_list = &lists[i];

        int rc = draw_list_init(&lists[i], draw_list_capacity);
        if (rc != DRAW_LIST_OK) {
            /* Rollback: destroy already-initialized draw lists. */
            for (int j = 0; j < i; ++j) {
                draw_list_destroy(&lists[j]);
            }
            free(block);
            return RENDER_PIPELINE_ERR_INVALID;
        }
    }

    pipeline->passes            = passes;
    pipeline->pass_count        = RENDER_PASS_TYPE_COUNT;
    pipeline->glBindFramebuffer = NULL;
    pipeline->owns_storage      = 1;

    return RENDER_PIPELINE_OK;
}

/* ── render_pipeline_destroy ──────────────────────────────────────── */

void render_pipeline_destroy(render_pipeline_t *pipeline)
{
    if (!pipeline || !pipeline->passes) { return; }

    if (pipeline->owns_storage) {
        /* Destroy all draw lists. */
        for (size_t i = 0; i < pipeline->pass_count; ++i) {
            if (pipeline->passes[i].draw_list) {
                draw_list_destroy(pipeline->passes[i].draw_list);
            }
        }
        /* Free the single backing block. */
        free(pipeline->passes);
    }

    pipeline->passes       = NULL;
    pipeline->pass_count   = 0;
    pipeline->owns_storage = 0;
}
