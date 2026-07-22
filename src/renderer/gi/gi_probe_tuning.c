/**
 * @file gi_probe_tuning.c
 * @brief Probe-GI tuning defaults + install (rpg-2vfm). These values are the ones
 *        the GI_* environment variables used to default to; they now live in
 *        render_config and travel down to the probe compute, with the env vars
 *        retained only as a live-tuning override.
 */
#include <string.h>

#include "ferrum/renderer/gi/gi_probe_gpu.h"

void gi_probe_tuning_defaults(gi_probe_tuning_t *t)
{
    if (t == NULL) return;
    memset(t, 0, sizeof *t);
    t->field_on = 1;            /* DDGI recurrent gather on (GI_FIELD). */
    t->mis = 0;                 /* GI_MIS */
    t->hybrid = 0;              /* GI_HYBRID */
    t->hero = 2;                /* GI_HERO */
    t->samples = 24;            /* GI_SAMPLES */
    t->spec_lobes = 2;          /* GI_SG_LOBES */
    t->update_interval = 8;     /* GI_UPDATE */
    t->n_probe_groups = 1;      /* GI_PROBE_GROUPS */
    t->bounce = 0.9f;           /* GI_BOUNCE -- steady state = 1/(1-bounce). */
    t->near_dist = 2.2f;        /* GI_NEAR */
    t->dmax = 2.5f;             /* GI_DMAX */
    t->emin = 0.02f;            /* GI_EMIN */
    t->norm_gate = 0.75f;       /* GI_NORM_GATE */
    t->stat_scale = 1.0f;       /* GI_STAT_SCALE */
    t->dyn_gain = 1.0f;         /* GI_DYN_GAIN */
    t->smooth = 0.15f;          /* GI_SMOOTH */
    t->vis_bias = 0.30f;        /* GI_VIS_BIAS */
    t->vis_varmin = 0.02f;      /* GI_VIS_VARMIN */
    t->vis_sharp = 1.0f;        /* GI_VIS_SHARP */
}

void gi_probe_gpu_set_tuning(gi_probe_gpu_t *g, const gi_probe_tuning_t *t)
{
    if (g == NULL) return;
    if (t != NULL) g->tuning = *t;
    else gi_probe_tuning_defaults(&g->tuning);
}
