# Handoff Report: Milestone 2 Review & Adversarial Challenge

## Review Summary

**Verdict**: REQUEST_CHANGES

The Milestone 2 implementation establishes a robust, highly capable visual shader compilation system with complete topological sorting (Kahn's algorithm), cycle detection (DFS 3-color), full Cook-Torrance GGX/Smith PBR and Unlit GLSL code generation, collision-free texture slot assignment (units 20+), and safe `SlipShader` memory lifecycle management. All 170 automated tests in `SlipTestShaderGraph.exe`, 25 tests in `SlipTestTopologyChallenger.exe`, and 24 stress tests pass with exit code 0.

However, an adversarial review of render queue dispatch surfaced a **Major finding** in `src/render/SlipRenderQueue.cpp` where exposed graph parameters for Unlit shader graph materials are bypassed and never bound to OpenGL uniforms during render queue flushing. A simple one-line condition update resolves this completely.

---

## 1. Observation

### Build & Verification Commands
- **Target `SlipEngine`**:
  Command: `cmake --build build --config Debug --target SlipEngine`
  Result: Exit code 0, 0 compiler errors, 0 warnings.
- **Target `SlipTestShaderGraph`**:
  Command: `cmake --build build --config Debug --target SlipTestShaderGraph`
  Result: Exit code 0, 0 compiler errors, 0 warnings.
- **Automated Test Suite**:
  Command: `.\build\Debug\SlipTestShaderGraph.exe`
  Result: 170/170 tests passing (Tier 1: 75/75, Tier 2: 75/75, Tier 3: 15/15, Tier 4: 5/5), exit code 0.
- **Topology Challenger Suite**:
  Command: `.\build\Debug\SlipTestTopologyChallenger.exe`
  Result: 25/25 tests passing, exit code 0.
- **Stress Suite**:
  Command: `.\build\Debug\SlipTestShaderGraphStress.exe`
  Result: 24/24 tests passing (100+ to 500+ nodes, JSON fuzzing, rapid Lit/Unlit toggling), exit code 0.

### Codebase Observations
1. **Render Queue Dispatch in `src/render/SlipRenderQueue.cpp` (Lines 99-114)**:
   ```cpp
   if (cmd.material != currentMaterial) {
       if (cmd.material) {
           if (sType == "Unlit") {
               currentShader->setVec4("u_Color", cmd.material->color.x, cmd.material->color.y, cmd.material->color.z, 1.0f);
               bool hasTex = (cmd.material->diffuseTexture != nullptr && std::strlen(cmd.material->diffPath) > 0);
               currentShader->setBool("u_HasTexture", hasTex);
               currentShader->setBool("u_ToLinear", true);
               if (hasTex) {
                   currentShader->setInt("u_MainTexture", 0);
                   cmd.material->diffuseTexture->bind(0);
               }
           } else {
               cmd.material->bindMaterialProperties(currentShader);
           }
       } else { ... }
   ```
2. **Material Property Binding in `src/render/SlipMaterial.cpp` (Lines 509-527)**:
   ```cpp
   void SlipMaterial::bindMaterialProperties(SlipShader* targetShader)
   {
       if (!targetShader) return;

       if (!shaderGraphPath.empty()) {
           bindGraphParameters(targetShader);
       }

       std::string sType = std::string(shaderType);
       if (sType == "Unlit") {
           targetShader->setVec3("u_Color", color);
           bool hasTex = (diffuseTexture != nullptr && std::strlen(diffPath) > 0);
           targetShader->setBool("u_HasTexture", hasTex);
           if (hasTex) {
               targetShader->setInt("u_MainTexture", 0);
               diffuseTexture->bind(0);
           }
           return;
       }
   ```
3. **Texture Slot Allocation in `src/render/SlipMaterial.cpp` (Lines 496-506)**:
   ```cpp
   unsigned int customTexUnit = 20;
   for (const auto& pair : graphTextureParams) {
       if (pair.second.empty() || customTexUnit >= 32) continue;
       std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
       SlipTexture* tex = SlipTexture::load(pair.second.c_str());
       if (tex) {
           targetShader->setInt(uName, (int)customTexUnit);
           tex->bind(customTexUnit);
       }
       customTexUnit++;
   }
   ```
4. **Memory Management in `src/render/SlipMaterial.cpp` (Lines 418-424 & Lines 344-356)**:
   ```cpp
   m_CustomShader = std::shared_ptr<SlipShader>(newShader, [](SlipShader* s) {
       if (s) {
           s->destroy();
           delete s;
       }
   });
   shader = m_CustomShader.get();
   ```
   Destructor `~SlipMaterial()` resets `m_CustomShader.reset()` and sets `shader = nullptr;`.

---

## 2. Logic Chain

1. **Integrity Audit**:
   - Source code inspection of `ShaderGraphCompiler.cpp`, `SlipMaterial.cpp`, `SlipGlobals.cpp`, `SlipRenderQueue.cpp`, and `ViewportPanel.cpp` confirms that no hardcoded test outputs or dummy facades exist.
   - Topological sorting and 3-color DFS cycle detection are authentically executed on the graph DAG.
   - Dead code elimination is performed by pruning nodes not reachable from `MasterNode`.
   - GLSL emission generates authentic Cook-Torrance GGX/Smith PBR with cascaded shadow maps, DDGI probe sampling, and bone skinning matrices.
   - **Verdict**: INTEGRITY CHECK CLEAN.

2. **Render Queue Dispatch Defect (Finding 1)**:
   - *Premise*: When a material is assigned an Unlit shader graph, `LoadShaderGraph` sets `cmd.material->shaderType` to `"Unlit"`, and `sType` evaluates to `"Unlit"`.
   - *Observation*: In `SlipRenderQueue.cpp` line 101, `if (sType == "Unlit")` matches all unlit materials.
   - *Inference*: In that branch, only `u_Color`, `u_HasTexture`, `u_ToLinear`, and `u_MainTexture` are bound. `cmd.material->bindMaterialProperties(currentShader)` is in the `else` branch (line 112) and is NOT invoked.
   - *Consequence*: `bindGraphParameters(targetShader)` is never executed for Unlit shader graph materials during render queue flushing. Any exposed float, vector, color, or texture parameters declared on an Unlit shader graph will NOT be uploaded to GLSL uniforms in the viewport.
   - *Deduction*: In `SlipRenderQueue.cpp`, line 101 must distinguish between legacy engine unlit materials (`cmd.material->shaderGraphPath.empty()`) and ShaderGraph materials, ensuring `cmd.material->bindMaterialProperties(currentShader)` is called for custom shader graphs.

3. **Collision-Free Texture Slots**:
   - Engine internal textures occupy units 0 (albedo/main), 1 (normal), 2 (roughness), 3 (CSM shadow map array), 4 (metallic), 5..14 (DDGI irradiance/visibility), 15 (AO), 16 (emissive), 17 (opacity), 18..19 (cubemap/cloud shadows).
   - Custom shader graph texture sampler units start strictly at unit 20 (`customTexUnit = 20`) up to 31.
   - Line 498 enforces `customTexUnit >= 32` guard, preventing overflow past standard OpenGL 3.3 limits (`GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS`).
   - Splicing and uniform assignment (`targetShader->setInt(uName, customTexUnit)`) correctly pairs uniform samplers to texture units dynamically.
   - **Verdict**: PASS.

4. **Memory Safety & `SlipShader` Management**:
   - `SlipShader::CreateFromSource` compiles GLSL vertex and fragment sources. If compilation fails, intermediate shader objects are cleaned up via `glDeleteShader` and returns `nullptr`.
   - `SlipMaterial` stores custom shaders in `std::shared_ptr<SlipShader> m_CustomShader` with a custom deleter lambda that invokes `s->destroy()` (`glDeleteProgram`) followed by `delete s`.
   - Default engine shaders from `SlipShaderLibrary` are referenced by raw pointer `shader` and are never deleted by `SlipMaterial`.
   - Destructor `~SlipMaterial` resets `m_CustomShader` and zeroes `shader` without double-freeing or leaking GPU programs.
   - Copying `SlipMaterial` safely copies `m_CustomShader` (refcounted).
   - **Verdict**: PASS.

5. **Global Time & DeltaTime Pipeline Propagation**:
   - `SlipGlobals.h` and `.cpp` expose `GetTime() const` and `SetTime(float)`, and clamp `GetDeltaTime() const` safely to `[0.0001f, 0.2f]` with `1/60.0f` fallback.
   - `SlipRenderQueue::Flush` binds `u_Time` and `u_DeltaTime` on shader changes.
   - `ViewportPanel.cpp` binds `u_Time` and `u_DeltaTime` in the render queue flush callback.
   - Both vertex and fragment shaders generated by `ShaderGraphCompiler` declare `uniform float u_Time;` and `uniform float u_DeltaTime;`.
   - **Verdict**: PASS.

---

## 3. Findings

### [Major] Finding 1: Render Queue Dispatch Bypasses Exposed Parameters for Unlit Shader Graphs
- **What**: When rendering through `SlipRenderQueue::Flush()`, materials with `shaderType == "Unlit"` that use a `.shadergraph` asset never have their exposed properties (floats, vectors, textures) bound to GLSL uniforms.
- **Where**: `c:\Projects\SlipEngine\src\render\SlipRenderQueue.cpp`, lines 101-112.
- **Why**: Line 101 branches on `if (sType == "Unlit")`, which executes hardcoded bindings for legacy unlit materials (`u_Color`, `u_MainTexture`) and skips the `else` branch where `cmd.material->bindMaterialProperties(currentShader)` is called. In contrast, `SlipMaterial::bindMaterialProperties` calls `bindGraphParameters(targetShader)` before handling unlit color.
- **Suggested Fix**: Update `SlipRenderQueue.cpp` line 101 to check if the material is using a shader graph:
  ```cpp
  if (sType == "Unlit" && cmd.material->shaderGraphPath.empty()) {
      currentShader->setVec4("u_Color", cmd.material->color.x, cmd.material->color.y, cmd.material->color.z, 1.0f);
      bool hasTex = (cmd.material->diffuseTexture != nullptr && std::strlen(cmd.material->diffPath) > 0);
      currentShader->setBool("u_HasTexture", hasTex);
      currentShader->setBool("u_ToLinear", true);
      if (hasTex) {
          currentShader->setInt("u_MainTexture", 0);
          cmd.material->diffuseTexture->bind(0);
      }
  } else {
      cmd.material->bindMaterialProperties(currentShader);
  }
  ```

### [Minor] Finding 2: Unordered Iteration for Texture Unit Assignment in `bindGraphParameters`
- **What**: In `SlipMaterial::bindGraphParameters`, texture units are assigned by iterating over `graphTextureParams` (`std::unordered_map`), which does not guarantee deterministic order across platforms or graph property declaration order.
- **Where**: `c:\Projects\SlipEngine\src\render\SlipMaterial.cpp`, lines 496-506.
- **Why**: While dynamic `targetShader->setInt(uName, customTexUnit)` correctly links each uniform to its assigned slot, iterating over an unordered map means texture units 20, 21, etc. can shift order when properties are modified.
- **Suggested Fix**: Iterate over properties in deterministic order or sort by uniform name before assigning texture unit indices.

---

## 4. Caveats
- No code modifications were made during this review in compliance with the review-only constraint.
- The test suite `tests/test_shader_graph.cpp` utilizes `TestShaderMaterial` (defined inside `namespace SlipTest`) rather than calling `SlipMaterial` directly, though it tests the exact same compiler (`ShaderGraphCompiler`), serialization, and GLSL code generation pipelines.

---

## 5. Conclusion
Milestone 2 is in an advanced, high-quality state with 100% test pass rate across all suites. However, because the user request and milestone contract require full support for both Lit and Unlit shader graphs with exposed public parameters rendered in the engine viewport, **REQUEST_CHANGES** is issued to resolve Finding 1 in `SlipRenderQueue.cpp`. Once line 101 of `SlipRenderQueue.cpp` is updated, the implementation will be fully approved.

---

## 6. Verification Method
To independently verify the finding and fix:
1. Review `src/render/SlipRenderQueue.cpp` lines 101-112 and compare with `src/render/SlipMaterial.cpp` lines 509-527.
2. Build `SlipEngine`:
   ```powershell
   cmake --build build --config Debug --target SlipEngine
   ```
3. Run the test suite:
   ```powershell
   cmake --build build --config Debug --target SlipTestShaderGraph
   .\build\Debug\SlipTestShaderGraph.exe
   ```
4. Verify that `cmd.material->bindMaterialProperties(currentShader)` is reached for both Lit and Unlit shader graph materials.
