/**
 * @file procgen_level_load.c
 * @brief Load a procgen level: token string → layout → SVO → mesh.
 */

#include "ferrum/procgen/procgen_level_load.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void procgen_level_init(procgen_level_t *lvl) {
    memset(lvl, 0, sizeof(*lvl));
    procgen_raster_config_default(&lvl->config);
    procgen_mesh_init(&lvl->mesh);
}

void procgen_level_free(procgen_level_t *lvl) {
    procgen_mesh_destroy(&lvl->mesh);
    free(lvl->layout.rooms);
    free(lvl->layout.corridors);
    free(lvl->layout.openings);
    free(lvl->layout.ramps);
    free(lvl->layout.markers);
    free(lvl->layout.nav_nodes);
    free(lvl->layout.nav_edges);
    memset(lvl, 0, sizeof(*lvl));
}

int procgen_level_load_string(procgen_level_t *lvl, const char *grammar, const char *token_str) {
    if (!lvl || !grammar || !token_str) return -1;

    /* Tokenize. */
    procgen_token_t tokens[16384];
    char err[1024];
    uint32_t count = 0;
    tok_error_t tr = procgen_tokenize(token_str, tokens, 16384, &count, err, sizeof(err));
    if (tr != TOK_ERR_NONE) {
        fprintf(stderr, "procgen_level_load: tokenize failed: %s\n", err);
        return -1;
    }

    /* Rasterize via registry. */
    procgen_grammar_registry_init();
    procgen_grammar_t g;
    memset(&g, 0, sizeof(g));
    g.name = grammar;
    g.version = 1;
    g.tokenize = procgen_tokenize;
    g.rasterize = grammar_blockout_rasterize;
    procgen_grammar_register(&g);

    if (procgen_rasterize_with_registry(tokens, count, &lvl->layout, err, sizeof(err)) != 0) {
        fprintf(stderr, "procgen_level_load: rasterize failed: %s\n", err);
        return -1;
    }

    /* Build SVO. */
    npc_svo_grid_t svo;
    uint32_t solid = procgen_svo_build_cfg(&lvl->config, &lvl->layout, &svo);
    printf("procgen: SVO built with %u solid voxels\n", solid);

    if (solid == 0) {
        fprintf(stderr, "procgen_level_load: no solid voxels generated\n");
        npc_svo_grid_destroy(&svo);
        return -1;
    }

    /* Generate mesh. */
    uint32_t tris = procgen_mesh_from_svo(&svo, &lvl->mesh);
    printf("procgen: mesh generated with %u triangles (%u vertices)\n",
           tris, lvl->mesh.vertex_count / 3);

    npc_svo_grid_destroy(&svo);
    lvl->ok = 1;
    return 0;
}

int procgen_level_load(procgen_level_t *lvl, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    /* If it's JSON, extract the raw_token_string field. Otherwise use as-is. */
    const char *token_str = buf;
    /* Simple extraction: look for "raw_token_string":"..." or just use the
       entire file as a token string if it starts with @grammar. */
    if (strstr(buf, "@grammar") == buf) {
        /* Already a token string file. */
    } else {
        /* Try to extract from JSON. */
        const char *key = "\"raw_token_string\":\"";
        const char *start = strstr(buf, key);
        if (start) {
            start += strlen(key);
            const char *end = strchr(start, '"');
            if (end) {
                size_t len = (size_t)(end - start);
                memmove(buf, start, len);
                buf[len] = '\0';
                token_str = buf;
            }
        }
    }

    /* Unescape JSON string. */
    char unescaped[65536];
    const char *s = token_str;
    char *d = unescaped;
    while (*s && (size_t)(d - unescaped) < sizeof(unescaped) - 1) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n': *d++ = '\n'; break;
            case 'r': *d++ = '\r'; break;
            case 't': *d++ = '\t'; break;
            case '"': *d++ = '"';  break;
            case '\\': *d++ = '\\'; break;
            default: *d++ = '\\'; *d++ = *s; break;
            }
            s++;
        } else {
            *d++ = *s++;
        }
    }
    *d = '\0';

    int rc = procgen_level_load_string(lvl, "blockout", unescaped);
    free(buf);
    return rc;
}
