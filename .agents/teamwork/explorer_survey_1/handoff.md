# Handoff Report: Material & Shader System Survey

**Agent:** explorer_survey_1 (Material System Explorer)  
**Parent Orchestrator:** af9ad2dc-4359-4d00-baf7-c2106d2d2f37  
**Artifact:** `c:\Projects\SlipEngine\.agents\teamwork\explorer_survey_1\survey_report.md`  
**Working Directory:** `c:\Projects\SlipEngine\.agents\teamwork\explorer_survey_1\`

---

## 1. Observation

1. **`SlipMaterial` Data Structure & Binding:**
   - In `include/SlipEngine/render/SlipMaterial.h:25-66`, materials maintain `glm::vec3 color`, `roughness`, `metallic`, `ao`, `normalScale`, `emissiveColor`, `emissiveIntensity`, `char shaderType[32] = "Lit"`, 7 texture slots (`diffPath`, `normalPath`, `roughnessPath`, `metallicPath`, `aoPath`, `emissivePath`, `opacityPath`), and flags (`alphaClip`, `alphaCutoff`, `invertRoughness`, `doubleSided`).
   - In `src/render/SlipMaterial.cpp:288-381`, `SlipMaterial::bindMaterialProperties(SlipShader* targetShader)` binds:
     - For `"Unlit"`: `u_Color`, `u_HasTexture`, `u_MainTexture` (unit 0).
     - For `"Lit"`: `u_Albedo`, `u_Roughness`, `u_Metallic`, `u_AO`, `u_EmissiveColor`, `u_EmissiveIntensity`, `u_AlphaClip`, `u_AlphaCutoff`, `u_InvertRoughness`, `u_NormalScale`, and textures to dedicated units: unit 0 (`u_AlbedoMap`), unit 1 (`u_NormalMap`), unit 2 (`u_RoughnessMap`), unit 4 (`u_MetallicMap`), unit 15 (`u_AOMap`), unit 16 (`u_EmissiveMap`), unit 17 (`u_OpacityMap`).
   - In `src/render/SlipMaterial.cpp:383-397`, `SlipMaterial::bind(glm::mat4 &model)` binds `u_Model` and `model` matrices, calls `init()`, and invokes `bindMaterialProperties(shader)`.

2. **Shader Management & Builtin GLSL Pipelines:**
   - In `include/SlipEngine/render/SlipShader.h:31`, `SlipShader::CreateFromSource(const char* vertexSrc, const char* fragmentSrc)` dynamically compiles vertex and fragment shaders from source strings at runtime.
   - In `src/render/SlipShaderLibrary.cpp:1235-1274`, `LoadBuiltinShaders()` creates and registers `"Unlit"` (`s_UnlitVert`, `s_UnlitFrag`), `"Lit"` and `"default"` (`s_LitVert`, `s_LitFrag`), `"Line"`, and `"ShadowDepth"`.
   - In `src/render/SlipShaderLibrary.cpp:2705-2723`, `LoadOrGet(const std::string& name)` checks `s_Shaders[name]`, then tries loading disk files `"assets/shaders/" + name + ".vert"` and `".frag"`, falling back to `Get("Lit")`.

3. **Lighting & Render Pipeline Uniforms:**
   - In `src/editor/panels/ViewportPanel.cpp:1413-1518`, `m_RenderQueue.Flush([&](SlipShader* shader) { ... })` passes:
     - Camera matrices: `u_Projection`, `u_View`, `projection`, `view`, `u_CameraPos`.
     - Directional lights: `u_NumDirLights`, `u_DirLights[i].direction`, `color`, `intensity`.
     - Cascaded Shadows: `u_ShadowMapArray` (unit 3), `u_LightSpaceMatrices[4]`, `u_CascadeEndClipSpace[4]`, `u_ShadowBias`, `u_PCFQuality`, `u_CascadeCount`.
     - Point lights: `u_NumPointLights` ($\le 32$), `u_PointLights[i].position`, `color`, `intensity`, `range`.
     - Spot lights: `u_NumSpotLights` ($\le 4$), `u_SpotLights[i].position`, `direction`, `color`, `intensity`, `range`, `spotAngle`.
     - DDGI probes: `SlipDDGI::Get().Bind(shader)` on texture units 5..14.
     - Reflection probe: `u_EnvCube` on unit 18.
     - Cloud shadows: `u_CloudShadowNoise` on unit 19 (PC).

4. **Time Variables:**
   - In `src/core/SlipGlobals.cpp:13-27`, `SlipGlobals::update(double time)` updates `currentFrame` and computes `deltaTime` clamped to $[0.0001, 0.2]$.
   - In `include/SlipEngine/core/SlipGlobals.h:24`, `SlipGlobals::Get().GetDeltaTime()` exists, but there is no getter for `currentFrame` (cumulative time).
   - `u_Time` and `u_DeltaTime` are currently not passed in `ViewportPanel.cpp:1414` or `SlipRenderQueue.cpp`.

5. **Material Inspector & Asset Handling:**
   - In `src/editor/panels/InspectorPanel.cpp:79-140`, `RenderSlipMaterialPropertiesEditor(SlipMaterial& mat, ...)` renders properties and a Shader dropdown (`"Lit"`, `"Unlit"`).
   - In `src/editor/panels/ContentBrowserPanel.cpp:470-495`, context menus allow editing assets (`.lua`, `.prefab`, `.controller`), opening corresponding editor panels via `SlipEditor::GetInstance()->...`.

6. **Build System & Existing Tests:**
   - Ran `build/Release/SlipTestRenderOpt.exe` and `build/Debug/SlipTestLua.exe`. Both pass their respective tests, confirming the CMake configuration, compilation flags, and libraries are functioning properly.

---

## 2. Logic Chain

1. **Material Independence & Generality (from Observation 1 & 2):**
   `SlipMaterial` references a shader by string key (`shaderType`) and gets resolved via `SlipShaderLibrary::LoadOrGet(sType)`. By extending `SlipMaterial` with a `shaderGraphPath` (or allowing `shaderType` to reference a graph) and dynamic parameter maps (`floatParameters`, `vectorParameters`, `textureParameters`), `SlipMaterial` can bind user-defined properties to any shader generated from a graph without modifying mesh draw call logic.
2. **Dynamic Compilation Viability (from Observation 2):**
   `SlipShader::CreateFromSource` is already present, robust, and logs GLSL syntax/link errors. A `ShaderGraphCompiler` can directly generate valid GLSL 330 core / 300 es source strings and instantiate `SlipShader` instances at runtime without needing external compiler tools or disk writes (though caching generated GLSL is also supported).
3. **Lighting Model Compatibility (from Observation 1 & 3):**
   SlipEngine's `s_LitFrag` expects surface properties: `albedo`, `normal`, `roughness`, `metallic`, `ao`, and `emissive`. For Lit shader graphs, the graph's master node outputs map 1:1 onto these PBR surface properties, which can then be fed into the existing Cook-Torrance lighting loop. For Unlit shader graphs, the output bypasses lighting completely and writes directly to `FragColor`, matching `s_UnlitFrag`.
4. **Collision-Free Texture Slots (from Observation 1 & 3):**
   Units 0, 1, 2, 4, 15, 16, 17 are used by standard material maps; unit 3 is CSM shadows; units 5-14 are DDGI; unit 18 is IBL cubemap; unit 19 is cloud noise. On desktop GL 3.3+, units 20 through 31 (12 units) are completely free and safe for custom texture samplers in Shader Graph.
5. **Time & Animation Injection (from Observation 4):**
   Because `SlipGlobals::Get().update()` is already invoked each frame in both `editor_main.cpp` and `SlipEngine.cpp`, exposing `float GetTime() const { return currentFrame; }` in `SlipGlobals.h` and adding `shader->setFloat("u_Time", ...)` and `shader->setFloat("u_DeltaTime", ...)` inside `ViewportPanel.cpp:1414` (and `SlipMaterial::bind`) cleanly satisfies R1's requirement for animated materials and deltaTime.

---

## 3. Caveats

- Android GL ES 3.0 has a guaranteed minimum of 16 texture units for fragment shaders. While desktop OpenGL provides 32 units (allowing units 20-31 for custom graph textures), on mobile devices running OpenGL ES 3.0, custom texture units should reuse unused material slots (e.g. if a graph doesn't use standard roughness or normal maps) or stay within available driver limits.
- Screen-Space Reflections (SSR) and DDGI probe updates in `ViewportPanel.cpp` write to location 1 (`FragGeometry`). The Lit code generator should preserve `layout(location = 1) out vec4 FragGeometry;` so SSR and DDGI continue working seamlessly on meshes with custom Lit shader graphs.
- If a custom shader graph does not use vertex displacement, generating the standard vertex shader (`s_LitVert` with skinning) ensures skeletal animations continue working out of the box.

---

## 4. Conclusion

SlipEngine's rendering pipeline and material architecture are exceptionally well-suited for the addition of a node-based Shader Graph:
1. `SlipMaterial` only requires addition of a shader graph asset reference, dynamic property overrides (`float`, `vec4`/color, `texture`), and binding logic in `bindMaterialProperties()`.
2. `SlipShaderLibrary` provides an ideal repository for caching generated shaders compiled via `SlipShader::CreateFromSource`.
3. Adding `u_Time` and `u_DeltaTime` is minimal, safe, and leverages existing frame timers in `SlipGlobals`.
4. The Master Node design cleanly separates Lit (PBR surface inputs integrated with GGX/Smith/CSM/DDGI) from Unlit (direct color emission).
5. Detailed implementation recommendations, struct definitions, node palettes, and code locations are fully documented in `survey_report.md`.

---

## 5. Verification Method

To verify these findings and existing system behavior:
1. **Inspect Survey Report:**
   Read `c:\Projects\SlipEngine\.agents\teamwork\explorer_survey_1\survey_report.md`.
2. **Inspect Material & Shader Source Files:**
   - `include/SlipEngine/render/SlipMaterial.h` & `src/render/SlipMaterial.cpp`
   - `include/SlipEngine/render/SlipShader.h` & `src/render/SlipShader.cpp`
   - `src/render/SlipShaderLibrary.cpp` (lines 1235-1274, 2705-2723)
   - `src/editor/panels/ViewportPanel.cpp` (lines 1413-1518)
3. **Execute Existing Test Suites:**
   ```powershell
   & "c:\Projects\SlipEngine\build\Debug\SlipTestLua.exe"
   & "c:\Projects\SlipEngine\build\Release\SlipTestRenderOpt.exe"
   ```
4. **Invalidation Conditions:**
   - Any modification changing vertex attribute locations (locations 0..6) would require updating generated vertex shaders.
   - Any reallocation of texture units 0..19 would shift the available custom texture range (units 20..31).
