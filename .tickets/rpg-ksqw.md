---
id: rpg-ksqw
status: open
deps: [rpg-8302, rpg-k4jk]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 3
assignee: KMD
parent: rpg-hjck
tags: [build, infra]
---
# Expandable orchestrator + libheadless/liball build split resolution

Make server/client orchestrators expandable (register subsystems as ordered stages rather than hardcoded numbered blocks) and resolve the library split so the headless GI subset (T10) links into libheadless.a while the client GI/render links into liball.a.

## Design

Server main() numbered stages -> a small stage-registration table (drain/physics/encode/flush already exist as callbacks; extend to setup/teardown). Ensure the headless GI + streaming CPU code compiles into libheadless.a (no GL); keep GPU paths in liball.a via the existing OBJ_HEADLESS/OBJ_ALL split (Makefile ~447-450). Update Makefile targets/names for server + client.

## Acceptance Criteria

Adding a new subsystem is a single stage registration; libheadless.a has zero GL symbols yet includes headless GI + CPU streaming; both server and client build under the renamed targets.

