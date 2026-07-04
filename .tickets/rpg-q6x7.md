---
id: rpg-q6x7
status: open
deps: [rpg-oxnh]
links: []
created: 2026-07-04T20:41:07Z
type: task
priority: 0
assignee: KMD
parent: rpg-aqm2
tags: [procgen, critic, vlm, prompt]
---
# procgen-8b: Lightweight VLM prompt builder

## Design

Implement critic_build_visual_prompt() that constructs a prompt for the visual coherence VLM. Given a screenshot and the level context (grammar name, room type at capture point, expected geometry), ask the VLM: 'You are a video game quality assurance tester. Examine this screenshot from a procedurally generated dungeon. Identify any visual issues: Z-fighting (two surfaces flickering at same depth), missing textures, impossible geometry (floating objects, holes in walls), incorrect lighting, or aesthetic problems. Rate overall visual coherence from 0.0 (broken) to 1.0 (perfect).' Write RED test.

## Acceptance Criteria

- Prompt includes role description\n- Prompt identifies what to look for (Z-fighting, missing textures, etc.)\n- Prompt includes level context (grammar, room type)\n- Prompt asks for structured output: score + issue list\n- Prompt requests score 0.0-1.0\n- Prompt works with both Qwen2.5-VL and Gemma-3 formats

