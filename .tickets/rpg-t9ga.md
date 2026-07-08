---
id: rpg-t9ga
status: closed
deps: [rpg-02fm]
links: []
created: 2026-07-05T22:54:51Z
type: epic
priority: 1
assignee: KMD
tags: [srd, critic, libtorch]
---
# SRD-E3: Libtorch Swappable Critic

The differentiable critic interface and both backend implementations. ISrdCritic is an abstract C++ base taking [N,4] layout_params tensor and [N] types tensor, returning a scalar loss with gradient. AnalyticalCritic implements differentiable geometric losses via libtorch ops (no .pt file). TorchScriptCritic loads a compiled model from a .pt path at runtime. C API hides C++ from calling code. The SRD loop holds an srd_critic_t* and never needs to know which backend is active. See ref/srd_redesign_plan.md §Libtorch Swappable Critic.

## Design

AnalyticalCritic loss terms: NonPenetration (sum of soft SDF overlaps via smooth-min), MinimumSize (max(min_size-hw,0)^2 + max(min_size-hd,0)^2 per type), TypeSeparation (penalise boss/treasure adjacent to entrance), AdjacencyCount (per-type target degree), SoftReachability (softmax Dijkstra surrogate), BoundsViolation. TorchScriptCritic: torch::jit::load(pt_path), forward with (params, types) inputs.

## Acceptance Criteria

AnalyticalCritic.score is differentiable: loss.backward() populates params.grad with non-zero values; NonPenetration loss increases when boxes overlap; TorchScriptCritic loads a valid .pt file and returns same-shape output; srd_critic_create_torchscript returns NULL on missing file; swapping critic pointer changes loss values without changing SRD loop code

