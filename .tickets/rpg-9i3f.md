---
id: rpg-9i3f
status: closed
deps: [rpg-t9ga]
links: []
created: 2026-07-05T22:55:22Z
type: epic
priority: 1
assignee: KMD
tags: [srd, optimiser, libtorch]
---
# SRD-E4: Budget Configuration and L-BFGS Continuous Optimiser

The srd_descent_config_t struct, budget-to-config mapping function, and the continuous optimisation phase of the SRD loop. L-BFGS is the optimiser (torch::optim::LBFGS) operating on a flat [N*4] parameter tensor extracted from srd_sdf_layout_t. The continuous phase runs lbfgs_max_iter iterations per outer SRD step. Parameters are written back to the layout after each outer step. See ref/srd_redesign_plan.md §Budget-Driven Configuration.

## Design

srd_descent_config_from_budget: budget<2s -> k=16,lbfgs=20,local=3; 2-10s -> k=64,lbfgs=100,local=10; 10-60s -> k=256,lbfgs=500,local=25; >60s -> k=512,lbfgs=convergence,local=50. LBFGS closure: zero_grad, loss=critic.score(params,types), loss.backward(), return loss. Params extracted as contiguous float tensor from layout boxes, written back after step.

## Acceptance Criteria

srd_descent_config_from_budget populates all fields with correct tier values; L-BFGS reduces AnalyticalCritic loss by at least 50% on a 4-room test layout within 100 iterations; parameter write-back produces consistent layout (cx/cz/hw/hd match tensor values); gradient does not accumulate across outer iterations (zero_grad called correctly)

