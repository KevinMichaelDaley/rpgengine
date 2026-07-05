/**
 * @file architect.h
 * @brief Architect VLM: generates dungeon token strings from natural language.
 *
 * Uses the engine's LLM infrastructure (engine_settings) to call a VLM
 * with a grammar-specific system prompt. On parse failure, reprompts
 * with error context up to max_retries.
 */

#ifndef FERRUM_PROCGEN_ARCHITECT_H
#define FERRUM_PROCGEN_ARCHITECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "ferrum/procgen/procgen_types.h"

/** @brief Maximum length of a generated ASCII floor plan string. */
#define ARCHITECT_MAX_OUTPUT 65536

/**
 * @brief Build the system + user prompt for the architect VLM.
 *
 * @param grammar_name   (unused, kept for API compatibility).
 * @param user_request   Natural language level description.
 * @param grammar_prompt Custom system prompt (or NULL for default).
 * @param error_context  Previous error for reprompting (or NULL).
 * @param out            Output buffer.
 * @param out_cap        Capacity of output buffer.
 * @return Number of bytes written (including null), or negative on error.
 */
int procgen_architect_build_prompt(const char *grammar_name,
                                   const char *user_request,
                                   const char *grammar_prompt,
                                   const char *error_context,
                                   char *out, size_t out_cap);

/**
 * @brief Result of an architect run.
 */
typedef struct {
    int      success;               /**< Non-zero if a valid token string was produced. */
    char     token_string[ARCHITECT_MAX_OUTPUT];  /**< The generated token string. */
    char     error_message[1024];  /**< Last error (empty on success). */

    /* Statistics. */
    uint32_t attempt_count;        /**< Number of VLM calls made. */
    uint32_t total_input_tokens;   /**< Total prompt tokens across attempts. */
    uint32_t total_output_tokens;  /**< Total completion tokens across attempts. */
    float    total_cost_usd;       /**< Estimated cost across all attempts. */
} architect_result_t;

/**
 * @brief Run the architect VLM to generate a token string.
 *
 * Builds a system prompt from the grammar's VLM prompt fragment (or a
 * default blockout prompt), appends the user request, sends to the
 * configured LLM, attempts to tokenize the response, and reprompts
 * on failure up to max_retries times.
 *
 * @param grammar_name   Name of the grammar (e.g., "blockout").
 * @param user_request   Natural language level description.
 * @param grammar_prompt Grammar-specific system prompt (or NULL for default).
 * @param max_retries    Maximum reprompt attempts (0 = no retry).
 * @param out            Result (allocated by caller).
 * @return 0 on success, -1 on fatal error (check out->success).
 */
int architect_run(const char *grammar_name,
                  const char *user_request,
                  const char *grammar_prompt,
                  uint32_t max_retries,
                  architect_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_ARCHITECT_H */
