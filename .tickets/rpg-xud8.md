---
id: rpg-xud8
status: closed
deps: [rpg-ct3l, rpg-9pjm, rpg-9i3f]
links: []
created: 2026-07-05T22:56:06Z
type: task
priority: 1
assignee: KMD
parent: rpg-ct3l
tags: [srd, discrete]
---
# srd-discrete-01: candidate sampling and LocalOptimize

Implement candidate sampling: for each of K iterations, pick a uniform random applicable rule, sample a valid selection, copy the layout, apply the rule, run local_optimize_steps of L-BFGS on the copy, record delta_L. Store results in a fixed-size candidate array (no dynamic allocation — use stack array of K_MAX=512).

## Design

srd_candidate_t: {rule_idx, sel, layout_copy, params_copy, delta_L}. K_MAX=512 (matches max budget tier). Layout copy: memcpy of srd_sdf_layout_t (fixed size). params_copy: float[SRD_MAX_BOXES*4]. local_optimize uses same LBFGS logic as srd_continuous_phase but with local_optimize_steps iterations. If rule.apply fails, skip that candidate (delta_L = -inf).

## Acceptance Criteria

K candidates populated in one call; failed applies skipped without crash; delta_L is positive for a rule that provably improves a simple test layout; local_optimize does run (verifiable by checking params_copy differs from initial values); no heap allocation inside the sampling loop

