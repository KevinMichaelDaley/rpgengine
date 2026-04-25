/**
 * @file llm_cost_compute.c
 * @brief Cost computation from token counts.
 */

#include "ferrum/llm/llm_cost_tracker.h"

float llm_cost_compute(int32_t input_tokens, int32_t output_tokens,
                       float input_cost_per_1k, float output_cost_per_1k) {
    float in_cost  = (input_tokens  / 1000.0f) * input_cost_per_1k;
    float out_cost = (output_tokens / 1000.0f) * output_cost_per_1k;
    return in_cost + out_cost;
}
