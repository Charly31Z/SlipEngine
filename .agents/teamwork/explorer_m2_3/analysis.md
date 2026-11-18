# Analysis: Global Time Uniform Binding & Build Verification (Milestone 2)

**Author:** `explorer_m2_3` (Time Binding & Build Verification Explorer)  
**Date:** 2026-11-08  
**Scope:** Milestone 2 (GLSL Compiler & SlipMaterial Runtime Integration)  
**Target Subsystems:**
- `include/SlipEngine/core/SlipGlobals.h` & `src/core/SlipGlobals.cpp`
- `src/editor/panels/ViewportPanel.cpp`
- `src/render/SlipRenderQueue.cpp` & `include/SlipEngine/render/SlipRenderQueue.h`
- `src/SlipEngine.cpp` & `src/render/SlipMaterial.cpp`
- Animated Materials Verification Strategy & Build Pipeline

---

## 1. Executive Summary

Milestone 2 introduces dynamic GLSL generation and runtime material integration for the Shader Graph system. Animated materials (such as waves, water, pulses, and procedural noise panning) require real-time injection of cumulative simulation time (`u_Time`) and per-frame delta time (`u_DeltaTime`) into compiled shaders.

Our investigation revealed three critical architectural gaps and one severe blocker bug:
1. **Missing Cumulative Time Query**: `SlipGlobals` tracks the current frame timestamp in a private variable `currentFrame`, but provides **no public `GetTime()` getter**.
2. **Missing Time Uniform Binding in Render Paths**:
   - `ViewportPanel.cpp:1414` binds cameras, lights, shadows, and DDGI in `m_RenderQueue.Flush()`, but **completely omits `u_Time` and `u_DeltaTime`**.
   - `SlipRenderQueue.cpp:44` does not bind default time uniforms when shaders are activated.
   - `SlipEngine.cpp:316` (the standalone game runner) and `SlipMaterial::bind()` do not bind `u_Time` or `u_DeltaTime`.
3. **Critical Material Shader Bypass Bug in `SlipRenderQueue.cpp`**:
   - Line 60 of `SlipRenderQueue.cpp` unconditionally resolves shaders using `SlipShaderLibrary::Get(sType)`. If a material uses a dynamically compiled Shader Graph (`cmd.material->shader`), the render queue **completely ignores it** and falls back to default "Lit", breaking all custom shader graph rendering in the editor viewport!
4. **Verification Strategy**:
   - A multi-tier automated and visual verification plan was designed, including headless offscreen FBO dynamic pixel readback testing, unit tests, and the `assets/materials/sample_animated_procedural.shadergraph` validation pipeline.

---

## 2. Investigation: `SlipGlobals` Time Tracking

### 2.1 Code Inspection
- File: `include/SlipEngine/core/SlipGlobals.h` (lines 10-43)
- File: `src/core/SlipGlobals.cpp` (lines 13-38)

```cpp
// include/SlipEngine/core/SlipGlobals.h
class SLIPENGINE_API SlipGlobals
{
private:
	float currentFrame, deltaTime, lastFrame;
	float fixedDeltaTime;
    ...
public:
	static SlipGlobals& Get();
	float GetDeltaTime() { return (deltaTime > 0.0f && deltaTime < 0.2f) ? deltaTime : (1.0f / 60.0f); }
	void SetDeltaTime(float dt) { this->deltaTime = dt; }
	float GetFixedDeltaTime() const { return fixedDeltaTime; }
	void SetFixedDeltaTime(float fdt) { fixedDeltaTime = fdt; }
	void update(double time);
    ...
```

```cpp
// src/core/SlipGlobals.cpp
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

### 2.2 Caller Sites
`SlipGlobals::Get().update(time)` is invoked continuously in the main loops of both the editor and runner:
- `src/SlipEngine.cpp:414`:
  ```cpp
  SlipGlobals::Get().update(static_cast<double>(currentCounter) / static_cast<double>(perfFreq));
  SlipGlobals::Get().SetDeltaTime(frameTime);
  ```
- `src/editor_main.cpp:46`:
  ```cpp
  SlipGlobals::Get().update(static_cast<double>(perfCounter) / static_cast<double>(perfFreq));
  ```
Both compute `perfCounter / perfFreq`, which yields exact cumulative elapsed wall-clock seconds.

### 2.3 Required Changes
In `include/SlipEngine/core/SlipGlobals.h`:
1. Expose `float GetTime() const { return currentFrame; }`.
2. Expose `void SetTime(float t) { this->currentFrame = t; }` (essential for deterministic headless unit tests and replay).
3. Mark `float GetDeltaTime() const` as `const` for const-correctness.

---

## 3. Investigation: Uniform Binding in Render Pipelines

### 3.1 Editor Viewport (`ViewportPanel.cpp`)
At line 1414 of `src/editor/panels/ViewportPanel.cpp`:
```cpp
m_RenderQueue.Flush([&](SlipShader* shader) {
    shader->setMat4("u_Projection", projection);
    shader->setMat4("u_View", view);
    shader->setMat4("projection", projection);
    shader->setMat4("view", view);
    shader->setVec3("u_CameraPos", eye.x, eye.y, eye.z);
    ...
```
This callback runs whenever `m_RenderQueue` switches the active shader program.
Adding:
```cpp
shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
```
at the top of this lambda ensures that any shader drawn through the viewport receives the current global frame time and delta time.

### 3.2 Render Queue Engine Core (`SlipRenderQueue.cpp`)
At lines 44-71 of `src/render/SlipRenderQueue.cpp`:
```cpp
void SlipRenderQueue::Flush(const std::function<void(SlipShader*)>& onBindShaderGlobals)
{
    if (m_Commands.empty()) return;

    Sort();

    SlipShader* currentShader = nullptr;
    SlipMaterial* currentMaterial = nullptr;
    ...
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
            currentMaterial = nullptr;
        }
        ...
```

#### Blocker Bug Identified:
1. `cmd.material->shader` is ignored! If `cmd.material` holds a dynamically generated shader graph program pointer (`cmd.material->shader != nullptr`), `SlipRenderQueue` ignores it and fetches the stock library shader for `sType`.
2. Time uniforms are omitted if `onBindShaderGlobals` is null or doesn't set them.

#### Proposed Fix:
Prioritize `cmd.material->shader` when selecting the shader:
```cpp
SlipShader* shader = nullptr;
if (cmd.material && cmd.material->shader) {
    shader = cmd.material->shader;
} else {
    std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
    if (sType == "default") sType = "Lit";
    shader = SlipShaderLibrary::Get(sType);
    if (!shader) shader = SlipShaderLibrary::Get("Lit");
}
if (!shader) continue;

if (shader != currentShader) {
    shader->use();
    shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
    shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
    if (onBindShaderGlobals) {
        onBindShaderGlobals(shader);
    }
    currentShader = shader;
    currentMaterial = nullptr;
}
```

### 3.3 Viewport Sorting Key Calculation (`ViewportPanel.cpp:1375`)
In `ViewportPanel.cpp:1321` and `1375`:
```cpp
uint32_t shaderId = (subMat && std::string(subMat->shaderType) == "Unlit") ? 1 : 2;
```
If two submeshes use different shader graph materials, both get `shaderId = 2`, preventing state-sorting by shader program.
Fix:
```cpp
uint32_t shaderId = (subMat && subMat->shader) 
    ? static_cast<uint32_t>(subMat->shader->ID) 
    : ((subMat && std::string(subMat->shaderType) == "Unlit") ? 1 : 2);
```

### 3.4 Standalone Game Runner (`SlipEngine.cpp:316`)
In `SlipEngine::Render()`:
```cpp
SlipShader* litShader = SlipShaderLibrary::LoadOrGet("Lit");
if (litShader) {
    litShader->use();
    litShader->setMat4("u_Projection", proj);
    litShader->setMat4("u_View", view);
    litShader->setVec3("u_CameraPos", eye);
    litShader->setFloat("u_Time", SlipGlobals::Get().GetTime());
    litShader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
    ...
```

### 3.5 Direct Material Binding (`SlipMaterial.cpp:383`)
For meshes rendered via `SlipMesh::draw` calling `SlipMaterial::bind`:
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

    // Globals
    shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
    shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());

    bindMaterialProperties(shader);
}
```

---

## 4. Verification Strategy for Animated Materials

Animated materials combine mathematical operations with `u_Time` and `u_DeltaTime` to produce dynamic visual surfaces (water ripples, pulsing holograms, dissolving shields, UV panning).

We define a 4-tier verification strategy:

```
┌─────────────────────────────────────────────────────────────┐
│ Tier 1: Static GLSL & Uniform Reflection Verification      │
│ - Check u_Time & u_DeltaTime uniform locations in program   │
│ - Verify no GL errors on glUniform1f update                 │
├─────────────────────────────────────────────────────────────┤
│ Tier 2: Dynamic FBO Frame Differencing (Offscreen Render)   │
│ - Render quad with animated graph at t = 0.0s               │
│ - glReadPixels to Buffer_0                                  │
│ - Advance SlipGlobals::update(1.0s)                         │
│ - Render quad at t = 1.0s to Buffer_1                       │
│ - Assert Buffer_0 != Buffer_1 (L1-norm diff > threshold)    │
├─────────────────────────────────────────────────────────────┤
│ Tier 3: Edge Case & DeltaTime Stability Tests               │
│ - Time = 0.0s boundary                                      │
│ - DeltaTime clamping (0.0001f min, 0.2f max)               │
│ - Long-running precision (t = 86400s / 24 hours simulation) │
├─────────────────────────────────────────────────────────────┤
│ Tier 4: Real-World Workload Scenarios & Asset Round-Trip    │
│ - Sample animated procedural water shader graph             │
│ - Real-time viewport playback at 60 FPS                     │
└─────────────────────────────────────────────────────────────┘
```

### 4.1 Tier 1: Uniform Reflection Verification
Ensures that whenever a graph contains a `TimeNode` or `DeltaTimeNode`, the generated GLSL declares `uniform float u_Time;` and `uniform float u_DeltaTime;`, and `glGetUniformLocation` queries return valid non-negative locations.
- Covered by `Test_T1_F11_01_UniformLocationQueriesTime` and `Test_T1_F11_02_BindingTimeWithoutOpenGLErrors`.

### 4.2 Tier 2: Dynamic FBO Frame Differencing Test (Headless)
To verify that animation actually alters rendered pixel output:
```cpp
bool VerifyAnimatedMaterialDynamicOutput(SlipMaterial* mat) {
    GLuint fbo, tex;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    
    std::vector<uint8_t> frame0(64 * 64 * 4);
    std::vector<uint8_t> frame1(64 * 64 * 4);
    
    // Render at t = 0.0
    SlipGlobals::Get().SetTime(0.0f);
    mat->bind(glm::mat4(1.0f));
    DrawUnitQuad();
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, frame0.data());
    
    // Render at t = 1.0
    SlipGlobals::Get().SetTime(1.0f);
    mat->bind(glm::mat4(1.0f));
    DrawUnitQuad();
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, frame1.data());
    
    // Cleanup
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &tex);
    glDeleteFramebuffers(1, &fbo);
    
    // Compute pixel delta sum
    uint64_t diffSum = 0;
    for (size_t i = 0; i < frame0.size(); ++i) {
        diffSum += std::abs(static_cast<int>(frame0[i]) - static_cast<int>(frame1[i]));
    }
    
    // Expect significant pixel difference
    return diffSum > (64 * 64 * 4); // Average > 1 unit per channel
}
```

### 4.3 Tier 3: DeltaTime Stability & Precision Checks
- Delta time sanitization: verify that calling `update(time)` with zero or negative increments clamps `deltaTime` to `0.0001f` to prevent division-by-zero in rate calculations.
- Clamping of large spikes: verify that lag spikes (> 0.25s) are capped at `0.2s` preventing simulation explosions.
- Long session drift: check that `sin(u_Time)` remains bounded within `[-1.0, 1.0]` even after 86,400 seconds (24 hours).

### 4.4 Tier 4: Sample Asset Validation
Asset specification for `assets/materials/sample_animated_procedural.shadergraph`:
- Type: `Lit`
- Nodes:
  - `TimeNode`: outputs `Time`
  - `PropertyNode` (`WaveSpeed`, Float, default `2.0`)
  - `MultiplyNode`: `Time * WaveSpeed`
  - `SinNode`: produces oscillation `[-1.0, 1.0]`
  - `RemapNode`: remaps `[-1.0, 1.0]` to `[0.2, 0.9]` (Roughness pulse)
  - `UVNode`: texcoords
  - `PropertyNode` (`WaveScale`, Float, default `8.0`)
  - `ProceduralNoiseNode`: simplex noise modulated by UV and scale
  - `MasterLitNode`: connects base color, normal perturbation, and pulsing roughness.

---

## 5. Build Commands & Verification Targets

### 5.1 Build Targets
- Core engine shared library:
  ```powershell
  cmake --build build --config Debug --target SlipEngine
  ```
- Editor application:
  ```powershell
  cmake --build build --config Debug --target SlipEditorApp
  ```
- Standalone runner:
  ```powershell
  cmake --build build --config Debug --target SlipRunner
  ```
- Shader Graph automated test suite:
  ```powershell
  cmake --build build --config Debug --target SlipTestShaderGraph
  ```

### 5.2 Test Execution Command
```powershell
.\build\Debug\SlipTestShaderGraph.exe
```
Expected output: 170/170 tests pass (Tier 1-4).

---

## 6. Summary of Actionable Changes for Implementers

| Target File | Change Summary | Priority |
|-------------|----------------|----------|
| `include/SlipEngine/core/SlipGlobals.h` | Add `float GetTime() const`, `void SetTime(float)` | HIGH |
| `src/editor/panels/ViewportPanel.cpp` | Bind `u_Time`, `u_DeltaTime` in `m_RenderQueue.Flush` lambda | HIGH |
| `src/editor/panels/ViewportPanel.cpp` | Update `shaderId` in `RenderCommand::MakeSortKey` to use `subMat->shader->ID` | HIGH |
| `src/render/SlipRenderQueue.cpp` | Check `cmd.material->shader` before `SlipShaderLibrary::Get(sType)` | BLOCKER |
| `src/render/SlipRenderQueue.cpp` | Bind `u_Time`, `u_DeltaTime` on shader change in `Flush()` | HIGH |
| `src/render/SlipMaterial.cpp` | Bind `u_Time`, `u_DeltaTime` in `SlipMaterial::bind(model)` | MEDIUM |
| `src/SlipEngine.cpp` | Bind `u_Time`, `u_DeltaTime` to `litShader` in runner `Render()` | MEDIUM |
