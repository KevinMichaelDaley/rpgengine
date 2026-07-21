/**
 * @file gl_constants_tests.c
 * @brief Value-check for the GL internal-format / compressed-format enums the
 *        renderer defines locally (no GLAD/GLEW). The engine hard-codes these
 *        hex values in gl_constants.h; a single transcription typo silently
 *        corrupts a texture allocation at runtime, so pin every value the
 *        format-shrink tickets rely on against the canonical GL spec numbers
 *        (rpg-1066). Pure header include -- no GL context required.
 */
#include <stdio.h>

#include "ferrum/renderer/gl_constants.h"

#define CHECK(macro, expected)                                                \
    do { if ((long)(macro) != (long)(expected)) {                             \
        fprintf(stderr, "  %s = 0x%lX, expected 0x%lX\n",                     \
                #macro, (long)(macro), (long)(expected));                     \
        ++fails;                                                              \
    } } while (0)

int main(void)
{
    int fails = 0;

    /* Floating-point colour formats (fp16 shadow/lightmap/SDF, rpg-jj8j/3exi/iz57). */
    CHECK(GL_R16F,    0x822D);
    CHECK(GL_RG16F,   0x822F);
    CHECK(GL_RGBA16F, 0x881A);   /* pre-existing; re-pinned as a guard. */
    CHECK(GL_RGB16F,  0x881B);
    CHECK(GL_R32F,    0x822E);
    CHECK(GL_RG32F,   0x8230);

    /* Packed HDR (static GI volume, rpg-iz57). */
    CHECK(GL_RGB9_E5,       0x8C3D);
    CHECK(GL_R11F_G11F_B10F, 0x8C3A);

    /* Depth (cube-shadow depth attachment shrink, rpg-jj8j). */
    CHECK(GL_DEPTH_COMPONENT16, 0x81A5);
    CHECK(GL_DEPTH_COMPONENT24, 0x81A6);

    /* Integer (single RG32I cluster TBO, rpg-2p4m). */
    CHECK(GL_RG32I, 0x823B);
    CHECK(GL_R32I,  0x8235);

    /* BC / S3TC + RGTC compressed material formats (rpg-wcc4). */
    CHECK(GL_COMPRESSED_RGB_S3TC_DXT1_EXT,        0x83F0);  /* BC1 */
    CHECK(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,       0x83F1);  /* BC1a */
    CHECK(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,       0x83F3);  /* BC3 */
    CHECK(GL_COMPRESSED_SRGB_S3TC_DXT1_EXT,       0x8C4C);
    CHECK(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 0x8C4F);
    CHECK(GL_COMPRESSED_RED_RGTC1,                0x8DBB);  /* BC4 */
    CHECK(GL_COMPRESSED_RG_RGTC2,                 0x8DBD);  /* BC5 */

    printf("gl_constants_tests: %s (%d mismatch%s)\n",
           fails == 0 ? "ok" : "FAIL", fails, fails == 1 ? "" : "es");
    return fails == 0 ? 0 : 1;
}
