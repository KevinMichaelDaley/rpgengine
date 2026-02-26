/**
 * @file cmd_physics_tests.c
 * @brief Tests for physics control commands: pause, resume, step, reset.
 *
 * Uses a mock physics controller to verify callback invocation and
 * state tracking without requiring an actual physics engine.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_physics_ctrl.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_undo.h"

/* ── Minimal test harness ─────────────────────────────────────────── */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); g_pass++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; return; \
    } \
} while (0)

/* ── Mock physics controller ──────────────────────────────────────── */

static bool g_mock_paused;
static int  g_pause_calls;
static int  g_resume_calls;
static int  g_step_calls;
static int  g_reset_calls;

static void mock_reset_counters(void) {
    g_mock_paused  = true;  /* Editor default: paused. */
    g_pause_calls  = 0;
    g_resume_calls = 0;
    g_step_calls   = 0;
    g_reset_calls  = 0;
}

static void mock_on_pause(void *ud)  { (void)ud; g_mock_paused = true;  g_pause_calls++;  }
static void mock_on_resume(void *ud) { (void)ud; g_mock_paused = false; g_resume_calls++; }
static void mock_on_step(void *ud)   { (void)ud; g_step_calls++;  }
static void mock_on_reset(void *ud)  { (void)ud; g_reset_calls++; }
static bool mock_is_paused(void *ud) { (void)ud; return g_mock_paused; }

/* ── Test fixture ─────────────────────────────────────────────────── */

typedef struct {
    edit_entity_store_t   entities;
    edit_selection_t      selection;
    edit_undo_stack_t     undo;
    edit_dispatch_t       dispatch;
    edit_cmd_ctx_t        cmd_ctx;
    edit_physics_ctrl_t   physics_ctrl;
} test_fixture_t;

static void fixture_init(test_fixture_t *f) {
    memset(f, 0, sizeof(*f));
    mock_reset_counters();

    edit_entity_store_init(&f->entities, 64);
    edit_selection_init(&f->selection);
    edit_undo_init(&f->undo, 32, 64 * 1024);

    /* Wire mock physics controller. */
    f->physics_ctrl.on_pause   = mock_on_pause;
    f->physics_ctrl.on_resume  = mock_on_resume;
    f->physics_ctrl.on_step    = mock_on_step;
    f->physics_ctrl.on_reset   = mock_on_reset;
    f->physics_ctrl.is_paused  = mock_is_paused;
    f->physics_ctrl.user_data  = NULL;

    f->cmd_ctx.entities  = &f->entities;
    f->cmd_ctx.selection = &f->selection;
    f->cmd_ctx.undo      = &f->undo;
    f->cmd_ctx.physics   = &f->physics_ctrl;

    edit_dispatch_init(&f->dispatch, 8192, &f->cmd_ctx);
    edit_commands_register_all(&f->dispatch);
}

static void fixture_destroy(test_fixture_t *f) {
    edit_dispatch_destroy(&f->dispatch);
    edit_undo_destroy(&f->undo);
    edit_selection_destroy(&f->selection);
    edit_entity_store_destroy(&f->entities);
}

/* Helper: execute a JSON command string. */
static bool exec_json(test_fixture_t *f, const char *json,
                      char *resp, uint32_t cap) {
    uint32_t n = edit_dispatch_exec(&f->dispatch, json,
                                    (uint32_t)strlen(json), resp, cap);
    return n > 0;
}

/* ── Physics pause tests ──────────────────────────────────────────── */

static void test_physics_pause(void) {
    test_fixture_t f;
    fixture_init(&f);
    g_mock_paused = false;  /* Start unpaused. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_pause\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_pause_calls == 1);
    ASSERT(g_mock_paused == true);

    fixture_destroy(&f);
}

static void test_physics_resume(void) {
    test_fixture_t f;
    fixture_init(&f);
    /* Default: paused = true. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_resume\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_resume_calls == 1);
    ASSERT(g_mock_paused == false);

    fixture_destroy(&f);
}

static void test_physics_step_when_paused(void) {
    test_fixture_t f;
    fixture_init(&f);
    /* Default: paused = true. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_step\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_step_calls == 1);

    fixture_destroy(&f);
}

static void test_physics_step_when_running(void) {
    test_fixture_t f;
    fixture_init(&f);
    g_mock_paused = false;  /* Not paused — step should fail. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_step\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "handler_failed") != NULL);
    ASSERT(g_step_calls == 0);

    fixture_destroy(&f);
}

static void test_physics_reset(void) {
    test_fixture_t f;
    fixture_init(&f);

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_reset\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_reset_calls == 1);

    fixture_destroy(&f);
}

static void test_physics_pause_idempotent(void) {
    test_fixture_t f;
    fixture_init(&f);
    /* Default: paused = true.  Pausing again should still succeed. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_pause\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_pause_calls == 1);
    ASSERT(g_mock_paused == true);

    fixture_destroy(&f);
}

static void test_physics_resume_idempotent(void) {
    test_fixture_t f;
    fixture_init(&f);
    g_mock_paused = false;  /* Already running. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_resume\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_resume_calls == 1);
    ASSERT(g_mock_paused == false);

    fixture_destroy(&f);
}

static void test_physics_no_ctrl(void) {
    test_fixture_t f;
    fixture_init(&f);
    f.cmd_ctx.physics = NULL;  /* No physics controller attached. */

    char resp[256];
    /* All commands should succeed gracefully (no-op). */
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_pause\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    ASSERT(exec_json(&f, "{\"id\":2,\"cmd\":\"physics_resume\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    ASSERT(exec_json(&f, "{\"id\":3,\"cmd\":\"physics_reset\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    /* Step without ctrl should succeed (no-op, can't check paused state). */
    ASSERT(exec_json(&f, "{\"id\":4,\"cmd\":\"physics_step\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);

    fixture_destroy(&f);
}

static void test_physics_reset_pauses(void) {
    test_fixture_t f;
    fixture_init(&f);
    g_mock_paused = false;  /* Running — reset should also pause. */

    char resp[256];
    ASSERT(exec_json(&f, "{\"id\":1,\"cmd\":\"physics_reset\"}", resp, sizeof(resp)));
    ASSERT(strstr(resp, "\"ok\":true") != NULL);
    ASSERT(g_reset_calls == 1);
    /* Reset should also pause physics. */
    ASSERT(g_pause_calls == 1);
    ASSERT(g_mock_paused == true);

    fixture_destroy(&f);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_physics_pause);
    RUN(test_physics_resume);
    RUN(test_physics_step_when_paused);
    RUN(test_physics_step_when_running);
    RUN(test_physics_reset);
    RUN(test_physics_pause_idempotent);
    RUN(test_physics_resume_idempotent);
    RUN(test_physics_no_ctrl);
    RUN(test_physics_reset_pauses);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
