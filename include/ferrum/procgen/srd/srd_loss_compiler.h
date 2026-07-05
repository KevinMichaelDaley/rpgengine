#ifndef FERRUM_PROCGEN_SRD_LOSS_COMPILER_H
#define FERRUM_PROCGEN_SRD_LOSS_COMPILER_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SRD_MAX_LABELS 4
#define SRD_MAX_TERMS 64

typedef struct {
    fr_loss_primitive_t primitive;
    uint32_t            label_indices[SRD_MAX_LABELS];
    float               target_value;
    int                 op;  /* 0=>, 1=<, 2== */
    int                 all_rooms;
    char                type_filter;
} srd_loss_term_t;

/**
 * @brief Compile a LOSS: string into structured loss terms.
 *
 * Parses the VLM-emitted LOSS block, resolves room labels using
 * the provided RoomGraph, and produces an array of srd_loss_term_t.
 *
 * @param loss      The LOSS: block string.
 * @param graph     RoomGraph for label resolution (or NULL).
 * @param n_rooms   Number of rooms (for all_rooms expansion).
 * @param terms_out Output array.
 * @param cap       Capacity of terms_out.
 * @param count_out Number of terms written.
 * @return 0 on success, -1 on parse error.
 */
int srd_loss_compile(const char *loss,
                     const fr_room_graph_t *graph,
                     uint32_t n_rooms,
                     srd_loss_term_t *terms_out,
                     uint32_t cap, uint32_t *count_out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_LOSS_COMPILER_H */
