---
id: rust-rpg-o4i.12
status: closed
deps: []
links: []
created: 2026-01-18T18:13:37.591589793-08:00
type: task
priority: 2
parent: rust-rpg-o4i
---
# P_004.10 Render pipeline: resource views (attachments + transient buffers)

### Why
P_004 spec calls for "Resource views" to avoid hard-coding effects: attachments (color/depth) and transient buffers passed through pipeline stages.

### What
- Define minimal types to represent:
  - color/depth attachments (identities + formats + intended usage)
  - transient buffers/resources used within a frame
  - per-pass declared inputs/outputs (resource views)
- Stage interface must allow "resource bindings" step distinct from draw submission.

### Tests (real GL, no mocking)
- Build a small pipeline where a pass declares a depth attachment and a color attachment.
- Ensure resources can be created/owned externally and passed into pipeline execution without hard-coded post effects.

### Acceptance
- Works without needing new post-effect-specific code.
- Minimal public surface; respects header/type limits.



