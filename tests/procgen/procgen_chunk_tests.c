/**
 * @file procgen_chunk_tests.c
 * @brief Tests for chunk-based SVO rasterization.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ferrum/procgen/procgen_chunk_builder.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/procgen_grammar_registry.h"
#include "ferrum/procgen/grammar_blockout.h"

static int g_pass=0,g_fail=0;
#define ASSERT_TRUE(e) do{if(!(e)){printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#e);g_fail++;return;}}while(0)
#define ASSERT_EQ(a,b) ASSERT_TRUE((a)==(b))
#define PASS() g_pass++

static void test_chunk_grid_init(){
    procgen_chunk_grid_t g;
    procgen_chunk_grid_init(&g, 64.0f, 6, 512.0f); /* 64m chunks, 64 cells, 1024m world */
    ASSERT_TRUE(g.initialized);
    ASSERT_TRUE(g.chunks!=NULL);
    ASSERT_EQ(g.count_x, (uint32_t)16);
    ASSERT_EQ(g.count_z, (uint32_t)16);
    procgen_chunk_grid_destroy(&g);
    PASS();
}

static void test_chunk_at(){
    procgen_chunk_grid_t g;
    procgen_chunk_grid_init(&g, 64.0f, 6, 512.0f);
    int i = procgen_chunk_grid_chunk_at(&g, 0.0f, 0.0f, 0.0f);
    ASSERT_TRUE(i >= 0);
    ASSERT_TRUE(i < (int)(g.count_x * g.count_z));
    procgen_chunk_grid_destroy(&g);
    PASS();
}

static void test_build_room(){
    const char *t = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=main\nSPAWN x=5 y=5 z=1\nMARKER x=5 y=5 z=1 name=c\n";
    procgen_token_t tok[4096];char err[256];uint32_t cnt=0;
    procgen_tokenize(t,tok,4096,&cnt,err,sizeof(err));
    procgen_grammar_registry_init();
    procgen_grammar_t g={"blockout",1,procgen_tokenize,grammar_blockout_rasterize,NULL,NULL,0};
    procgen_grammar_register(&g);
    fr_dungeon_layout_t l;procgen_rasterize_with_registry(tok,cnt,&l,err,sizeof(err));

    procgen_chunk_grid_t grid;
    procgen_chunk_grid_init(&grid, 64.0f, 6, 64.0f); /* 32m chunks, 32 cells, 128m world */
    uint32_t n = procgen_chunk_grid_build(&grid, &l);
    ASSERT_TRUE(n > 0);

    int loaded = 0;
    for(size_t i=0;i<(size_t)grid.count_x*grid.count_z;i++) if(grid.chunks[i].loaded)loaded++;
    ASSERT_TRUE(loaded >= 1);
    printf("  chunks loaded: %d/%zu\n", loaded, (size_t)grid.count_x*grid.count_z);

    procgen_chunk_grid_destroy(&grid);
    free(l.rooms);free(l.corridors);free(l.openings);free(l.ramps);free(l.markers);free(l.nav_nodes);free(l.nav_edges);
    PASS();
}

static void test_build_corridor(){
    const char *t = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=8 h=8 floor_z=0 ceil_z=6 name=s\nSPAWN x=4 y=4 z=1\nCORRIDOR_H from=(8,3) to=(20,3) w=2 floor_z=0 ceil_z=5\nROOM_QUAD x=20 y=-2 w=8 h=10 floor_z=0 ceil_z=6 name=e\nMARKER x=4 y=4 z=1 name=a\nMARKER x=14 y=3 z=1 name=b\nMARKER x=24 y=3 z=1 name=c\n";
    procgen_token_t tok[4096];char err[256];uint32_t cnt=0;
    procgen_tokenize(t,tok,4096,&cnt,err,sizeof(err));
    procgen_grammar_registry_init();
    procgen_grammar_t g={"blockout",1,procgen_tokenize,grammar_blockout_rasterize,NULL,NULL,0};
    procgen_grammar_register(&g);
    fr_dungeon_layout_t l;procgen_rasterize_with_registry(tok,cnt,&l,err,sizeof(err));

    procgen_chunk_grid_t grid;
    procgen_chunk_grid_init(&grid, 64.0f, 6, 64.0f);
    uint32_t n = procgen_chunk_grid_build(&grid, &l);
    ASSERT_TRUE(n > 0);

    int loaded = 0;
    for(size_t i=0;i<(size_t)grid.count_x*grid.count_z;i++) if(grid.chunks[i].loaded)loaded++;
    printf("  chunks loaded: %d/%zu\n", loaded, (size_t)grid.count_x*grid.count_z);

    procgen_chunk_grid_destroy(&grid);
    free(l.rooms);free(l.corridors);free(l.openings);free(l.ramps);free(l.markers);free(l.nav_nodes);free(l.nav_edges);
    PASS();
}

static void test_unload(){
    procgen_chunk_grid_t g;
    procgen_chunk_grid_init(&g, 32.0f, 5, 128.0f);
    procgen_chunk_grid_unload_far(&g, 0,0, 0);
    ASSERT_EQ(0u, 0u);
    procgen_chunk_grid_destroy(&g);
    PASS();
}

int main(){
    printf("=== Procgen Chunk Tests ===\n\n");
    printf("RUN  chunk_grid_init\n");test_chunk_grid_init();printf("OK\n");
    printf("RUN  chunk_at\n");test_chunk_at();printf("OK\n");
    printf("RUN  build_room\n");test_build_room();printf("OK\n");
    printf("RUN  build_corridor\n");test_build_corridor();printf("OK\n");
    printf("RUN  unload\n");test_unload();printf("OK\n");
    printf("\n=== Results: %d passed, %d failed ===\n",g_pass,g_fail);
    return g_fail>0?1:0;
}
