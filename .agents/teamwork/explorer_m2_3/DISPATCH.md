# M2 Explorer 3: Global Time Uniform Binding & Build Verification

- Identity: `explorer_m2_3` (`teamwork_explorer`)
- Working directory: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\`
- Parent: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`
- Reference Files:
  - `c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md`
  - `c:\Projects\SlipEngine\PROJECT.md`
  - `c:\Projects\SlipEngine\include\SlipEngine\core\SlipGlobals.h`
  - `c:\Projects\SlipEngine\src\core\SlipGlobals.cpp`
  - `c:\Projects\SlipEngine\src\editor\panels\ViewportPanel.cpp` (lines 1413-1460 for `onBindShaderGlobals`)
  - `c:\Projects\SlipEngine\src\render\SlipRenderQueue.cpp`

## Mission
Investigate global time uniform binding and build verification for Milestone 2:
1. `SlipGlobals`:
   - Inspect `include/SlipEngine/core/SlipGlobals.h` and `src/core/SlipGlobals.cpp`.
   - Add `float GetTime() const { return static_cast<float>(currentFrame); }` so cumulative time in seconds is queryable.
2. Global Shader Uniforms:
   - In `ViewportPanel.cpp:1414` inside `m_RenderQueue.Flush([&](SlipShader* shader) { ... })`, bind `u_Time` and `u_DeltaTime`:
     ```cpp
     shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
     shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
     ```
   - Check if `SlipRenderQueue.cpp` or `SlipEngine.cpp` also needs this binding for runner standalone execution.
3. Build Verification & Test Strategy:
   - Identify build target: `cmake --build build --config Debug --target SlipEngine`.
   - Design verification checks confirming that animated shader graphs dynamically update in real time.
4. Provide findings and recommendations in `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\analysis.md` and complete `handoff.md`.


## 2026-11-08T14:42:35Z
You are explorer_m2_3 (Time Binding & Build Verification Explorer).
Your working directory is: c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\
Read:
- c:\Projects\SlipEngine\.agents\teamwork\ORIGINAL_REQUEST.md
- c:\Projects\SlipEngine\PROJECT.md
- c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_3\DISPATCH.md
- c:\Projects\SlipEngine\include\SlipEngine\core\SlipGlobals.h
- c:\Projects\SlipEngine\src\core\SlipGlobals.cpp
- c:\Projects\SlipEngine\src\editor\panels\ViewportPanel.cpp
- c:\Projects\SlipEngine\src\render\SlipRenderQueue.cpp

Investigate:
1. Exposing `GetTime()` in `SlipGlobals.h` and `src/core/SlipGlobals.cpp`.
2. Binding `u_Time` and `u_DeltaTime` in `ViewportPanel.cpp` and `SlipRenderQueue.cpp`.
3. Verification strategy for animated materials.
Write findings and recommendations to analysis.md and complete handoff.md in your working directory. Send a message to parent af9ad2dc-4359-4d00-baf7-c2106d2d2f37 when finished.
