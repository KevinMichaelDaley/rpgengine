#include "ferrum/procgen/srd/srd_loss_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int resolve_label(const char *label, const fr_room_graph_t *graph) {
    if (!graph || !label || !label[0]) return -1;

    /* Exact match on label */
    for (uint32_t i = 0; i < graph->node_count; i++) {
        if (strcmp(graph->nodes[i].label, label) == 0)
            return (int)i;
    }

    /* Match on type_char */
    if (strlen(label) == 1) {
        for (uint32_t i = 0; i < graph->node_count; i++) {
            if (graph->nodes[i].type_char == label[0])
                return (int)i;
        }
    }

    return -1;
}

int srd_loss_compile(const char *loss,
                     const fr_room_graph_t *graph,
                     uint32_t n_rooms,
                     srd_loss_term_t *terms_out,
                     uint32_t cap, uint32_t *count_out) {
    if (!loss || !terms_out || !count_out || cap == 0) return -1;
    (void)n_rooms;

    uint32_t n = 0;
    const char *p = loss;

    /* Skip to LOSS: header */
    const char *header = strstr(p, "LOSS:");
    if (!header) header = strstr(p, "Loss:");
    if (!header) return -1;
    p = header + 5;  /* skip "LOSS:" */

    char line[256];
    while (*p && n < cap) {
        /* Skip whitespace */
        while (*p && (*p == '\n' || *p == '\r' || *p == ' ')) p++;
        if (*p == '\0') break;

        /* Read one line */
        size_t len = 0;
        while (*p && *p != '\n' && *p != '\r' && len < sizeof(line) - 1)
            line[len++] = *p++;
        line[len] = '\0';
        if (len == 0) { if (*p) p++; continue; }

        /* Strip trailing spaces */
        while (len > 0 && (line[len-1] == ' ' || line[len-1] == '\t'))
            line[--len] = '\0';
        if (len == 0) { if (*p) p++; continue; }

        srd_loss_term_t *t = &terms_out[n];
        memset(t, 0, sizeof(*t));
        for (int i = 0; i < 4; i++) t->label_indices[i] = (uint32_t)-1;

        char name[64], arg1[64], arg2[64];
        name[0] = arg1[0] = arg2[0] = '\0';
        float val = 0.0f;
        char op_char = 0;

        /* Parse: Primitive(arg1 [, arg2]) [>|<|=] [value] */
        int parsed = sscanf(line, "%63[a-zA-Z_](%63[^)]) %c %f",
                            name, arg1, &op_char, &val);
        if (parsed < 2) {
            /* Try without operator: Primitive(arg1 [, arg2]) */
            parsed = sscanf(line, "%63[a-zA-Z_](%63[^)])", name, arg1);
            if (parsed < 2) continue;
        }
        /* Split arg1 on comma for two-arg primitives */
        char *comma = strchr(arg1, ',');
        if (comma) {
            *comma = '\0';
            strncpy(arg2, comma + 1, sizeof(arg2)-1);
            /* Trim leading space from arg2 */
            char *ap = arg2;
            while (*ap == ' ') ap++;
            if (ap != arg2) memmove(arg2, ap, strlen(ap) + 1);
        }

        /* Trim trailing space from arg1 */
        size_t alen = strlen(arg1);
        while (alen > 0 && arg1[alen-1] == ' ') arg1[--alen] = '\0';

        /* Identify primitive */
        if (strcmp(name, "PathDistance") == 0)        t->primitive = FR_LOSS_PATH_DISTANCE;
        else if (strcmp(name, "LineOfSight") == 0)    t->primitive = FR_LOSS_LINE_OF_SIGHT;
        else if (strcmp(name, "NonPenetration") == 0) t->primitive = FR_LOSS_NON_PENETRATION;
        else if (strcmp(name, "MinimumSize") == 0)    t->primitive = FR_LOSS_MINIMUM_SIZE;
        else if (strcmp(name, "Separation") == 0)     t->primitive = FR_LOSS_SEPARATION;
        else if (strcmp(name, "Containment") == 0)    t->primitive = FR_LOSS_CONTAINMENT;
        else if (strcmp(name, "AdjacencyCount") == 0) t->primitive = FR_LOSS_ADJACENCY_COUNT;
        else if (strcmp(name, "HeightSpan") == 0)     t->primitive = FR_LOSS_HEIGHT_SPAN;
        else if (strcmp(name, "StairAlignment") == 0) t->primitive = FR_LOSS_STAIR_ALIGNMENT;
        else if (strcmp(name, "FloorAccessibility") == 0) t->primitive = FR_LOSS_FLOOR_ACCESSIBILITY;
        else continue;

        /* Resolve labels */
        if (strcmp(arg1, "all") == 0) {
            t->all_rooms = 1;
        } else {
            t->label_indices[0] = resolve_label(arg1, graph);
        }

        if (arg2[0]) {
            if (strcmp(arg2, "all") == 0)
                t->all_rooms = 1;
            else
                t->label_indices[1] = resolve_label(arg2, graph);
        }

        /* Operator and target value */
        t->op = (op_char == '<') ? 1 : (op_char == '=' ? 2 : 0);
        t->target_value = val;

        n++;
    }

    *count_out = n;
    return 0;
}
