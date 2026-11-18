# E2E Test Infra: SlipEngine Shader Graph System

## Test Philosophy
- Opaque-box, requirement-driven derived from `ORIGINAL_REQUEST.md` and user-facing specifications.
- Autonomous verification executable: `SlipTestShaderGraph` (built via CMake, headless OpenGL context with SDL2 + GLAD matching `tests/test_terrain.cpp`).
- Pass/Fail semantics: All assertions must pass and executable must return exit code 0.

## Feature Inventory Coverage Map
| # | Feature | Requirement Source | Tier 1 (>=5) | Tier 2 (>=5) | Tier 3 (Pairwise) | Tier 4 (Scenario) |
|---|---------|-------------------|:------------:|:------------:|:-----------------:|:-----------------:|
| F01 | Graph Model & Ports | R1, R2 | 5 | 5 | ✓ | ✓ |
| F02 | Math & Vector Nodes | R1 | 5 | 5 | ✓ | ✓ |
| F03 | Procedural & Utility Nodes | R1 | 5 | 5 | ✓ | ✓ |
| F04 | Time & DeltaTime Nodes | R1 | 5 | 5 | ✓ | ✓ |
| F05 | Camera & Transform Nodes | R1 | 5 | 5 | ✓ | ✓ |
| F06 | Texture Sampling Nodes | R1 | 5 | 5 | ✓ | ✓ |
| F07 | Master Output Node (Lit/Unlit) | R1 | 5 | 5 | ✓ | ✓ |
| F08 | Asset File Serialization (.shadergraph) | R2 | 5 | 5 | ✓ | ✓ |
| F09 | Asset Pipeline / GUID Registration | R2 | 5 | 5 | ✓ | ✓ |
| F10 | GLSL Code Generation (Lit & Unlit) | R1, R2 | 5 | 5 | ✓ | ✓ |
| F11 | Time Uniform Binding (u_Time, u_DeltaTime) | R1 | 5 | 5 | ✓ | ✓ |
| F12 | SlipMaterial Integration & Shader Resolution | R2 | 5 | 5 | ✓ | ✓ |
| F13 | Public / Exposed Property Declaration | R3 | 5 | 5 | ✓ | ✓ |
| F14 | Material Parameter Uniform Overrides | R3 | 5 | 5 | ✓ | ✓ |
| F15 | Material File (.mat) Graph Persistence | R2, R3 | 5 | 5 | ✓ | ✓ |

## Test Architecture
- **Test Executable**: `build/Debug/SlipTestShaderGraph.exe`
- **Source Location**: `tests/test_shader_graph.cpp`
- **Invocation**: `.\build\Debug\SlipTestShaderGraph.exe`
- **Context Initialization**: Headless OpenGL 3.3 Core profile via `SDL_CreateWindow(..., SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN)` + `gladLoadGLLoader`.
- **Pass Semantics**: Returns exit code 0 with detailed test output for every tier.

## Real-World Application Scenarios (Tier 4)
| # | Scenario | Features Exercised | Expected Behavior |
|---|----------|--------------------|-------------------|
| 1 | Animated Procedural Water Shader | F02, F03, F04, F07, F10, F11, F12, F14 | Sin(time * speed) + procedural noise modulating albedo, normal perturbation, and roughness; compiles and binds in OpenGL |
| 2 | Pulsing Emissive Hologram (Unlit) | F02, F04, F07, F10, F11, F12, F13, F14 | Unlit material with deltaTime animation, exposed pulse rate and tint, direct emissive output |
| 3 | PBR Metallic Foil with Normal & Roughness | F02, F06, F07, F10, F12, F13, F14 | Lit master node with texture sampling, exposed metallic slider, roughness scale, and normal map |
| 4 | Distance-Fading Fresnel Lit Surface | F02, F05, F07, F10, F12, F13 | Camera position, dot(normal, viewDir) Fresnel effect modulating roughness and emissive rim |
| 5 | Full Round-Trip Material Workflow | F01, F08, F09, F12, F14, F15 | Create graph -> save .shadergraph -> load .shadergraph -> assign to SlipMaterial -> override params -> save .mat -> reload .mat -> verify GLSL and uniform binding |

## Coverage Thresholds
- Tier 1: ≥75 test cases (≥5 per feature across 15 core features)
- Tier 2: ≥75 test cases (boundary, empty graph, disconnected ports, cycle prevention, type mismatch, extreme values)
- Tier 3: ≥15 test cases (cross-feature interactions: math + time, texture + procedural, exposed param + unlit master, etc.)
- Tier 4: ≥5 end-to-end real-world workload scenarios
- **Total Minimum**: ≥170 test assertions / cases in `SlipTestShaderGraph`
