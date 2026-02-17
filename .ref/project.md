# Torusbreakdown RPG Project

## Overview
An RPG game engine built in C++ with networking and ECS (Entity Component System) architecture.

## Key Components
- **ECS System**: Entity-Component-System for game entity management (include/ferrum/ecs/)
- **Networking**: RUDP (Reliable UDP) networking with replication (include/ferrum/net/)
- **Job System**: Work scheduling and instrumentation (include/ferrum/job/)
- **Math**: Vector/matrix/quaternion math libraries (include/ferrum/math/)
- **Memory**: Arena and pool-based memory management (include/ferrum/memory/)
- **Demo**: Camera, geometry, input handling, server world (include/ferrum/demo/)

## Tech Stack
- C++ (primary language)
- Node.js/npm (build tooling)
- Makefile for build orchestration

## Review Focus
- Code quality and architecture consistency
- Memory safety and allocation patterns
- Networking reliability and replication correctness
- ECS system performance and design
- Test coverage and maintainability

## Project Structure
- `include/` - Header files organized by subsystem
- `docs/` - Documentation
- `extern/` - External dependencies
- `design/` - Design documents
- `Makefile` - Build configuration
