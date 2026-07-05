#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_architect.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_GT(a, b) ASSERT_TRUE((int)(a) > (int)(b))
#define PASS() g_pass++

static void test_prompt_builder_contains_ascii_format(void) {
    char prompt[4096];
    int rc = procgen_architect_build_prompt("blockout", "Build a dungeon", NULL, NULL,
                                            prompt, sizeof(prompt));
    ASSERT_INT_GT(rc, 0);  /* returns size */
    /* Must contain ASCII format keyword */
    ASSERT_TRUE(strstr(prompt, "=== FLOOR") != NULL);
    /* Must NOT contain old token format */
    ASSERT_TRUE(strstr(prompt, "ROOM_QUAD") == NULL);
    ASSERT_TRUE(strstr(prompt, "@grammar") == NULL);
    PASS();
}

static void test_prompt_contains_loss_format(void) {
    char prompt[4096];
    int rc = procgen_architect_build_prompt("blockout", "Build a dungeon", NULL, NULL,
                                            prompt, sizeof(prompt));
    ASSERT_INT_GT(rc, 0);
    ASSERT_TRUE(strstr(prompt, "LOSS:") != NULL);
    PASS();
}

int main(void) {
    printf("=== Architect Prompt Tests ===\n\n");

    RUN(test_prompt_builder_contains_ascii_format);
    RUN(test_prompt_contains_loss_format);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
