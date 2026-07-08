---
id: rpg-ct3l
status: closed
deps: [rpg-9pjm, rpg-9i3f]
links: []
created: 2026-07-05T22:56:06Z
type: epic
priority: 1
assignee: KMD
tags: [srd, discrete, rules]
---
# SRD-E5: Discrete Rewrite Phase — K-Candidates, LocalOptimize, Max-Cover

The discrete half of the SRD loop as specified in Kodnongbua et al. Algorithm 2. Samples K valid rule/selection pairs uniformly from the applicable rule set. For each candidate, applies the rule to a copy of the layout and runs a small L-BFGS LocalOptimize pass to compute delta_L. Builds a compatibility graph (two candidates conflict if their locality_radius neighbourhoods overlap) and applies greedy max-cover of all positive delta_L candidates. Repair rules applied unconditionally afterwards. See ref/srd_redesign_plan.md §Correct SRD Loop.

## Design

srd_discrete_phase(layout, current_loss, config, rng): allocate K candidate slots. For each: copy layout, sample rule+selection, apply rule, run local_optimize_steps of LBFGS, compute delta_L. Build N_cands x N_cands compatibility matrix. Greedy max-cover: sort by delta_L desc, greedily add compatible candidates. Apply selected. Apply repair rules.

## Acceptance Criteria

K=64 candidates evaluated per call; delta_L correctly measured as current_loss - post_local_opt_loss; compatibility matrix correctly identifies conflicting candidates (overlapping locality boxes); greedy max-cover selects non-conflicting subset; repair rules applied after; net loss after discrete phase is <= net loss before on a non-trivial test layout (averaged over 10 trials)

