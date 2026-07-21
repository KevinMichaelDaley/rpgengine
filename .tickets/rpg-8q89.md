---
id: rpg-8q89
status: open
deps: []
links: []
created: 2026-07-21T01:37:54Z
type: task
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf]
---
# Add glInvalidateFramebuffer for transient shadow/prepass depth

Section 1.6. Zero glInvalidateFramebuffer hits in src/. Every shadow/prepass FBO's transient depth attachment is written back to memory at FBO switch on tiled/integrated GPUs (Intel, RDNA2 APU) even though it is never sampled. Fix: load glInvalidateFramebuffer optionally (ARB_invalidate_subdata; the gl_loader_t pattern is NULL-tolerant, cf. render_forward.c:205-207) and invalidate depth attachments after each shadow/prepass FBO's last draw. Low risk, measurable on Deck/Iris.

## Acceptance Criteria

Transient depth attachments are invalidated after their last use where ARB_invalidate_subdata is present; no-op when absent.

