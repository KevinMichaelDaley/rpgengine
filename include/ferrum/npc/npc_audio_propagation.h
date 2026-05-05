/**
 * @file npc_audio_propagation.h
 * @brief Audio propagation graph for SENSE_QUERY auditory channel.
 *
 * Provides a distance-based attenuation model backed by a graph
 * structure that will eventually hold beam-traced connectivity
 * between navmesh sample points.  The current stub uses inverse-
 * square attenuation plus material absorption.
 */
#ifndef FERRUM_NPC_AUDIO_PROPAGATION_H
#define FERRUM_NPC_AUDIO_PROPAGATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/math/vec3.h"
#include <math.h>
#include <stdint.h>

/* ── Constants ──────────────────────────────────────────────────── */

/** Attenuation (dB) above which sound is considered inaudible. */
#define NPC_AUDIO_INAUDIBLE_THRESHOLD 60.0f

/* ── Acoustic material ──────────────────────────────────────────── */

typedef struct npc_acoustic_material {
    float absorption_low;    /* 0.0-1.0 @ 125 Hz  */
    float absorption_mid;    /* 0.0-1.0 @ 1 kHz   */
    float absorption_high;   /* 0.0-1.0 @ 8 kHz   */
    float scattering;        /* 0.0-1.0            */
    float transmission;      /* 0.0-1.0            */
} npc_acoustic_material_t;

/** Preset material definitions. */
#define NPC_ACOUSTIC_MATERIAL_WOOD  {0.11f, 0.10f, 0.07f, 0.15f, 0.03f}
#define NPC_ACOUSTIC_MATERIAL_STONE {0.01f, 0.01f, 0.01f, 0.05f, 0.00f}
#define NPC_ACOUSTIC_MATERIAL_METAL {0.01f, 0.01f, 0.01f, 0.05f, 0.00f}
#define NPC_ACOUSTIC_MATERIAL_GLASS {0.03f, 0.02f, 0.01f, 0.05f, 0.10f}
#define NPC_ACOUSTIC_MATERIAL_SNOW  {0.80f, 0.90f, 0.95f, 0.20f, 0.00f}
#define NPC_ACOUSTIC_MATERIAL_ICE   {0.02f, 0.02f, 0.02f, 0.03f, 0.01f}
#define NPC_ACOUSTIC_MATERIAL_FLESH {0.40f, 0.35f, 0.30f, 0.10f, 0.05f}
#define NPC_ACOUSTIC_MATERIAL_AIR   {0.00f, 0.00f, 0.00f, 0.00f, 1.00f}

/* ── Graph types ────────────────────────────────────────────────── */

typedef struct npc_audio_graph_edge {
    uint32_t to_node_id;
    float    attenuation_db;
    float    path_length;
    uint32_t material_hits;
} npc_audio_graph_edge_t;

typedef struct npc_audio_graph_node {
    uint32_t               node_id;
    vec3_t                 position;
    uint32_t               edge_count;
    uint32_t               edge_cap;
    npc_audio_graph_edge_t *edges;
} npc_audio_graph_node_t;

typedef struct npc_audio_graph {
    npc_audio_graph_node_t *nodes;
    uint32_t                node_count;
    uint32_t                node_cap;
    npc_acoustic_material_t medium;
} npc_audio_graph_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

void npc_audio_graph_init(npc_audio_graph_t *graph);
void npc_audio_graph_destroy(npc_audio_graph_t *graph);

/* ── Query ──────────────────────────────────────────────────────── */

/**
 * @brief Query attenuation between two world positions.
 *
 * Uses inverse-square law: attenuation = 20*log10(distance) +
 * medium absorption (mid-band).  Returns NPC_AUDIO_INAUDIBLE_THRESHOLD
 * if the computed attenuation exceeds it.
 *
 * @param graph   Propagation graph (may be NULL for pure air).
 * @param a       Source position.
 * @param b       Listener position.
 * @return Attenuation in dB, clamped to NPC_AUDIO_INAUDIBLE_THRESHOLD.
 */
float npc_audio_graph_query(const npc_audio_graph_t *graph,
                            vec3_t a, vec3_t b);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_AUDIO_PROPAGATION_H */
