/**
 * @file sdf_field_serialize_tests.c
 * @brief Round-trip tests for sdf_field (de)serialization -- the on-disk/asset
 *        form of a chunk's near/medium/far SDF (rpg-fzht, folds in rpg-iudw).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/chunk/sdf_field.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++g_fail; } \
                              else printf("ok: %s\n", msg); } while (0)
static int feq(float a, float b) { float d = a - b; return d > -1e-5f && d < 1e-5f; }

int main(void)
{
    int dims[3] = { 4, 3, 2 };
    uint32_t n = (uint32_t)dims[0]*dims[1]*dims[2];   /* 24 */
    float src[24];
    for (uint32_t i = 0; i < n; ++i) src[i] = (float)i * 0.25f - 2.0f;
    sdf_field_t f = { { dims[0],dims[1],dims[2] }, { -1.5f, 0.5f, 3.0f }, 0.75f, src };

    size_t need = sdf_field_serial_size(&f);
    CHECK(need == 4 + 3*4 + 3*4 + 4 + (size_t)n*4, "serial size = magic+dims+min+voxel+data");

    unsigned char *buf = malloc(need);
    size_t wrote = sdf_field_serialize(&f, buf, need);
    CHECK(wrote == need, "serialize writes the full size");
    CHECK(sdf_field_serialize(&f, buf, need - 1) == 0, "serialize into a short buffer -> 0");

    /* Deserialize into a fresh field + data buffer. */
    float back[24];
    sdf_field_t g; memset(&g, 0, sizeof g);
    bool ok = sdf_field_deserialize(buf, wrote, &g, back, 24);
    CHECK(ok, "deserialize succeeds");
    CHECK(g.dims[0]==4 && g.dims[1]==3 && g.dims[2]==2, "dims round-trip");
    CHECK(feq(g.min[0],-1.5f) && feq(g.min[1],0.5f) && feq(g.min[2],3.0f), "min round-trips");
    CHECK(feq(g.voxel, 0.75f), "voxel round-trips");
    int dbad = 0; for (uint32_t i = 0; i < n; ++i) if (!feq(g.data[i], src[i])) ++dbad;
    CHECK(dbad == 0, "distance data round-trips exactly");
    CHECK(g.data == back, "deserialize writes into the caller's data buffer");

    /* Failure modes. */
    sdf_field_t h;
    CHECK(!sdf_field_deserialize(buf, 3, &h, back, 24), "truncated header -> false");
    CHECK(!sdf_field_deserialize(buf, wrote, &h, back, 10), "too-small data dst -> false");
    unsigned char bad[8]; memcpy(bad, "XXXX", 4);
    CHECK(!sdf_field_deserialize(bad, 8, &h, back, 24), "bad magic -> false");

    free(buf);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
