/**
 * @file client_asset_download_tests.c
 * @brief Tests for client-side asset downloader.
 *
 * Spins up a real asset server, connects with the download client,
 * and verifies the full binary protocol round-trip.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ferrum/editor/edit_asset_registry.h"
#include "ferrum/editor/assets/edit_asset_serve.h"
#include "ferrum/editor/client/client_asset_download.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

static char g_tmpdir[256];

static void make_tmpdir_(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/asset_dl_test_XXXXXX");
    char *r = mkdtemp(g_tmpdir);
    (void)r;
}

static void write_file_(const char *dir, const char *rel,
                         const char *data, size_t len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, rel);
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + strlen(dir) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(data, 1, len, f); fclose(f); }
}

static void rm_tmpdir_(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_tmpdir);
    int r = system(cmd);
    (void)r;
}

typedef struct test_ctx {
    edit_asset_registry_t reg;
    edit_asset_server_t   server;
    asset_download_t      dl;
} test_ctx_t;

static bool setup_(test_ctx_t *t) {
    make_tmpdir_();
    write_file_(g_tmpdir, "meshes/cube.glb", "GLBCUBEDATA", 11);
    write_file_(g_tmpdir, "textures/stone.png", "PNGSTONEDATAXX", 14);

    edit_asset_registry_init(&t->reg, 64);
    edit_asset_registry_scan(&t->reg, g_tmpdir);
    if (!edit_asset_server_start(&t->server, 0, &t->reg, g_tmpdir))
        return false;
    usleep(50000);

    asset_download_init(&t->dl);
    if (!asset_download_connect(&t->dl, "127.0.0.1", t->server.port))
        return false;
    return true;
}

static void teardown_(test_ctx_t *t) {
    asset_download_disconnect(&t->dl);
    edit_asset_server_stop(&t->server);
    edit_asset_registry_destroy(&t->reg);
    rm_tmpdir_();
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Download a file that exists. */
static bool test_download_ok(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    asset_download_result_t res;
    ASSERT(asset_download_request(&t.dl, "meshes/cube.glb", &res));
    ASSERT(res.status == ASSET_DL_OK);
    ASSERT(res.size == 11);
    ASSERT(memcmp(res.data, "GLBCUBEDATA", 11) == 0);
    free(res.data);

    teardown_(&t);
    return true;
}

/** Request a file that doesn't exist. */
static bool test_download_not_found(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    asset_download_result_t res;
    ASSERT(asset_download_request(&t.dl, "meshes/nope.glb", &res));
    ASSERT(res.status == ASSET_DL_NOT_FOUND);
    ASSERT(res.size == 0);
    ASSERT(res.data == NULL);

    teardown_(&t);
    return true;
}

/** Multiple sequential requests. */
static bool test_sequential(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    asset_download_result_t r1, r2;
    ASSERT(asset_download_request(&t.dl, "meshes/cube.glb", &r1));
    ASSERT(r1.status == ASSET_DL_OK && r1.size == 11);
    free(r1.data);

    ASSERT(asset_download_request(&t.dl, "textures/stone.png", &r2));
    ASSERT(r2.status == ASSET_DL_OK && r2.size == 14);
    ASSERT(memcmp(r2.data, "PNGSTONEDATAXX", 14) == 0);
    free(r2.data);

    teardown_(&t);
    return true;
}

/** Request on disconnected downloader fails gracefully. */
static bool test_disconnected(void) {
    asset_download_t dl;
    asset_download_init(&dl);

    asset_download_result_t res;
    ASSERT(!asset_download_request(&dl, "foo", &res));
    ASSERT(res.status == ASSET_DL_NET_ERROR);
    return true;
}

/** Null safety. */
static bool test_null_safety(void) {
    asset_download_init(NULL);
    asset_download_disconnect(NULL);
    ASSERT(!asset_download_connect(NULL, NULL, 0));
    ASSERT(!asset_download_request(NULL, NULL, NULL));
    return true;
}

/** Connect, download, disconnect, reconnect, download again. */
static bool test_reconnect(void) {
    test_ctx_t t;
    ASSERT(setup_(&t));

    asset_download_result_t r1;
    ASSERT(asset_download_request(&t.dl, "meshes/cube.glb", &r1));
    ASSERT(r1.status == ASSET_DL_OK);
    free(r1.data);

    /* Disconnect and reconnect. */
    asset_download_disconnect(&t.dl);
    usleep(50000);
    ASSERT(asset_download_connect(&t.dl, "127.0.0.1", t.server.port));

    asset_download_result_t r2;
    ASSERT(asset_download_request(&t.dl, "textures/stone.png", &r2));
    ASSERT(r2.status == ASSET_DL_OK && r2.size == 14);
    free(r2.data);

    teardown_(&t);
    return true;
}

int main(void) {
    RUN(test_download_ok);
    RUN(test_download_not_found);
    RUN(test_sequential);
    RUN(test_disconnected);
    RUN(test_null_safety);
    RUN(test_reconnect);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
