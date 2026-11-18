# Dispatch: E2E Testing Track Orchestrator

## Mission
You are the E2E Testing Track Orchestrator.
Working directory: `c:\Projects\SlipEngine\.agents\teamwork\sub_orch_test\`
Your parent is the Project Orchestrator: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`.

Read:
- `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
- `c:\Projects\SlipEngine\PROJECT.md`
- `c:\Projects\SlipEngine\TEST_INFRA.md`
- `c:\Projects\SlipEngine\.agents\teamwork\sub_orch_test\SCOPE.md`

## Required Workflow
Follow the Project Pattern E2E Testing Track:
1. Design the test harness in `tests/test_shader_graph.cpp` utilizing the headless OpenGL pattern from `tests/test_terrain.cpp`.
2. Register target `SlipTestShaderGraph` in `CMakeLists.txt` linking `SlipEngine`, `glad`, `SDL2::SDL2`, `glm`.
3. Implement test cases for Tier 1 (Feature Coverage >= 5 per feature), Tier 2 (Boundaries >= 5 per feature), Tier 3 (Cross-Feature Pairwise), and Tier 4 (Real-World Application Scenarios including animated procedural graphs).
4. Run iteration loop with Explorers, Workers/Test Writers, Reviewers, Challengers, and Forensic Auditor.
5. When all tests compile and pass with exit code 0, publish `TEST_READY.md` at project root `c:\Projects\SlipEngine\TEST_READY.md`.
6. Deliver `handoff.md` and notify parent `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`.
