/**
 * @file procgen_architect_cli.c
 * @brief CLI tool: generate a dungeon from a natural language prompt.
 *
 * Usage: ./build/procgen_architect_cli [grammar] [prompt] [output.json]
 *        Or:  ./build/procgen_architect_cli --file prompt.txt
 *        Or:  echo "prompt" | ./build/procgen_architect_cli --stdin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_architect.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/grammar_blockout.h"
#include "ferrum/procgen/procgen_serialize.h"
#include "ferrum/engine_settings.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] [prompt]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --model MODEL        LLM model (default: from engine settings)\n");
    fprintf(stderr, "  --url URL            LLM base URL (default: from engine settings)\n");
    fprintf(stderr, "  --key KEY            API key\n");
    fprintf(stderr, "  --grammar NAME       Grammar name (default: blockout)\n");
    fprintf(stderr, "  --retries N          Max reprompt retries (default: 3)\n");
    fprintf(stderr, "  --output FILE.json   Write JSON output\n");
    fprintf(stderr, "  --stdin              Read prompt from stdin\n");
    fprintf(stderr, "  --file FILE.txt      Read prompt from file\n");
    fprintf(stderr, "  --dataset DIR        Generate dataset into directory\n");
    fprintf(stderr, "  --count N            Number of levels for dataset (default: 10)\n");
    fprintf(stderr, "  --seed N             Dataset prompt seed (default: 42)\n");
}

static void free_l(fr_dungeon_layout_t *l) {
    free(l->rooms); free(l->corridors); free(l->openings);
    free(l->ramps); free(l->markers);
    free(l->nav_nodes); free(l->nav_edges);
    memset(l, 0, sizeof(*l));
}

int main(int argc, char **argv) {
    const char *model = NULL;
    const char *url = NULL;
    const char *key = NULL;
    const char *grammar_name = "blockout";
    const char *output_path = NULL;
    const char *dataset_dir = NULL;
    const char *prompt_file = NULL;
    int max_retries = 3;
    int dataset_count = 10;
    int seed = 42;
    int use_stdin = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--model") == 0 && i+1 < argc) model = argv[++i];
        else if (strcmp(argv[i], "--url") == 0 && i+1 < argc) url = argv[++i];
        else if (strcmp(argv[i], "--key") == 0 && i+1 < argc) key = argv[++i];
        else if (strcmp(argv[i], "--grammar") == 0 && i+1 < argc) grammar_name = argv[++i];
        else if (strcmp(argv[i], "--retries") == 0 && i+1 < argc) max_retries = atoi(argv[++i]);
        else if (strcmp(argv[i], "--output") == 0 && i+1 < argc) output_path = argv[++i];
        else if (strcmp(argv[i], "--dataset") == 0 && i+1 < argc) dataset_dir = argv[++i];
        else if (strcmp(argv[i], "--count") == 0 && i+1 < argc) dataset_count = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i+1 < argc) seed = atoi(argv[++i]);
        else if (strcmp(argv[i], "--file") == 0 && i+1 < argc) prompt_file = argv[++i];
        else if (strcmp(argv[i], "--stdin") == 0) use_stdin = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]); return 0;
        } else break;
    }

    /* Get user prompt. */
    char user_prompt[4096] = {0};
    if (use_stdin) {
        fread(user_prompt, 1, sizeof(user_prompt) - 1, stdin);
    } else if (prompt_file) {
        FILE *f = fopen(prompt_file, "r");
        if (!f) { fprintf(stderr, "cannot open %s\n", prompt_file); return 1; }
        fread(user_prompt, 1, sizeof(user_prompt) - 1, f);
        fclose(f);
    } else {
        /* Concatenate remaining args as prompt. */
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') continue; /* skip flags */
            if (user_prompt[0]) strncat(user_prompt, " ", sizeof(user_prompt) - strlen(user_prompt) - 1);
            strncat(user_prompt, argv[i], sizeof(user_prompt) - strlen(user_prompt) - 1);
        }
        if (user_prompt[0] == '\0') {
            fprintf(stderr, "No prompt provided. Use --stdin, --file, or command-line prompt.\n");
            return 1;
        }
    }

    /* Configure engine settings. */
    fr_engine_settings_init();
    fr_engine_settings_t *s = fr_engine_settings_mut();
    if (!s) { fprintf(stderr, "settings init failed\n"); return 1; }

    if (url) strncpy(s->llm_base_url, url, sizeof(s->llm_base_url) - 1);
    if (model) strncpy(s->llm_model, model, sizeof(s->llm_model) - 1);
    if (key) strncpy(s->llm_api_key, key, sizeof(s->llm_api_key) - 1);

    /* If no URL configured, try OpenRouter with the key from ~/.ssh/OPENROUTER_API_KEY */
    if (s->llm_base_url[0] == '\0') {
        strncpy(s->llm_base_url, "https://openrouter.ai/api/v1",
                sizeof(s->llm_base_url) - 1);
    }
    if (s->llm_api_key[0] == '\0') {
        FILE *kf = fopen(getenv("HOME") ? 
            "/home/kmd/.ssh/OPENROUTER_API_KEY" : 
            "/dev/null", "r");
        if (kf) {
            char kbuf[256] = {0};
            fgets(kbuf, sizeof(kbuf), kf);
            fclose(kf);
            size_t kl = strlen(kbuf);
            while (kl > 0 && (kbuf[kl-1] == '\n' || kbuf[kl-1] == '\r'))
                kbuf[--kl] = '\0';
            if (kbuf[0]) memcpy(s->llm_api_key, kbuf, kl < sizeof(s->llm_api_key)-1 ? kl : sizeof(s->llm_api_key)-1); s->llm_api_key[kl < sizeof(s->llm_api_key)-1 ? kl : sizeof(s->llm_api_key)-1] = '\0';
        }
    }
    if (s->llm_model[0] == '\0') {
        strncpy(s->llm_model, "google/gemini-2.0-flash-001",
                sizeof(s->llm_model) - 1);
    }

    s->llm_timeout_ms = 60000;
    s->llm_max_tokens = 2048;
    s->llm_input_cost_per_1k = 0.00015f;
    s->llm_output_cost_per_1k = 0.0006f;
    s->llm_budget_usd = 1.0f;
    fr_engine_settings_freeze();

    printf("=== Procgen Architect ===\n");
    printf("Model:    %s\n", fr_engine_settings_get()->llm_model);
    printf("URL:      %s\n", fr_engine_settings_get()->llm_base_url);
    printf("Grammar:  %s\n", grammar_name);
    printf("Retries:  %d\n", max_retries);
    printf("Prompt:   %.200s%s\n", user_prompt,
           strlen(user_prompt) > 200 ? "..." : "");

    if (dataset_dir) {
        /* Generate N levels with different prompt variations. */
        system("mkdir -p /tmp/procgen_dataset");
        const char *prompts[] = {
            "a small entrance room with two corridors leading to a treasure room",
            "a crypt with three connected rooms and a boss arena at the end",
            "a narrow corridor maze with four side rooms and a central chamber",
            "a two-floor dungeon with a ramp between floors and three rooms per floor",
            "a linear sequence of five rooms connected by corridors with doors",
            "an open arena room with four smaller rooms branching off via corridors",
            "a U-shaped dungeon with rooms at each end and a long corridor connecting them",
            "a compact dungeon with one large room and two small side rooms",
            "a spiral layout with four rooms connected in sequence plus a hidden room",
            "a T-junction dungeon with three corridors meeting at a central junction",
        };
        int n = dataset_count < 10 ? dataset_count : 10;
        for (int i = 0; i < n; i++) {
            srand((unsigned)(seed + i));
            printf("\n--- Dataset %d/%d ---\n", i+1, n);
            architect_result_t result;
            int rc = architect_run("blockout", prompts[i], NULL,
                                    (uint32_t)max_retries, &result);
            printf("  success=%d attempts=%u cost=$%.6f\n",
                   result.success, result.attempt_count, result.total_cost_usd);
            if (result.success) {
                char path[512];
                snprintf(path, sizeof(path), "%s/level_%03d.txt", dataset_dir, i);
                FILE *f = fopen(path, "w");
                if (f) { fputs(result.token_string, f); fclose(f); }

                /* Also save as JSON. */
                procgen_token_t tokens[8192];
                char err[256];
                uint32_t count = 0;
                tok_error_t tr = procgen_tokenize(result.token_string, tokens, 8192,
                                                   &count, err, sizeof(err));
                if (tr == TOK_ERR_NONE && count > 0) {
                    fr_dungeon_layout_t layout;
                    if (grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)) == 0) {
                        snprintf(path, sizeof(path), "%s/level_%03d.json", dataset_dir, i);
                        procgen_serialize_to_json(&layout, path, err, sizeof(err));
                        free_l(&layout);
                    }
                }
            }
            if (rc != 0) fprintf(stderr, "  FAIL: %s\n", result.error_message);
        }
    } else {
        /* Single prompt. */
        printf("\n");
        architect_result_t result;
        int rc = architect_run("blockout", user_prompt, NULL,
                                (uint32_t)max_retries, &result);
        printf("\n=== Result ===\n");
        printf("Success:  %s\n", result.success ? "yes" : "no");
        printf("Attempts: %u\n", result.attempt_count);
        printf("Tokens:   %u in / %u out\n",
               result.total_input_tokens, result.total_output_tokens);
        printf("Cost:     $%.6f\n", result.total_cost_usd);

        if (result.success) {
            printf("\n=== Token String ===\n%s\n", result.token_string);

            /* Tokenize + rasterize + serialize. */
            procgen_token_t tokens[8192];
            char err[256];
            uint32_t count = 0;
            tok_error_t tr = procgen_tokenize(result.token_string, tokens, 8192,
                                               &count, err, sizeof(err));
            if (tr != TOK_ERR_NONE) {
                printf("FAILED TO TOKENIZE OUTPUT: %s\n", err);
            } else {
                fr_dungeon_layout_t layout;
                if (grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)) == 0) {
                    printf("Layout: %u rooms, %u corridors, %u openings, "
                           "%u ramps, %u markers, %u nav nodes, %u nav edges\n",
                           layout.room_count, layout.corridor_count, layout.opening_count,
                           layout.ramp_count, layout.marker_count,
                           layout.nav_node_count, layout.nav_edge_count);

                    if (output_path) {
                        procgen_serialize_to_json(&layout, output_path, err, sizeof(err));
                        printf("JSON written to: %s\n", output_path);
                    }
                    free_l(&layout);
                } else {
                    printf("RASTERIZE FAILED: %s\n", err);
                }
            }
        } else {
            printf("\nERROR: %s\n", result.error_message);
        }
        if (rc != 0) return 1;
    }

    return 0;
}
