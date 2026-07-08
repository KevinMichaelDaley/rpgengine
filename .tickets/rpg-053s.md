---
id: rpg-053s
status: closed
deps: [rpg-3v9s, rpg-t9ga]
links: []
created: 2026-07-05T22:55:22Z
type: task
priority: 1
assignee: KMD
parent: rpg-9i3f
tags: [srd, optimiser, libtorch]
---
# srd-config-02: continuous optimisation phase (L-BFGS)

Implement the continuous optimisation phase: extract (cx,cz,hw,hd) from srd_sdf_layout_t into a requires_grad tensor, run L-BFGS for lbfgs_max_iter iterations using the critic as the loss function, write parameters back to layout. This is one phase of the outer SRD loop, factored into its own function for testability.

## Design

srd_continuous_phase(layout, types_tensor, config, critic): build params tensor [N,4] from layout->boxes. Create LBFGS optimizer. Run closure loop. After convergence, copy tensor data back to layout->boxes (cx,cz,hw,hd). Clamp hw,hd >= SRD_EPSILON to prevent negative extents during optimisation.

## Acceptance Criteria

Loss strictly decreases (or plateaus) over iterations on a non-trivial layout; hw,hd never go below SRD_EPSILON after write-back; layout->boxes matches tensor values after write-back; function is callable independently from the full SRD loop for unit testing

