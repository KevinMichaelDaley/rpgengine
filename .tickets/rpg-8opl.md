---
id: rpg-8opl
status: closed
deps: [rpg-2ijr]
links: []
created: 2026-07-05T22:54:51Z
type: task
priority: 1
assignee: KMD
parent: rpg-t9ga
tags: [srd, critic, libtorch]
---
# srd-critic-03: TorchScriptCritic implementation

Implement TorchScriptCritic using torch::jit::load. Must handle missing .pt file gracefully (return NULL from C API). The loaded model's forward signature must match (Tensor[N,4], Tensor[N]) -> Tensor[]. Include a test that saves a trivial scripted module and loads it back.

## Design

torch::jit::load(pt_path) in constructor; catch c10::Error and re-throw as std::runtime_error caught by C API wrapper which returns NULL. In score(): module_.forward({params, types}).toTensor(). The C API create_torchscript wraps in try/catch.

## Acceptance Criteria

Loads a valid test .pt file and returns scalar tensor; returns NULL when file is missing; returns NULL when file is corrupt (not a TorchScript module); gradient flows through the loaded model (verified by calling backward on the output)

