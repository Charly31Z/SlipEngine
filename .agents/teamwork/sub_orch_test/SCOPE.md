# Scope: E2E Testing Track Orchestrator

## Objective
Design and implement the complete opaque-box E2E automated test suite (`SlipTestShaderGraph`), covering Tiers 1-4 per `TEST_INFRA.md`, and publish `TEST_READY.md`.

## Features Tested
- F01 - F15: Graph model, all node categories (math, procedural, time, camera, texture, Lit/Unlit master), serialization round-trip, asset pipeline, GLSL generation, time uniform binding, `SlipMaterial` assignment, parameter overrides, and `.mat` persistence.
- Real-World Workload Scenarios (Tier 4):
  1. Animated Procedural Water Shader
  2. Pulsing Emissive Hologram (Unlit)
  3. PBR Metallic Foil with Normal & Roughness (Lit)
  4. Distance-Fading Fresnel Lit Surface
  5. Full Round-Trip Material Workflow

## Files Owned
- `tests/test_shader_graph.cpp`
- `CMakeLists.txt` (register executable target `SlipTestShaderGraph`)
- `TEST_READY.md` (at project root, created when test suite is fully implemented and ready)

## Test Architecture
- Standalone executable: `build/Debug/SlipTestShaderGraph.exe`.
- Headless OpenGL 3.3 Core Profile context created via SDL2 (`SDL_WINDOW_HIDDEN`) + GLAD (matching `tests/test_terrain.cpp`).
- Complete test suites covering:
  - Tier 1: ≥75 feature coverage tests
  - Tier 2: ≥75 boundary & corner case tests
  - Tier 3: ≥15 pairwise cross-feature tests
  - Tier 4: ≥5 realistic workload scenarios
- When complete, publish `TEST_READY.md` at project root with runner commands, coverage counts, and feature matrix.

## Iteration Configuration
Sub-orchestrator executes Explorer -> Test Writer / Worker -> Reviewer -> Challenger -> Auditor -> Gate.
Auditor check is MANDATORY.
Publish `TEST_READY.md` only after build and full test execution passes.
