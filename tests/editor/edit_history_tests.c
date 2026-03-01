/**
 * @file edit_history_tests.c
 * @brief Tests for edit history — command log with context snapshots.
 *
 * Tests:
 *  1. init and destroy (no leak, no crash)
 *  2. init with NULL log path (no file, still works)
 *  3. record a single command
 *  4. record stores cmd, args, result, ok flag
 *  5. sequence numbers are monotonic
 *  6. timestamp is populated and looks like ISO-8601
 *  7. ring buffer wraps (oldest overwritten)
 *  8. flush writes entries to file
 *  9. flush advances cursor (no double-write)
 * 10. context snapshot includes cursor position
 * 11. context snapshot includes selection
 * 12. context snapshot includes @ aliases
 * 13. record with NULL ctx (no crash, empty context)
 * 14. record with NULL args/result (no crash)
 * 15. dispatch integration: exec records to history
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ferrum/editor/edit_history.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/edit_commands.h"

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
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** 1. Init and destroy with file. */
static bool test_init_destroy(void) {
    edit_history_t hist;
    const char *path = "/tmp/test_hist_1.jsonl";
    unlink(path);

    ASSERT(edit_history_init(&hist, 64, path));
    ASSERT(hist.entries != NULL);
    ASSERT(hist.capacity == 64);
    ASSERT(hist.seq == 0);
    ASSERT(hist.log_file != NULL);

    edit_history_destroy(&hist);
    ASSERT(hist.entries == NULL);
    ASSERT(hist.log_file == NULL);
    unlink(path);
    return true;
}

/** 2. Init with NULL log path (no file). */
static bool test_init_no_file(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 32, NULL));
    ASSERT(hist.log_file == NULL);
    ASSERT(hist.entries != NULL);
    edit_history_destroy(&hist);
    return true;
}

/** 3. Record a single command. */
static bool test_record_single(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 64, NULL));

    edit_history_record(&hist, NULL, "spawn",
                        "{\"type\":\"box\"}", "1", true);

    ASSERT(hist.head == 1);
    ASSERT(hist.seq == 1);
    ASSERT(strcmp(hist.entries[0].cmd, "spawn") == 0);

    edit_history_destroy(&hist);
    return true;
}

/** 4. Record stores cmd, args, result, ok flag. */
static bool test_record_fields(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 64, NULL));

    edit_history_record(&hist, NULL, "move",
                        "{\"delta\":[1,2,3]}", "{\"ok\":true}", true);

    const edit_history_entry_t *e = &hist.entries[0];
    ASSERT(strcmp(e->cmd, "move") == 0);
    ASSERT(strstr(e->args, "delta") != NULL);
    ASSERT(strstr(e->result, "ok") != NULL);
    ASSERT(e->ok == true);

    /* Record a failure too. */
    edit_history_record(&hist, NULL, "bad_cmd", NULL, NULL, false);
    ASSERT(hist.entries[1].ok == false);
    ASSERT(hist.entries[1].args[0] == '\0');
    ASSERT(hist.entries[1].result[0] == '\0');

    edit_history_destroy(&hist);
    return true;
}

/** 5. Sequence numbers are monotonic. */
static bool test_sequence_monotonic(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 64, NULL));

    for (int i = 0; i < 10; i++) {
        edit_history_record(&hist, NULL, "noop", NULL, NULL, true);
    }

    for (int i = 0; i < 10; i++) {
        ASSERT(hist.entries[i].seq == (uint64_t)(i + 1));
    }

    edit_history_destroy(&hist);
    return true;
}

/** 6. Timestamp is populated and looks like ISO-8601. */
static bool test_timestamp_format(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 64, NULL));

    edit_history_record(&hist, NULL, "test", NULL, NULL, true);

    const char *ts = hist.entries[0].timestamp;
    ASSERT(strlen(ts) >= 19); /* "2026-03-01T04:04:23" minimum */
    ASSERT(ts[4] == '-');     /* YYYY- */
    ASSERT(ts[7] == '-');     /* MM- */
    ASSERT(ts[10] == 'T');    /* T */

    edit_history_destroy(&hist);
    return true;
}

/** 7. Ring buffer wraps (oldest overwritten). */
static bool test_ring_wrap(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 4, NULL));

    /* Fill ring: seq 1,2,3,4 */
    for (int i = 0; i < 4; i++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        edit_history_record(&hist, NULL, cmd, NULL, NULL, true);
    }
    ASSERT(hist.head == 0); /* wrapped back to 0 */
    ASSERT(hist.seq == 4);

    /* Overwrite slot 0: seq becomes 5. */
    edit_history_record(&hist, NULL, "new", NULL, NULL, true);
    ASSERT(hist.entries[0].seq == 5);
    ASSERT(strcmp(hist.entries[0].cmd, "new") == 0);
    /* Slot 1 still has seq 2. */
    ASSERT(hist.entries[1].seq == 2);

    edit_history_destroy(&hist);
    return true;
}

/** 8. Flush writes entries to file. */
static bool test_flush_to_file(void) {
    edit_history_t hist;
    const char *path = "/tmp/test_hist_8.jsonl";
    unlink(path);
    ASSERT(edit_history_init(&hist, 64, path));

    edit_history_record(&hist, NULL, "spawn",
                        "{\"type\":\"box\"}", "1", true);
    edit_history_record(&hist, NULL, "move",
                        "{\"delta\":[1,0,0]}", "true", true);

    uint32_t flushed = edit_history_flush(&hist);
    ASSERT(flushed == 2);

    edit_history_destroy(&hist);

    /* Read the file back. */
    FILE *f = fopen(path, "r");
    ASSERT(f != NULL);
    char line[8192];
    int lines = 0;
    while (fgets(line, sizeof(line), f)) {
        lines++;
        ASSERT(strstr(line, "\"seq\"") != NULL);
        ASSERT(strstr(line, "\"cmd\"") != NULL);
        ASSERT(strstr(line, "\"ts\"") != NULL);
    }
    fclose(f);
    ASSERT(lines == 2);

    unlink(path);
    return true;
}

/** 9. Flush advances cursor (no double-write). */
static bool test_flush_no_double_write(void) {
    edit_history_t hist;
    const char *path = "/tmp/test_hist_9.jsonl";
    unlink(path);
    ASSERT(edit_history_init(&hist, 64, path));

    edit_history_record(&hist, NULL, "a", NULL, NULL, true);
    ASSERT(edit_history_flush(&hist) == 1);

    edit_history_record(&hist, NULL, "b", NULL, NULL, true);
    ASSERT(edit_history_flush(&hist) == 1); /* only "b", not "a" again */

    /* Flush with nothing pending. */
    ASSERT(edit_history_flush(&hist) == 0);

    edit_history_destroy(&hist);
    unlink(path);
    return true;
}

/** 10. Context snapshot includes cursor position. */
static bool test_ctx_cursor(void) {
    edit_cmd_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cursor_stack[0][0] = 1.0f;
    ctx.cursor_stack[0][1] = 2.5f;
    ctx.cursor_stack[0][2] = 3.0f;
    ctx.cursor_stack_count = 1;

    char buf[4096];
    size_t len = edit_history_snapshot_ctx(&ctx, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"cursor\"") != NULL);
    ASSERT(strstr(buf, "1") != NULL);
    ASSERT(strstr(buf, "2.5") != NULL);
    ASSERT(strstr(buf, "3") != NULL);
    return true;
}

/** 11. Context snapshot includes selection. */
static bool test_ctx_selection(void) {
    edit_cmd_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    edit_selection_t sel;
    edit_selection_init(&sel);
    edit_selection_add(&sel, 10);
    edit_selection_add(&sel, 20);
    ctx.selection = &sel;

    char buf[4096];
    size_t len = edit_history_snapshot_ctx(&ctx, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"sel\"") != NULL);
    ASSERT(strstr(buf, "10") != NULL);
    ASSERT(strstr(buf, "20") != NULL);

    edit_selection_destroy(&sel);
    return true;
}

/** 12. Context snapshot includes @ aliases. */
static bool test_ctx_aliases(void) {
    edit_cmd_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    edit_entity_store_t store;
    edit_entity_store_init(&store, 256);
    ctx.entities = &store;

    /* Create an alias entity named @origin. */
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    snprintf(e->name, sizeof(e->name), "@origin");
    e->pos[0] = 5.0f; e->pos[1] = 6.0f; e->pos[2] = 7.0f;

    char buf[4096];
    size_t len = edit_history_snapshot_ctx(&ctx, buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(strstr(buf, "\"aliases\"") != NULL);
    ASSERT(strstr(buf, "@origin") != NULL);
    ASSERT(strstr(buf, "5") != NULL);

    edit_entity_store_destroy(&store);
    return true;
}

/** 13. Record with NULL ctx (no crash, empty context). */
static bool test_record_null_ctx(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 64, NULL));

    edit_history_record(&hist, NULL, "test", "{}", "42", true);
    ASSERT(hist.entries[0].ctx[0] == '{' || hist.entries[0].ctx[0] == '\0');

    edit_history_destroy(&hist);
    return true;
}

/** 14. Record with NULL args/result (no crash). */
static bool test_record_null_strings(void) {
    edit_history_t hist;
    ASSERT(edit_history_init(&hist, 64, NULL));

    edit_history_record(&hist, NULL, "test", NULL, NULL, false);
    ASSERT(hist.entries[0].args[0] == '\0');
    ASSERT(hist.entries[0].result[0] == '\0');

    edit_history_destroy(&hist);
    return true;
}

/** 15. Dispatch integration: exec records to history. */
static bool test_dispatch_integration(void) {
    edit_dispatch_t dispatch;
    edit_entity_store_t entities;
    edit_selection_t selection;
    edit_cmd_ctx_t ctx;
    edit_history_t hist;

    memset(&ctx, 0, sizeof(ctx));
    edit_dispatch_init(&dispatch, 8192, &ctx);
    edit_entity_store_init(&entities, 256);
    edit_selection_init(&selection);
    ASSERT(edit_history_init(&hist, 64, NULL));

    ctx.entities = &entities;
    ctx.selection = &selection;
    dispatch.history = &hist;

    edit_commands_register_all(&dispatch);

    /* Execute a spawn command. */
    char resp[4096];
    const char *json = "{\"id\":1,\"cmd\":\"spawn\",\"args\":"
                       "{\"type\":\"box\",\"pos\":[0,0,0]}}";
    edit_dispatch_exec(&dispatch, json, (uint32_t)strlen(json),
                       resp, sizeof(resp));

    /* History should have one entry. */
    ASSERT(hist.seq == 1);
    ASSERT(strcmp(hist.entries[0].cmd, "spawn") == 0);
    ASSERT(hist.entries[0].ok == true);
    ASSERT(hist.entries[0].seq == 1);

    edit_history_destroy(&hist);
    edit_dispatch_destroy(&dispatch);
    edit_entity_store_destroy(&entities);
    edit_selection_destroy(&selection);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_init_destroy);
    RUN(test_init_no_file);
    RUN(test_record_single);
    RUN(test_record_fields);
    RUN(test_sequence_monotonic);
    RUN(test_timestamp_format);
    RUN(test_ring_wrap);
    RUN(test_flush_to_file);
    RUN(test_flush_no_double_write);
    RUN(test_ctx_cursor);
    RUN(test_ctx_selection);
    RUN(test_ctx_aliases);
    RUN(test_record_null_ctx);
    RUN(test_record_null_strings);
    RUN(test_dispatch_integration);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
