/**
 * @file lm_sdf_file_tests.c
 * @brief Unit tests for lm_sdf_file: per-chunk baked SDF (signed distance field)
 *        sidecar serialization.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/lightmap/lm_sdf_file.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_roundtrip(void)
{
    int32_t dims[3] = { 2, 3, 4 };
    float voxel = 0.05f;
    float origin[3] = { -1.5f, 0.25f, 7.0f };
    size_t n = (size_t)dims[0] * dims[1] * dims[2];
    float *dist = malloc(n * sizeof(float));
    ASSERT_TRUE(dist != NULL);
    for (size_t i = 0; i < n; ++i)
        dist[i] = (float)i * 0.5f - 3.0f;   /* mix of negative (inside) + positive */

    const char *path = "/tmp/lm_sdf_rt.sdf";
    ASSERT_TRUE(lm_sdf_save(path, dims, voxel, origin, dist));

    lm_sdf_data_t out;
    ASSERT_TRUE(lm_sdf_load(path, &out));
    ASSERT_TRUE(out.dims[0] == 2 && out.dims[1] == 3 && out.dims[2] == 4);
    ASSERT_TRUE(out.voxel == voxel);
    ASSERT_TRUE(out.origin[0] == origin[0] && out.origin[1] == origin[1] &&
                out.origin[2] == origin[2]);
    ASSERT_TRUE(out.dist != NULL);
    for (size_t i = 0; i < n; ++i)
        ASSERT_TRUE(out.dist[i] == dist[i]);

    lm_sdf_data_free(&out);
    ASSERT_TRUE(out.dist == NULL);   /* free zeroes it. */
    free(dist);
    return 0;
}

static int test_min_dims(void)
{
    int32_t dims[3] = { 1, 1, 1 };
    float voxel = 1.0f, origin[3] = { 0, 0, 0 }, d = -0.25f;
    const char *path = "/tmp/lm_sdf_min.sdf";
    ASSERT_TRUE(lm_sdf_save(path, dims, voxel, origin, &d));
    lm_sdf_data_t out;
    ASSERT_TRUE(lm_sdf_load(path, &out));
    ASSERT_TRUE(out.dims[0] == 1 && out.dims[1] == 1 && out.dims[2] == 1);
    ASSERT_TRUE(out.dist[0] == d);
    lm_sdf_data_free(&out);
    return 0;
}

static int test_failure_modes(void)
{
    int32_t dims[3] = { 1, 1, 1 };
    float voxel = 1.0f, origin[3] = { 0, 0, 0 }, d = 0.0f;

    ASSERT_TRUE(!lm_sdf_save(NULL, dims, voxel, origin, &d));
    ASSERT_TRUE(!lm_sdf_save("/tmp/x.sdf", dims, voxel, origin, NULL));
    ASSERT_TRUE(!lm_sdf_save("/tmp/x.sdf", NULL, voxel, origin, &d));

    lm_sdf_data_t out;
    ASSERT_TRUE(!lm_sdf_load("/tmp/does_not_exist_12345.sdf", &out));

    /* Bad magic. */
    FILE *f = fopen("/tmp/lm_sdf_bad.sdf", "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("NOPE", 1, 4, f);
    fclose(f);
    ASSERT_TRUE(!lm_sdf_load("/tmp/lm_sdf_bad.sdf", &out));
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_roundtrip();
    rc |= test_min_dims();
    rc |= test_failure_modes();
    if (rc == 0)
        printf("  OK: all lm_sdf_file tests passed\n");
    return rc;
}
