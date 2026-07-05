---
id: rpg-nhjk
status: closed
deps: [rpg-oxnh]
links: []
created: 2026-07-04T20:41:07Z
type: task
priority: 0
assignee: KMD
parent: rpg-aqm2
tags: [procgen, critic, vlm, screenshot]
---
# procgen-8a: Screenshot capture at events

## Design

Implement critic_capture_screenshot() that renders the current frame to an offscreen buffer and saves it as raw RGB data (no PNG encoding for now — direct buffer to VLM). Capture at: player death position, last frame before death, each marker location (on first approach), and optionally every N seconds for a timelapse. Use existing renderer offscreen path (PBO readback or glReadPixels). Write RED test with mock renderer.

## Acceptance Criteria

- Screenshot captured at death position\n- Screenshot captured at each marker location\n- Frame data is raw RGB, correct resolution\n- Works headless without display\n- Multiple screenshots per playthrough, bounded\n- Screenshot slots freed on playthrough reset

