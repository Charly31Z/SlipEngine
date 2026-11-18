# Worker M2 Dispatch: GLSL Compiler & SlipMaterial Runtime Integration

- Identity: `worker_m2` (`teamwork_worker`)
- Working directory: `c:\Projects\SlipEngine\.agents\teamwork\worker_m2\`
- Parent: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`
- Reference Files:
  - `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
  - `c:\Projects\SlipEngine\PROJECT.md`
  - `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\analysis.md` (GLSL compiler design & full templates)
  - `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_2\analysis.md` (SlipMaterial integration & diffs)
  - `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\analysis.md` and `time_binding.patch` (Time binding & render queue fix)

## MANDATORY INTEGRITY WARNING
> DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

## Files Owned Exclusively
- `include/SlipEngine/render/shadergraph/ShaderGraphCompiler.h`
- `src/render/shadergraph/ShaderGraphCompiler.cpp`
- `include/SlipEngine/render/SlipMaterial.h`
- `src/render/SlipMaterial.cpp`
- `include/SlipEngine/core/SlipGlobals.h`
- `src/core/SlipGlobals.cpp`
- `src/editor/panels/ViewportPanel.cpp`
- `src/render/SlipRenderQueue.cpp`
- `CMakeLists.txt`

## Implementation Tasks
1. **Implement `ShaderGraphCompiler`** (`ShaderGraphCompiler.h` and `ShaderGraphCompiler.cpp`):
   - Topological sorting of reachable nodes from MasterNode (Kahn's algorithm).
   - Uniform declarations for blackboard parameters, time, and camera matrices.
   - Preamble containing GLSL procedural functions (`slip_gradient_noise`, `slip_voronoi`, `slip_hash22`).
   - Vertex shader emission (standard vertex pipeline with up to 100-bone skeletal animation).
   - Fragment shader emission:
     - **Lit mode**: evaluates surface attributes (albedo, normal, roughness, metallic, emissive, AO, alpha) and evaluates SlipEngine's standard Cook-Torrance GGX/Smith PBR lighting model (CSM cascaded shadows, point lights, spot lights, DDGI, reflection probes, and MRT `FragColor`/`FragGeometry`).
     - **Unlit mode**: evaluates color, emissive, alpha, and outputs directly to `FragColor`.
2. **Update `SlipMaterial`** (`SlipMaterial.h` and `SlipMaterial.cpp`):
   - Add `shaderGraphPath`, parameter override maps (`float`, `vec4`, `texture`), and `m_CustomShader` (`std::shared_ptr<SlipShader>`).
   - In `init()`: if `shaderGraphPath` is assigned, load graph, compile with `ShaderGraphCompiler::Compile`, create shader with `SlipShader::CreateFromSource`.
   - In `bindMaterialProperties()`: bind exposed property uniforms. Assign custom texture parameters to units 20+ (up to 31) to prevent collisions with engine units 0..19.
   - In `loadMatFromFile()` and `Save()`: support saving and loading `shaderGraph` path and parameter values.
3. **Update `SlipGlobals`** (`SlipGlobals.h` and `SlipGlobals.cpp`):
   - Add `float GetTime() const { return static_cast<float>(currentFrame); }` and `void SetTime(float t) { currentFrame = t; }`.
4. **Update `ViewportPanel.cpp` & `SlipRenderQueue.cpp`**:
   - In `ViewportPanel.cpp:1414`: bind `u_Time` and `u_DeltaTime` in `m_RenderQueue.Flush([&](SlipShader* shader) { ... })`.
   - In `ViewportPanel.cpp:1375`: sort commands using `(subMat && subMat->shader) ? subMat->shader->ID : ...`.
   - In `SlipRenderQueue.cpp:58-65`: select `(cmd.material && cmd.material->shader) ? cmd.material->shader : SlipShaderLibrary::Get(sType)`.
5. **Verify Build and Tests**:
   - `cmake --build build --config Debug --target SlipEngine` (must be 0 errors, 0 warnings).
   - `cmake --build build --config Debug --target SlipTestShaderGraph`
   - `.\build\Debug\SlipTestShaderGraph.exe` (must pass 170/170 tests with exit code 0).
6. Document changes and write `c:\Projects\SlipEngine\.agents\teamwork\worker_m2\handoff.md`.


## 2026-11-08T14:52:10Z
Received dispatch from parent af9ad2dc-4359-4d00-baf7-c2106d2d2f37:
Execute Milestone 2 tasks:
- ShaderGraphCompiler.h/cpp
- SlipMaterial.h/cpp
- SlipGlobals.h/cpp
- ViewportPanel.cpp & SlipRenderQueue.cpp
- CMakeLists.txt
- Build & test verification

## 2026-11-08T15:11:27Z
Received message from parent af9ad2dc-4359-4d00-baf7-c2106d2d2f37:
- Progress check-in on Milestone 2 implementation.
- Requested update on progress.md and handoff report when ready.

