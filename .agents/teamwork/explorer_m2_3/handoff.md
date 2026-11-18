# Handoff Report: Global Time Uniform Binding & Build Verification

**Agent:** `explorer_m2_3`  
**Working Directory:** `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\`  
**Date:** 2026-11-08  
**Recipient:** Orchestrator (`af9ad2dc-4359-4d00-baf7-c2106d2d2f37`) / Implementers

---

## 1. Observation

1. **`SlipGlobals.h` / `SlipGlobals.cpp`**:
   - In `include/SlipEngine/core/SlipGlobals.h` (lines 12-28):
     ```cpp
     private:
         float currentFrame, deltaTime, lastFrame;
         float fixedDeltaTime;
     ...
     public:
         static SlipGlobals& Get();
         float GetDeltaTime() { return (deltaTime > 0.0f && deltaTime < 0.2f) ? deltaTime : (1.0f / 60.0f); }
         void SetDeltaTime(float dt) { this->deltaTime = dt; }
     ```
   - In `src/core/SlipGlobals.cpp` (lines 13-27):
     ```cpp
     void SlipGlobals::update(double time)
     {
         currentFrame = static_cast<float>(time);
         if (lastFrame == 0.0f) {
             deltaTime = 1.0f / 60.0f;
         } else {
             deltaTime = currentFrame - lastFrame;
             if (deltaTime <= 0.0f) {
                 deltaTime = 0.0001f;
             } else if (deltaTime > 0.2f) {
                 deltaTime = 0.2f;
             }
         }
         lastFrame = currentFrame;
     }
     ```
   - In `src/editor_main.cpp` (line 46):
     ```cpp
     SlipGlobals::Get().update(static_cast<double>(perfCounter) / static_cast<double>(perfFreq));
     ```
   - In `src/SlipEngine.cpp` (lines 414-415):
     ```cpp
     SlipGlobals::Get().update(static_cast<double>(currentCounter) / static_cast<double>(perfFreq));
     SlipGlobals::Get().SetDeltaTime(frameTime);
     ```
   - Direct observation: Cumulative seconds are tracked in `currentFrame`, but there is **no `GetTime()` getter method**.

2. **`ViewportPanel.cpp` Global Shader Binding**:
   - In `src/editor/panels/ViewportPanel.cpp` (lines 1414-1420):
     ```cpp
     // Vaciado agrupado por material y shader (Material Sorting)
     m_RenderQueue.Flush([&](SlipShader* shader) {
         shader->setMat4("u_Projection", projection);
         shader->setMat4("u_View", view);
         shader->setMat4("projection", projection);
         shader->setMat4("view", view);
         shader->setVec3("u_CameraPos", eye.x, eye.y, eye.z);
     ```
   - Direct observation: `u_Time` and `u_DeltaTime` are **completely absent** from the `m_RenderQueue.Flush` callback in `ViewportPanel.cpp`.

3. **`SlipRenderQueue.cpp` Shader Selection and Time Uniforms**:
   - In `src/render/SlipRenderQueue.cpp` (lines 56-72):
     ```cpp
     for (const auto& cmd : m_Commands) {
         SlipShader* shader = nullptr;
         std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
         if (sType == "default") sType = "Lit";
         shader = SlipShaderLibrary::Get(sType);
         if (!shader) shader = SlipShaderLibrary::Get("Lit");
         if (!shader) continue;

         if (shader != currentShader) {
             shader->use();
             if (onBindShaderGlobals) {
                 onBindShaderGlobals(shader);
             }
             currentShader = shader;
             currentMaterial = nullptr; // Forzar re-bind de material al cambiar shader
         }
     ```
   - Direct observation:
     1. Line 60 unconditionally calls `SlipShaderLibrary::Get(sType)`. If `cmd.material->shader` contains a custom dynamically compiled ShaderGraph program, `SlipRenderQueue` ignores it and falls back to stock "Lit".
     2. `SlipRenderQueue::Flush()` does not set default `u_Time` or `u_DeltaTime` uniforms on `shader`.

4. **Viewport Sort Key Calculation**:
   - In `src/editor/panels/ViewportPanel.cpp` (lines 1375-1380):
     ```cpp
     float dist = glm::distance(eye, glm::vec3(item.worldTransform[3]));
     uint32_t shaderId = (subMat && std::string(subMat->shaderType) == "Unlit") ? 1 : 2;
     uint32_t matId = subMat ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(subMat) & 0xFFFF) : 0;
     uint32_t vaoId = (item.mesh && item.submeshIndex < item.mesh->meshes.size()) ?
                      (item.mesh->meshes[item.submeshIndex].VAO & 0xFF) : 0;

     cmd.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, shaderId, matId, vaoId, dist, true);
     ```
   - Direct observation: `shaderId` is hardcoded to 1 or 2, failing to differentiate unique shader graph shaders during sorting.

5. **Standalone Runner & Direct Material Rendering**:
   - In `src/SlipEngine.cpp` (lines 316-322):
     ```cpp
     SlipShader* litShader = SlipShaderLibrary::LoadOrGet("Lit");
     if (litShader) {
         litShader->use();
         litShader->setMat4("u_Projection", proj);
         litShader->setMat4("u_View", view);
         litShader->setVec3("u_CameraPos", eye);
     ```
   - In `src/render/SlipMaterial.cpp` (lines 383-397):
     ```cpp
     void SlipMaterial::bind(glm::mat4 &model)
     {
         if (!shader) {
             init();
         }
         if (!shader) return;

         shader->use();

         // Matrices
         shader->setMat4("u_Model", model);
         shader->setMat4("model", model);

         bindMaterialProperties(shader);
     }
     ```
   - Direct observation: Neither sets `u_Time` or `u_DeltaTime`.

6. **Build Verification**:
   - Executing `cmake --build build --config Debug --target SlipEngine` exited with code 0 (`SlipEngine.dll`).
   - Executing `cmake --build build --config Debug --target SlipTestShaderGraph` exited with code 0 (`SlipTestShaderGraph.exe`).
   - Running `.\build\Debug\SlipTestShaderGraph.exe` executed 170 tests across Tiers 1-4 with 170 passed, 0 failed.

---

## 2. Logic Chain

1. **Premise 1 (Time Query)**: Animated shader graphs rely on `TimeNode` and `DeltaTimeNode`. `ShaderNode.cpp:920-942` emits expressions referencing uniforms `u_Time` and `u_DeltaTime`.
2. **Premise 2 (State Tracking)**: Both `editor_main.cpp:46` and `SlipEngine.cpp:414` continuously call `SlipGlobals::Get().update(time)`, keeping `currentFrame` and `deltaTime` accurate to real wall-clock seconds.
3. **Inference 1**: Because `currentFrame` already maintains cumulative time in seconds, adding `float GetTime() const { return currentFrame; }` and `void SetTime(float t) { this->currentFrame = t; }` to `SlipGlobals.h` provides zero-overhead, thread-safe, queryable time across the engine.
4. **Premise 3 (Render Uniform Injection)**: In the editor, all mesh rendering passes through `ViewportPanel.cpp:1414` via `m_RenderQueue.Flush(onBindShaderGlobals)`.
5. **Inference 2**: Injecting `shader->setFloat("u_Time", SlipGlobals::Get().GetTime())` and `shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime())` inside the `onBindShaderGlobals` lambda in `ViewportPanel.cpp:1414`, and directly inside `SlipRenderQueue::Flush()` at line 66 upon shader switch, guarantees all shaders executed by the queue receive updated time uniforms on every frame.
6. **Premise 4 (Queue Shader Dispatch)**: `SlipRenderQueue.cpp:60` currently dispatches only shaders found in `SlipShaderLibrary`. However, Shader Graph materials create custom shaders stored on `SlipMaterial::shader`.
7. **Inference 3 (Blocker)**: Unless `SlipRenderQueue::Flush()` is updated to check `if (cmd.material && cmd.material->shader) shader = cmd.material->shader;`, any material using a compiled shader graph will fail to render its custom shader and will instead render with stock Lit.
8. **Premise 5 (Runner and Direct Draw)**: Meshes rendered outside the render queue (such as in `SlipEngine::Render()` or via `SlipMesh::draw()`) call `SlipMaterial::bind()`.
9. **Inference 4**: Setting `u_Time` and `u_DeltaTime` in `SlipEngine::Render()` and `SlipMaterial::bind()` guarantees standalone runner games will animate custom materials identically to the editor.

---

## 3. Caveats

1. **Floating Point Precision at High Elapsed Times**: `currentFrame` is a 32-bit `float`. After ~4.6 hours of continuous runtime (16,384 seconds), single-precision float increments below 0.001s lose precision. For games or editor sessions running for days, a periodic modulus or double-precision backing store could be considered; however, for standard engine shader uniforms (`u_Time`), 32-bit float matches standard GPU hardware uniform registers.
2. **Unused Uniform Optimization**: If a shader graph does not contain any `TimeNode` or `DeltaTimeNode`, the GLSL compiler strips `u_Time` and `u_DeltaTime`. Calling `shader->setFloat("u_Time", ...)` when the uniform is absent is completely safe in SlipEngine because `SlipShader::setFloat` checks `glGetUniformLocation != -1` before calling `glUniform1f`.
3. **No Direct Code Modifications**: As an explorer in read-only investigation mode, no source files were modified. All proposed changes are documented in `analysis.md` and packaged in `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\time_binding.patch`.

---

## 4. Conclusion

1. **Expose `GetTime()`**:
   Add `float GetTime() const { return currentFrame; }` and `void SetTime(float t) { this->currentFrame = t; }` to `include/SlipEngine/core/SlipGlobals.h`.
2. **Bind `u_Time` and `u_DeltaTime`**:
   - In `ViewportPanel.cpp:1414` inside `m_RenderQueue.Flush([&](SlipShader* shader) { ... })`.
   - In `SlipRenderQueue.cpp:65` when `shader != currentShader`.
   - In `SlipEngine.cpp:318` on `litShader`.
   - In `SlipMaterial.cpp:390` in `SlipMaterial::bind()`.
3. **Fix Blocker Bug in `SlipRenderQueue.cpp:60`**:
   Prioritize `cmd.material->shader` so custom compiled shader graph shaders are not replaced by stock `SlipShaderLibrary` shaders.
4. **Fix Sorting Key in `ViewportPanel.cpp:1375`**:
   Use `subMat->shader->ID` when available to prevent custom shader graph materials from sorting into standard Lit batches.
5. **Execute Verification Strategy**:
   Adopt the 4-tier verification strategy outlined in Section 4 of `analysis.md`, including headless FBO frame differencing to guarantee real-time visual animation.

---

## 5. Verification Method

To independently verify all findings and validate implementations:

1. **Build Commands**:
   ```powershell
   cmake --build build --config Debug --target SlipEngine
   cmake --build build --config Debug --target SlipEditorApp
   cmake --build build --config Debug --target SlipRunner
   cmake --build build --config Debug --target SlipTestShaderGraph
   ```

2. **Automated Unit & Scenario Tests**:
   ```powershell
   .\build\Debug\SlipTestShaderGraph.exe
   ```
   Verify 100% pass rate (170/170 tests), specifically validating:
   - `Test_T1_F11_01_UniformLocationQueriesTime`
   - `Test_T1_F11_02_BindingTimeWithoutOpenGLErrors`
   - `Test_T1_F11_03_SuccessiveTimeProgression`
   - `Test_T1_F11_04_SlipGlobalsUpdateTimeIntegration`
   - `Test_T1_F11_05_DeltaTimeSanitizationProtection`
   - `Test_T4_01_AnimatedProceduralWaterShader`

3. **Code & Patch Inspection**:
   Inspect `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\time_binding.patch` and verify each unified diff against the target files.

4. **Invalidation Conditions**:
   The conclusions in this report would be invalidated if:
   - `SlipGlobals` was deprecated in favor of a different subsystem for simulation time.
   - `SlipRenderQueue` was refactored to require uniforms to be passed solely through a Uniform Buffer Object (UBO) block instead of standard uniform variables.
