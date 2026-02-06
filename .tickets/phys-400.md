---
id: phys-400
status: open
deps: [phys-300]
links: []
created: 2026-02-06T05:20:00.000000000-08:00
type: epic
priority: 2
---
# Phase 4: Tiered Simulation

**Goal:** Full tier system with per-tier parameters.

## Overview

This phase implements the complete tiered simulation system:
- Distance-based tier classification with hysteresis
- Per-tier solver parameters (substeps, iterations)
- Per-tier stabilization strength
- Amortized ticking for T4 (background) bodies
- Halo closure for fast-moving T0 bodies

## Tier Definitions

- T0: Direct Manipulation (< 5m) - 3 substeps, 24 iterations
- T1: Near Interactive (< 15m) - 2 substeps, 20 iterations
- T2: Visible (< 50m) - 1 substep, 16 iterations
- T3: World-Shaping (< 200m) - 1 substep, 12 iterations
- T4: Background (> 200m) - amortized 10 Hz, 8 iterations
- T5: Sleeping - not simulated

## Subtasks

- phys-401: Distance-Based Tier Classification
- phys-402: Per-Tier Solver Parameters
- phys-403: Per-Tier Stabilization
- phys-404: Amortized T4 Ticking
- phys-405: Halo Closure Implementation
- phys-406: Phase 4 Integration Test + Benchmark

## Performance Targets

- 5000 bodies tiered: < 5 ms/tick
- T0 latency: < 1 ms (highest priority)
