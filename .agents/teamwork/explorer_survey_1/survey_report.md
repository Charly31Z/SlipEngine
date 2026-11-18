# SlipEngine Material & Shader System Survey Report
**Author:** explorer_survey_1 (Material System Explorer)  
**Date:** 2026-11-08  
**Scope:** Material system (`SlipMaterial`), shader management (`SlipShader`, `SlipShaderLibrary`), GLSL pipelines (Lit/Unlit), rendering queue (`SlipRenderQueue`, `SlipRender`, `ViewportPanel`), uniform/texture/time bindings, and Shader Graph integration blueprint.

---

## Executive Summary

SlipEngine uses an OpenGL 3.3+ (desktop) and OpenGL ES 3.0 (Android) modular rendering architecture. Materials are represented by `SlipMaterial`, which wraps surface properties, texture maps, and references a shader program from `SlipShaderLibrary`. The engine provides two core surface shading pipelines:
1. **Lit (PBR Cook-Torrance):** Microfacet BRDF using GGX distribution, Smith masking-shadowing, Fresnel-Schlick, supporting up to 4 Cascaded Directional Lights with PCF, 32 Point Lights, 4 Spot Lights, Dynamic Diffuse Global Illumination (DDGI) irradiance probe volumes, and Cubemap Image-Based Lighting (IBL).
2. **Unlit:** Direct color emission and texture modulation without light evaluation or shadow casting.

To integrate a production-ready Node-Based Material Editor (Shader Graph) supporting custom animated, mathematical, and procedural shaders, the engine requires:
- Dynamic shader compilation via `SlipShader::CreateFromSource` and registration into `SlipShaderLibrary`.
- Extended `SlipMaterial` support for referencing `.shadergraph` assets and storing dynamic exposed parameters (floats, vectors/colors, textures).
- Consistent injection of animation/time variables (`u_Time`, `u_DeltaTime`) and camera matrices across `SlipRenderQueue::Flush`, `SlipMaterial::bind`, and `ViewportPanel`.
- A code generation pipeline translating node trees into modular GLSL fragments for both Lit (PBR master outputs) and Unlit (direct color master output).

---

## 1. Material System Architecture: `SlipMaterial`

### 1.1 Class Structure & Data Members
- **Header:** `include/SlipEngine/render/SlipMaterial.h`
- **Source:** `src/render/SlipMaterial.cpp`

```cpp
class SLIPENGINE_API SlipMaterial {
public:
    std::string path;

    // Base Color & Legacy Properties
    glm::vec3 color = glm::vec3(1.0f);     // Base color / Albedo multiplier
    glm::vec3 ambient = glm::vec3(0.125f); // Legacy Blinn-Phong
    glm::vec3 specular = glm::vec3(0.16f); // Legacy Blinn-Phong
    float shininess = 3.0f;                // Legacy Blinn-Phong

    // PBR Lit Properties
    float roughness = 0.5f;
    float metallic = 0.0f;
    float ao = 1.0f;
    float normalScale = 1.0f;
    glm::vec3 emissiveColor = glm::vec3(0.0f);
    float emissiveIntensity = 0.0f;

    // Shader Identification
    char shaderType[32] = "Lit"; // "Lit", "Unlit", or custom shader name
    SlipShader* shader = nullptr; // Cached pointer from SlipShaderLibrary

    // Texture Slots (File paths & cached SlipTexture pointers)
    char diffPath[192] = "";       SlipTexture* diffuseTexture = nullptr;
    char normalPath[192] = "";     SlipTexture* normalTexture = nullptr;
    char roughnessPath[192] = "";  SlipTexture* roughnessTexture = nullptr;
    char metallicPath[192] = "";   SlipTexture* metallicTexture = nullptr;
    char aoPath[192] = "";         SlipTexture* aoTexture = nullptr;
    char emissivePath[192] = "";   SlipTexture* emissiveTexture = nullptr;
    char opacityPath[192] = "";    SlipTexture* opacityTexture = nullptr;

    // Render State Flags
    bool alphaClip = false;
    float alphaCutoff = 0.5f;
    bool invertRoughness = false;
    bool doubleSided = false;

    SlipMaterial(std::string path = "");
    ~SlipMaterial();

    bool Save(const std::string& savePath = "");
    void reloadTextures();
    void init();
    void bind(glm::mat4& model);
    void bindMaterialProperties(SlipShader* targetShader);
};
```

### 1.2 Serialization Format (`.mat`)
Materials are stored as human-readable key-value ASCII text files with the `.mat` extension (e.g. `assets/materials/MyMaterial.mat`).
Lines follow `<key>: <value>` format:
```text
shader: Lit
color: 1 1 1
roughness: 0.5
metallic: 0
ao: 1
emissive: 0 0 0
emissiveIntensity: 0
ambient: 0.125 0.125 0.125
specular: 0.16 0.16 0.16
shininess: 3
diffuseTexture: assets/textures/wood_albedo.png
normalTexture: assets/textures/wood_normal.png
normalScale: 1
roughnessTexture: assets/textures/wood_roughness.png
metallicTexture: 
aoTexture: 
emissiveTexture: 
opacityTexture: 
alphaClip: 0
alphaCutoff: 0.5
invertRoughness: 0
doubleSided: 0
```
- Deserialization: `SlipMaterial::loadMatFromFile(std::string matPath)` checks multiple search candidates: raw path, with `.mat`, inside `assets/materials/`, `assets/`, project root, and relative directories.
- Serialization: `SlipMaterial::Save(const std::string& savePath)` outputs all properties to disk.

### 1.3 Texture Resolution & Slot Loading
`LoadMaterialSlotTexture()` in `SlipMaterial.cpp:174-211` searches for relative texture files in:
1. `SlipFileSystem::GetProjectAssetsPath()`
2. `SlipFileSystem::GetProjectRootPath()`
3. Material's parent directory (`matDir`)
4. Adjacent `Textures/` and `textures/` folders.
`SlipTexture::load(path)` maintains an internal texture cache to avoid redundant GPU uploads.

---

## 2. Shader Compilation & Management: `SlipShader` & `SlipShaderLibrary`

### 2.1 `SlipShader` Program Wrapper
- **Header:** `include/SlipEngine/render/SlipShader.h`
- **Source:** `src/render/SlipShader.cpp`

Key capabilities:
1. **Dynamic compilation from strings:**
   ```cpp
   SlipShader* SlipShader::CreateFromSource(const char* vertexSrc, const char* fragmentSrc);
   ```
   Compiles vertex and fragment GLSL strings, links the program, queries compilation and linking logs via `glGetShaderInfoLog` and `glGetProgramInfoLog`, and returns `nullptr` with printed errors if compilation fails.
2. **File compilation:**
   `SlipShader(const char* vertexPath, const char* fragmentPath)` reads files via `SlipFileSystem::ReadAllText`.
3. **Uniform Setters:**
   `setBool`, `setInt`, `setFloat`, `setVec2`, `setVec3`, `setVec4`, `setMat4`, `setMat4Array`.

### 2.2 `SlipShaderLibrary` Central Cache
- **Header:** `include/SlipEngine/render/SlipShaderLibrary.h`
- **Source:** `src/render/SlipShaderLibrary.cpp`

Maintains `std::unordered_map<std::string, std::unique_ptr<SlipShader>> s_Shaders;`.
- Built-in shaders initialized in `LoadBuiltinShaders()`:
  - `"Lit"` and `"default"`: Standard PBR Cook-Torrance shader
  - `"Unlit"`: Direct color / textured unlit shader
  - `"Line"`: Wireframe / debug visualization
  - `"ShadowDepth"`: Cascaded Shadow Map depth render pass
  - `"Sprite2D"`, `"Font2D"`: 2D rendering
  - `"OutlinePost"`: Fullscreen selection outline post-processing
  - Cloud shaders: `"CloudRaymarch"`, `"CloudSlices"`, `"CloudBilateralUpscale"`
- Embedded GLSL source fallback: Built-in shaders are hardcoded as static C-string literals in `SlipShaderLibrary.cpp`, allowing the engine to run without loose external shader files on disk while supporting overrides if `assets/shaders/<name>.vert` and `.frag` are present.
- `LoadOrGet(const std::string& name)`:
  1. Checks if already loaded in `s_Shaders`.
  2. If file `assets/shaders/<name>.vert` and `.frag` exist, compiles and stores in map.
  3. Falls back to `Get("Lit")`.

---

## 3. Rendering Pipeline & Shading Pipelines

### 3.1 Vertex Layout & Attributes
All meshes in SlipEngine (`SlipMesh`, `SlipPrimitive`) share a unified vertex layout:

| Location | Attribute | Type | Description |
|---|---|---|---|
| 0 | `aPos` | `vec3` | Model-space vertex position |
| 1 | `aNormal` | `vec3` | Model-space vertex normal |
| 2 | `aTexCoords` | `vec2` | Primary UV coordinates |
| 3 | `aTangent` | `vec3` | Model-space tangent vector |
| 4 | `aBitangent` | `vec3` | Model-space bitangent vector |
| 5 | `aBoneIDs` | `ivec4` | Skeletal skinning bone indices |
| 6 | `aWeights` | `vec4` | Skeletal skinning bone weights |

### 3.2 Vertex Processing (`s_LitVert` & `s_UnlitVert`)
1. **Skinning:** If `u_HasBones && u_SkinningQuality > 0`, accumulates `skinMatrix` from `u_BoneTransforms[aBoneIDs[i]] * aWeights[i]` (quality 1, 2, or 4 bones).
2. **Transforms:**
   - `localPos = skinMatrix * vec4(aPos, 1.0);`
   - `worldPos = u_Model * localPos;`
   - `gl_Position = u_Projection * u_View * worldPos;`
3. **TBN Transform:**
   - `vNormal = normalize(mat3(u_Model) * (mat3(skinMatrix) * aNormal));`
   - `vTangent = mat3(u_Model) * (mat3(skinMatrix) * aTangent);`
   - `vBitangent = mat3(u_Model) * (mat3(skinMatrix) * aBitangent);`

### 3.3 Lit (PBR) Fragment Shading Pipeline (`s_LitFrag`)
1. **Surface Parameter Resolution:**
   - **Albedo:** `u_Albedo * texture(u_AlbedoMap, vTexCoords).rgb`
   - **Opacity & Alpha Clip:** Evaluates `u_OpacityMap` or Albedo alpha; discards if `alpha < u_AlphaCutoff`.
   - **Normal Mapping:** Computes surface normal from `u_NormalMap` scaled by `u_NormalScale`. If vertex tangent/bitangent are degenerate, computes screenspace TBN using `dFdx(vWorldPos)` and `dFdy(vWorldPos)`.
   - **Roughness & Metallic:** Supports discrete texture channels or packed ORM textures (Green = Roughness, Blue = Metallic). Optional `u_InvertRoughness` (Glossiness compatibility).
   - **Emissive:** `u_EmissiveColor * u_EmissiveIntensity` modulated by `u_EmissiveMap`.
   - **AO:** `u_AO * texture(u_AOMap, vTexCoords).r`.
2. **Microfacet BRDF Evaluation:**
   - Base reflectivity: $F_0 = \text{mix}(0.04, \text{albedo}, \text{metallic})$
   - Normal Distribution: GGX / Trowbridge-Reitz:
     $$D(N, H, \alpha) = \frac{\alpha^2}{\pi ((N \cdot H)^2 (\alpha^2 - 1) + 1)^2}$$
   - Geometric Masking: Smith with Schlick-GGX:
     $$G(N, V, L, k) = G_1(N, V) \cdot G_1(N, L), \quad k = \frac{(\text{roughness} + 1)^2}{8}$$
   - Fresnel: Schlick approximation:
     $$F(H, V, F_0) = F_0 + (1 - F_0)(1 - \max(0, H \cdot V))^5$$
   - Direct specular: $\frac{D \cdot G \cdot F}{4(N \cdot V)(N \cdot L) + 0.0001}$
   - Diffuse: $k_D \cdot \frac{\text{albedo}}{\pi} \cdot \text{wrapDiffuse}$, where $k_D = (1 - F)(1 - \text{metallic})$.
3. **Lighting Accumulation:**
   - **Directional Lights (`u_NumDirLights` $\le 4$):** Cascade 0 shadow evaluated via Cascaded Shadow Mapping (CSM) on `u_ShadowMapArray` (unit 3) with PCF (0=1-tap, 1=9-tap, 2=25-tap). Volumetric cloud shadow modulation via `CalculateCloudShadow`.
   - **Point Lights (`u_NumPointLights` $\le 32$):** Quadratic attenuation with linear distance cutoff.
   - **Spot Lights (`u_NumSpotLights` $\le 4$):** Inner/outer cone angular falloff + distance attenuation.
4. **Indirect Ambient & DDGI:**
   - Hemispheric fallback: `mix(groundColor, skyColor, hemiFactor) * albedo * ao`.
   - When `u_DDGIEnabled` is active: Samples oct-encoded irradiance probes via `SampleDDGI(vWorldPos, N)`.
5. **Environment Reflections (IBL):**
   - If `u_HasEnvCube`: Samples `u_EnvCube` (unit 18) at reflection vector $R = \text{reflect}(-V, N)$ with `textureLod` parameterized by $\text{roughness} \times 6.0$, modulated by Fresnel with roughness.
6. **Outputs:**
   - Location 0 (`FragColor`): Final color in HDR or linear space (tonemapped on Android).
   - Location 1 (`FragGeometry`): Octahedral normal, roughness, metallic for post-processing / SSR / deferred passes.

### 3.4 Unlit Fragment Shading Pipeline (`s_UnlitFrag`)
- Operates without lighting calculations, shadow lookups, or DDGI evaluation.
- Outputs `FragColor = u_Color * texture(u_MainTexture, vTexCoords)`.
- Optional gamma correction to linear (`u_ToLinear`).
- Outlines supported at vertex stage via `u_OutlineWidth` along vertex normal.

---

## 4. Uniforms, Textures, Matrices, and Time Variables

### 4.1 Global Camera Matrices & Vectors
Set during scene flushing (`ViewportPanel.cpp:1414-1420` and `SlipEngine.cpp:319-321`):

| Uniform Name | GLSL Type | Description |
|---|---|---|
| `u_Model` / `model` | `mat4` | Object Local-to-World matrix (per draw call) |
| `u_View` / `view` | `mat4` | World-to-View matrix |
| `u_Projection` / `projection` | `mat4` | View-to-Clip projection matrix |
| `u_CameraPos` | `vec3` | Camera world position |

### 4.2 Texture Unit Allocation Map
SlipEngine reserves specific texture units across the pipeline:

| Texture Unit | Sampler Uniform | Bound In | Description |
|---|---|---|---|
| **0** | `u_AlbedoMap` / `u_MainTexture` | `SlipMaterial::bindMaterialProperties` | Albedo / Diffuse base color |
| **1** | `u_NormalMap` | `SlipMaterial::bindMaterialProperties` | Tangent-space normal map |
| **2** | `u_RoughnessMap` | `SlipMaterial::bindMaterialProperties` | Roughness map (or packed ORM) |
| **3** | `u_ShadowMapArray` | `ViewportPanel` (Flush callback) | CSM Directional Light shadow cascade array |
| **4** | `u_MetallicMap` | `SlipMaterial::bindMaterialProperties` | Metallic map (or packed ORM) |
| **5 .. 14** | `u_DDGIIrr[5]`, `u_DDGIVis[5]` | `SlipDDGI::Bind` | DDGI probe irradiance & visibility atlas textures |
| **15** | `u_AOMap` | `SlipMaterial::bindMaterialProperties` | Ambient Occlusion map |
| **16** | `u_EmissiveMap` | `SlipMaterial::bindMaterialProperties` | Emissive map |
| **17** | `u_OpacityMap` | `SlipMaterial::bindMaterialProperties` | Opacity / alpha mask map |
| **18** | `u_EnvCube` | `ViewportPanel` (Flush callback) | Environment reflection cubemap (PC) |
| **19** | `u_CloudShadowNoise` | `ViewportPanel` / `SlipEngine` | Volumetric cloud noise texture (PC) |
| **20 .. 31** | **Available for Shader Graph** | **Free for custom samplers** | Available for custom Texture2D nodes |

> **Architectural Note:** OpenGL 3.3+ guarantees at least 16 fragment texture units, and all modern desktop hardware supports 32. For Shader Graph custom texture sampling, units **20 through 31** can be dynamically assigned to custom texture parameters without any collision with engine systems.

### 4.3 Time and Animation Variables
- **Current Engine State:**
  - `SlipGlobals::Get().GetDeltaTime()` returns current frame delta time (in seconds, clamped to $[0.0001, 0.2]$).
  - `SlipGlobals::update(double time)` receives cumulative elapsed time from high-precision performance counters (`currentCounter / perfFreq`).
  - Total elapsed time is stored in private member `currentFrame` in `SlipGlobals`.
- **Recommendation:**
  Add a getter `float GetTime() const { return currentFrame; }` to `SlipGlobals.h`.
  In `m_RenderQueue.Flush` (`ViewportPanel.cpp:1414`), pass:
  ```cpp
  shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
  shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
  ```
  This immediately enables time-animated shaders in all scene views and runtime.

---

## 5. Architectural Blueprint for Shader Graph Integration

### 5.1 System Overview
```
+-------------------------------------------------------------+
|                     Shader Graph Asset                      |
|                  (.shadergraph JSON file)                   |
|   - Material Type: Lit / Unlit                              |
|   - Nodes (Math, Trig, Time, Camera, Procedural, Textures)  |
|   - Connections (Links between node ports)                  |
|   - Exposed Parameters (Float, Vector, Color, Texture2D)    |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                    ShaderGraphCompiler                      |
|   - Topological sort of DAG                                 |
|   - GLSL Code Generator:                                    |
|       * Lit: Injects surface properties into PBR engine     |
|       * Unlit: Injects color/emission directly              |
|   - SlipShader::CreateFromSource(vertSrc, fragSrc)          |
|   - Register in SlipShaderLibrary                           |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                         SlipMaterial                        |
|   - References .shadergraph file path                       |
|   - Stores overrides for exposed parameters                 |
|   - In bindMaterialProperties(): binds uniforms & textures  |
|   - InspectorPanel renders dynamic property controls        |
+-------------------------------------------------------------+
```

### 5.2 Asset File Format (`.shadergraph`)
A clean JSON structure stored on disk with extension `.shadergraph`:
```json
{
  "materialType": "Lit",
  "exposedParameters": [
    { "id": "param_speed", "name": "Speed", "type": "Float", "defaultValue": 2.0 },
    { "id": "param_tint", "name": "BaseTint", "type": "Color", "defaultValue": [0.2, 0.6, 1.0, 1.0] },
    { "id": "param_noise", "name": "NoiseTex", "type": "Texture2D", "defaultValue": "assets/textures/noise.png" }
  ],
  "nodes": [
    {
      "id": 1,
      "type": "MasterLit",
      "position": [800, 300],
      "properties": {}
    },
    {
      "id": 2,
      "type": "Time",
      "position": [100, 200],
      "properties": {}
    },
    {
      "id": 3,
      "type": "Multiply",
      "position": [300, 250],
      "properties": { "b": 2.0 }
    }
  ],
  "links": [
    { "id": 101, "fromNode": 2, "fromPort": "time", "toNode": 3, "toPort": "a" },
    { "id": 102, "fromNode": 3, "fromPort": "out", "toNode": 1, "toPort": "roughness" }
  ]
}
```

### 5.3 Required Node Library
1. **Master Nodes:**
   - `MasterLit`: Ports: Albedo (`vec3`), Normal (`vec3`), Roughness (`float`), Metallic (`float`), Ambient Occlusion (`float`), Emissive (`vec3`), Alpha (`float`), AlphaCutoff (`float`).
   - `MasterUnlit`: Ports: Color / Emission (`vec3` or `vec4`), Alpha (`float`), AlphaCutoff (`float`).
2. **Time & Animation:**
   - `Time`: Ports: `time`, `deltaTime`, `sinTime`, `cosTime`.
3. **Camera & Geometry:**
   - `CameraPos`: World camera position (`vec3`).
   - `CameraMatrices`: `u_View`, `u_Projection`, `u_Model`.
   - `WorldPosition`, `WorldNormal`, `UV0`, `Tangent`, `Bitangent`.
4. **Mathematical Operations:**
   - Arithmetic: `Add`, `Subtract`, `Multiply`, `Divide`, `Power`, `Sqrt`, `Abs`, `Negate`, `Min`, `Max`, `Clamp`, `Lerp`, `Step`, `Smoothstep`, `Remap`.
   - Trigonometry: `Sin`, `Cos`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`.
   - Vector Math: `DotProduct`, `CrossProduct`, `Normalize`, `Length`, `Distance`, `Reflect`, `SplitVector`, `CombineVector`.
5. **Procedural Functions:**
   - `SineWave`, `Fresnel`, `Panner` (UV scroll with time), `RotateUV`, `Checkerboard`, `VoronoiNoise`, `SimplexNoise2D`, `GradientNoise`.
6. **Texture Sampling:**
   - `SampleTexture2D`: Samples a Texture parameter or static slot using input UVs.
7. **Exposed Parameters & Constants:**
   - `FloatConstant`, `Vector2Constant`, `Vector3Constant`, `ColorConstant`, `ParameterNode`.

### 5.4 GLSL Code Generator Architecture
The compiler generates standard vertex and fragment GLSL shaders:
- **Vertex Shader Template:** Standard `s_LitVert` with support for skeletal skinning and attribute passing. If the graph provides Vertex Offset, it modifies `localPos` before matrix multiplication.
- **Fragment Shader Template:**
  1. Header with GLSL version (`#version 330 core` or `#version 300 es`).
  2. Built-in uniform declarations: `u_Time`, `u_DeltaTime`, `u_CameraPos`, `u_Model`, `u_View`, `u_Projection`.
  3. Exposed parameter uniform declarations:
     - `uniform float u_sg_Speed;`
     - `uniform vec4 u_sg_BaseTint;`
     - `uniform sampler2D u_sg_NoiseTex;`
  4. Node functions and procedural helper implementations.
  5. Evaluated code in topological dependency order:
     - Variable assignments matching port connections (e.g. `float node_3_out = u_Time * u_sg_Speed;`).
  6. Material Master Node resolution:
     - **For Unlit:** Directly writes `FragColor = vec4(node_master_color, node_master_alpha);`.
     - **For Lit:** Evaluates node outputs for surface properties, then executes SlipEngine's standard Cook-Torrance BRDF function (`Lo + ambient + emissive`), matching `s_LitFrag` with full lighting, CSM shadows, and DDGI!

### 5.5 `SlipMaterial` Modifications
1. **Members to add:**
   ```cpp
   std::string shaderGraphPath; // Path to .shadergraph asset
   std::unordered_map<std::string, float> floatParameters;
   std::unordered_map<std::string, glm::vec4> vectorParameters;
   std::unordered_map<std::string, std::string> textureParameters;
   std::unordered_map<std::string, SlipTexture*> cachedTextureParams;
   ```
2. **In `SlipMaterial::init()`:**
   If `!shaderGraphPath.empty()`, load and compile the shader graph (or fetch from `SlipShaderLibrary`), setting `shader` to the compiled result.
3. **In `SlipMaterial::bindMaterialProperties(targetShader)`:**
   ```cpp
   for (const auto& [name, val] : floatParameters) {
       targetShader->setFloat("u_sg_" + name, val);
   }
   for (const auto& [name, val] : vectorParameters) {
       targetShader->setVec4("u_sg_" + name, val);
   }
   int texSlot = 20; // Use units 20+ for graph textures
   for (const auto& [name, path] : textureParameters) {
       if (texSlot <= 31) {
           SlipTexture* tex = cachedTextureParams[name];
           if (tex) {
               targetShader->setInt("u_sg_" + name, texSlot);
               tex->bind(texSlot);
               texSlot++;
           }
       }
   }
   ```
4. **Serialization in `.mat`:**
   ```text
   shaderGraph: assets/shaders/MyEffect.shadergraph
   param.float.Speed: 2.5
   param.color.BaseTint: 0.2 0.6 1.0 1.0
   param.texture.NoiseTex: assets/textures/noise.png
   ```

### 5.6 Material Inspector Integration (`InspectorPanel.cpp`)
In `RenderSlipMaterialPropertiesEditor()`:
- Add selector for "Shader Type": Lit, Unlit, or Shader Graph.
- When Shader Graph is selected:
  - Display file picker / drag-and-drop box for `.shadergraph` assets.
  - Button `[Abrir en Shader Graph]` opens the node editor panel.
  - Dynamically parse the exposed parameter definitions from the graph asset and render:
    - `ImGui::SliderFloat` or `DragFloat` for Floats.
    - `ImGui::ColorEdit4` or `ColorEdit3` for Colors / Vectors.
    - `RenderTextureSlotControl` for Texture2D parameters.

---

## 6. Verification and Testing Strategy

1. **Graph Serialization Unit Test:**
   - Test saving a node graph with math, time, and texture nodes to a `.shadergraph` file and reloading it.
   - Assert exact preservation of node count, node types, port links, coordinates, and exposed parameters.
2. **GLSL Code Generation Verification:**
   - Generate GLSL code for an Unlit animated material using `u_DeltaTime`, `Sin`, and procedural UV panning.
   - Generate GLSL code for a Lit PBR material using custom procedural roughness, normal mapping, and exposed tint parameters.
   - Compile both shaders using `SlipShader::CreateFromSource` in an active OpenGL context (or verify GLSL syntax correctness in headless test).
3. **Material Assignment & Parameter Override Test:**
   - Create a `SlipMaterial`, assign the shader graph asset, override exposed float and color properties, and verify `bindMaterialProperties` binds them to shader uniform locations.
4. **Sample Asset Deliverable:**
   - Ship a functional sample `.shadergraph` asset (e.g. `assets/shaders/PBR_WaterDisplacement.shadergraph` or `assets/shaders/AnimatedProceduralEnergy.shadergraph`) demonstrating mathematical animation using `deltaTime` and procedural math.

---

## 7. Concrete Code Files & Modification Locations

| File | Modification / Addition |
|---|---|
| `include/SlipEngine/render/SlipMaterial.h` | Add `shaderGraphPath`, parameter override maps (`floatParameters`, `vectorParameters`, `textureParameters`), texture cache. |
| `src/render/SlipMaterial.cpp` | Update `loadMatFromFile`, `Save`, `init`, and `bindMaterialProperties` to support Shader Graph resolution and exposed parameter binding. |
| `include/SlipEngine/core/SlipGlobals.h` | Add `float GetTime() const { return currentFrame; }`. |
| `src/editor/panels/ViewportPanel.cpp` | In `m_RenderQueue.Flush` callback (line 1414), bind `u_Time` and `u_DeltaTime`. |
| `include/SlipEngine/render/SlipShaderLibrary.h` & `.cpp` | Add `RegisterShader(name, shader)` to allow dynamic registration of generated programs. |
| **New:** `include/SlipEngine/render/ShaderGraph/` | Header files for `ShaderGraph`, `ShaderNode`, `ShaderLink`, `ShaderGraphCompiler`. |
| **New:** `src/render/ShaderGraph/` | Implementation files for node graph evaluation, serialization, and GLSL code generation. |
| **New:** `include/SlipEngine/editor/panels/ShaderGraphPanel.h` | Node-based editor panel with ImGui canvas, palette, zooming, dragging, port connections. |
| **New:** `src/editor/panels/ShaderGraphPanel.cpp` | Implementation of node editor canvas, context menus, property inspector, Lit/Unlit master node toggling. |
| `src/editor/SlipEditor.cpp` | Register `ShaderGraphPanel` in `m_Panels` and add menu item in main menu bar. |
| `src/editor/panels/InspectorPanel.cpp` | Update `RenderSlipMaterialPropertiesEditor` to inspect Shader Graph parameters and link to editor. |
| `src/editor/panels/ContentBrowserPanel.cpp` | Add context menu for "Nuevo Shader Graph (.shadergraph)" and double-click to open. |
| `CMakeLists.txt` & `tests/` | Add `SlipTestShaderGraph` test target verifying serialization, code generation, and material binding. |
