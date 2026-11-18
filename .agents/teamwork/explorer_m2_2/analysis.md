# SlipMaterial Shader Graph Runtime Integration Analysis

## 1. Executive Summary

This report establishes the complete architectural design and code diffs for integrating SlipEngine's node-based Shader Graph system into the engine's core material pipeline (`SlipMaterial`).

Key findings and architectural decisions:
1. **Asset Referencing & Parameter Storage**: `SlipMaterial` stores `shaderGraphPath` and three dedicated parameter override hash maps (`graphFloatParams`, `graphVectorParams`, `graphTextureParams`), alongside a cached list of graph property declarations (`graphProperties`) for editor inspection without disk I/O penalties.
2. **Dynamic Shader Compilation & Lifecycle**: In `SlipMaterial::init()` and `SlipMaterial::LoadShaderGraph()`, graphs are deserialized via `ShaderGraphSerializer`, compiled to GLSL via `ShaderGraphCompiler::Compile(graph)`, and compiled into active OpenGL programs via `SlipShader::CreateFromSource`. Shader lifecycle is managed using `std::shared_ptr<SlipShader>` with a custom OpenGL deleter (`glDeleteProgram`), ensuring zero GPU/CPU memory leaks and safe copy semantics when materials are inspected or cloned.
3. **Collision-Free Uniform & Texture Binding**: Custom texture properties bind starting at OpenGL texture unit 20 (up to 31), preventing conflicts with SlipEngine's reserved texture units 0..19 (Albedo: 0, Normal: 1, Roughness: 2, CSM shadow array: 3, Metallic: 4, DDGI probes: 5..14, AO: 15, Emissive: 16, Opacity: 17, Env/Reflection: 18..19).
4. **Bidirectional Serialization**: `.mat` persistence supports both `graphFloat_<name>` / `graphVector_<name>` / `graphTexture_<name>` (matching automated tests in `tests/test_shader_graph.cpp`) and `graphParam.<name>` / `graphParam.<type>.<name>`.
5. **Critical Engine Rendering Fix**: Identified that `SlipRenderQueue::Flush()` currently bypasses `cmd.material->shader` and forces a lookup from `SlipShaderLibrary::Get(sType)`. We specify the exact 3-line patch in `SlipRenderQueue.cpp` to ensure graph materials render in the viewport.

---

## 2. Architectural Overview

```
                      ┌─────────────────────────────────────────┐
                      │    Shader Graph Asset (.shadergraph)    │
                      └────────────────────┬────────────────────┘
                                           │
                                           │ Deserialized by ShaderGraphSerializer
                                           ▼
                      ┌─────────────────────────────────────────┐
                      │    SlipEngine::ShaderGraphCompiler      │
                      │  - Topological sort of node DAG         │
                      │  - GLSL Vertex & Fragment Generation    │
                      └────────────────────┬────────────────────┘
                                           │
                                           │ GeneratedShaderCode (vertexSrc, fragmentSrc)
                                           ▼
                      ┌─────────────────────────────────────────┐
                      │    SlipShader::CreateFromSource         │
                      │  - glCreateShader, glCompileShader      │
                      │  - glCreateProgram, glLinkProgram       │
                      └────────────────────┬────────────────────┘
                                           │
                                           │ std::shared_ptr<SlipShader> (m_CustomShader)
                                           ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                                   SlipMaterial                                  │
  │  Fields:                                                                        │
  │   - shaderGraphPath                                                             │
  │   - graphFloatParams    (std::unordered_map<std::string, float>)                │
  │   - graphVectorParams   (std::unordered_map<std::string, glm::vec4>)            │
  │   - graphTextureParams  (std::unordered_map<std::string, std::string>)          │
  │   - graphProperties     (std::vector<ShaderProperty> blackboard cache)          │
  │   - shader              (Raw pointer pointing to m_CustomShader.get())          │
  │                                                                                 │
  │  Methods:                                                                       │
  │   - LoadShaderGraph(path)                                                       │
  │   - SetGraphFloat / SetGraphVector / SetGraphTexture                            │
  │   - bindMaterialProperties(targetShader) -> binds parameters (textures at 20+)  │
  │   - loadMatFromFile / Save -> serializes graph path & overrides                 │
  └────────────────────────┬────────────────────────────────────────────────────────┘
                           │
                           │ Submitted to Render Queue
                           ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │                                SlipRenderQueue                                  │
  │  - Uses cmd.material->shader if present (custom graph program)                  │
  │  - Sorts commands by shader program ID & material pointer                       │
  │  - Dispatches uniform bindings & draws submeshes                                │
  └─────────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Data Structure & Interface Design in `SlipMaterial.h`

### 3.1 Field Additions
```cpp
// Shader Graph Integration
std::string shaderGraphPath;
std::unordered_map<std::string, float> graphFloatParams;
std::unordered_map<std::string, glm::vec4> graphVectorParams;
std::unordered_map<std::string, std::string> graphTextureParams;
std::vector<SlipEngine::ShaderProperty> graphProperties;
```

### 3.2 Method Additions
```cpp
// Shader Graph Methods
bool LoadShaderGraph(const std::string& path);
void SetGraphFloat(const std::string& name, float value);
void SetGraphVector(const std::string& name, const glm::vec4& value);
void SetGraphTexture(const std::string& name, const std::string& texturePath);

float GetGraphFloat(const std::string& name, float fallback = 0.0f) const;
glm::vec4 GetGraphVector(const std::string& name, const glm::vec4& fallback = glm::vec4(0.0f)) const;
std::string GetGraphTexture(const std::string& name, const std::string& fallback = "") const;

bool HasGraphFloat(const std::string& name) const;
bool HasGraphVector(const std::string& name) const;
bool HasGraphTexture(const std::string& name) const;

const std::vector<SlipEngine::ShaderProperty>& GetGraphProperties() const { return graphProperties; }
```

### 3.3 Shader Ownership & Safety
`SlipMaterial` uses a private member:
```cpp
private:
    std::shared_ptr<SlipShader> m_CustomShader;
```
When `LoadShaderGraph` compiles a new program:
```cpp
m_CustomShader = std::shared_ptr<SlipShader>(newShader, [](SlipShader* s) {
    if (s) {
        s->destroy(); // calls glDeleteProgram(ID)
        delete s;
    }
});
this->shader = m_CustomShader.get();
```
**Rationale**:
- `this->shader` remains a raw `SlipShader*`, retaining 100% backwards compatibility with all existing engine render routines.
- If a `SlipMaterial` instance is copied (e.g. `s_InspectedMat = SlipMaterial(...)` in `InspectorPanel.cpp`), both copies safely increment the reference count. No double-free occurs when one goes out of scope.
- When all references are destroyed, the custom deleter automatically destroys the OpenGL program and deletes the C++ instance.
- Calling `LoadShaderGraph` with a new graph cleanly releases the previous OpenGL program without leaking GPU resources.

---

## 4. Uniform & Texture Binding Design (`bindMaterialProperties`)

### 4.1 Texture Unit Allocation Map
Investigation of `SlipMaterial.cpp`, `SlipShadows.cpp`, `SlipDDGI.cpp`, and `ViewportPanel.cpp` confirms the engine's active texture unit mapping:

| Unit Range | Subsystem / Texture Slot | Source File |
|---|---|---|
| `GL_TEXTURE0` | Albedo / Diffuse Map (`u_AlbedoMap`) | `SlipMaterial.cpp:325` |
| `GL_TEXTURE1` | Normal Map (`u_NormalMap`) | `SlipMaterial.cpp:335` |
| `GL_TEXTURE2` | Roughness Map (`u_RoughnessMap`) | `SlipMaterial.cpp:343` |
| `GL_TEXTURE3` | Cascaded Shadow Map Array (`u_ShadowMapArray`) | `ViewportPanel.cpp:1450` |
| `GL_TEXTURE4` | Metallic Map (`u_MetallicMap`) | `SlipMaterial.cpp:351` |
| `GL_TEXTURE5..14` | DDGI Irradiance, Distance & Probe Arrays | `SlipDDGI.cpp`, `ViewportPanel.cpp:1503` |
| `GL_TEXTURE15` | Ambient Occlusion Map (`u_AOMap`) | `SlipMaterial.cpp:362` |
| `GL_TEXTURE16` | Emissive Map (`u_EmissiveMap`) | `SlipMaterial.cpp:370` |
| `GL_TEXTURE17` | Opacity / Cutout Mask (`u_OpacityMap`) | `SlipMaterial.cpp:378` |
| `GL_TEXTURE18` | Reflection Probe Texture Array | `ViewportPanel.cpp:1471` |
| `GL_TEXTURE19` | Environment Cube / Sky Reflection | `ViewportPanel.cpp:1506` |
| **`GL_TEXTURE20..31`** | **Custom Shader Graph Textures** | **`SlipMaterial::bindGraphParameters`** |

OpenGL 3.3 requires minimum 48 combined texture image units (`GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS` is typically 80 to 192 on desktop GPUs). Assigning custom texture parameters to units 20..31 provides 12 simultaneous custom texture samplers while guaranteeing **zero collision** with the engine's PBR, CSM, and DDGI pipelines.

### 4.2 Parameter Binding Algorithm
In `bindGraphParameters(SlipShader* targetShader)`:
1. Iterate through `graphProperties`:
   - For `PinType::Float`: read from `graphFloatParams[prop.name]` (or `"u_" + prop.name`), fallback to `prop.defaultValue.x`. Call `targetShader->setFloat(uName, val)`.
   - For `PinType::Vec2`: read from `graphVectorParams`, fallback to `glm::vec2(prop.defaultValue)`. Call `targetShader->setVec2(uName, val)`.
   - For `PinType::Vec3`: read from `graphVectorParams`, fallback to `glm::vec3(prop.defaultValue)`. Call `targetShader->setVec3(uName, val)`.
   - For `PinType::Vec4`: read from `graphVectorParams`, fallback to `prop.defaultValue`. Call `targetShader->setVec4(uName, val)`.
   - For `PinType::Boolean`: read from `graphFloatParams`, fallback to `prop.defaultValue.x > 0.5f`. Call `targetShader->setBool(uName, val)`.
   - For `PinType::Texture2D`: resolve texture path from `graphTextureParams[prop.name]` (fallback `prop.defaultTexturePath`). If non-empty and `texUnit < 32`:
     ```cpp
     SlipTexture* tex = SlipTexture::load(texPath.c_str());
     if (tex) {
         targetShader->setInt(uName, texUnit);
         tex->bind(texUnit);
     }
     texUnit++;
     ```
2. Iterate through any unmapped entries in `graphFloatParams`, `graphVectorParams`, and `graphTextureParams` to guarantee all user-specified overrides are bound even if the material was loaded from disk prior to graph compilation.

---

## 5. Material File (.mat) Serialization & Deserialization

### 5.1 Format Compatibility
To ensure 100% compatibility with both the automated test suite in `tests/test_shader_graph.cpp` and the project requirements:
- **Test Suite Format**: `graphFloat_<name>: <val>`, `graphVector_<name>: <x> <y> <z> <w>`, `graphTexture_<name>: <path>`
- **Dispatch Format**: `graphParam.<name>: <val>`, `graphParam.<type>.<name>: <val>`

In `SlipMaterial::Save`:
```cpp
if (!shaderGraphPath.empty()) {
    ss << "shaderGraph: " << shaderGraphPath << "\n";
    for (const auto& pair : graphFloatParams) {
        ss << "graphFloat_" << pair.first << ": " << pair.second << "\n";
        ss << "graphParam." << pair.first << ": " << pair.second << "\n";
    }
    for (const auto& pair : graphVectorParams) {
        ss << "graphVector_" << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
        ss << "graphParam." << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
    }
    for (const auto& pair : graphTextureParams) {
        ss << "graphTexture_" << pair.first << ": " << pair.second << "\n";
        ss << "graphParam." << pair.first << ": " << pair.second << "\n";
    }
}
```

In `SlipMaterial::loadMatFromFile`:
The parser checks prefixes:
- `key == "shaderGraph" || key == "shaderGraphPath"` -> sets `shaderGraphPath = val;`
- `key.rfind("graphFloat_", 0) == 0` -> extracts name from offset 11, sets float.
- `key.rfind("graphVector_", 0) == 0` -> extracts name from offset 12, parses 4 floats into `glm::vec4`.
- `key.rfind("graphTexture_", 0) == 0` -> extracts name from offset 13, sets texture path.
- `key.rfind("graphParam.float.", 0) == 0` -> extracts name from offset 17, sets float.
- `key.rfind("graphParam.vec4.", 0) == 0` -> extracts name from offset 16, parses 4 floats into `glm::vec4`.
- `key.rfind("graphParam.texture.", 0) == 0` -> extracts name from offset 19, sets texture path.
- `key.rfind("graphParam.", 0) == 0` -> extracts name from offset 11, checks token count: if >1 tokens -> vector; if 1 numerical token -> float; else -> texture path.

At the end of parsing, `init()` is called, which automatically triggers `LoadShaderGraph(shaderGraphPath)`.

---

## 6. Critical Engine Rendering Fix: `SlipRenderQueue.cpp`

### 6.1 Issue Identification
In `src/render/SlipRenderQueue.cpp`, lines 57-63:
```cpp
for (const auto& cmd : m_Commands) {
    SlipShader* shader = nullptr;
    std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
    if (sType == "default") sType = "Lit";
    shader = SlipShaderLibrary::Get(sType);
    if (!shader) shader = SlipShaderLibrary::Get("Lit");
    if (!shader) continue;
```
If `cmd.material` has a custom compiled Shader Graph program (`cmd.material->shader != nullptr`), `SlipRenderQueue` completely ignores it and falls back to `SlipShaderLibrary::Get(sType)` (standard Lit or Unlit shader). Consequently, custom shaders never execute in the viewport!

### 6.2 Proposed Fix
```cpp
for (const auto& cmd : m_Commands) {
    SlipShader* shader = (cmd.material && cmd.material->shader) ? cmd.material->shader : nullptr;
    if (!shader) {
        std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
        if (sType == "default") sType = "Lit";
        shader = SlipShaderLibrary::Get(sType);
        if (!shader) shader = SlipShaderLibrary::Get("Lit");
    }
    if (!shader) continue;
```
With this fix, any material utilizing a Shader Graph renders using its generated OpenGL shader program seamlessly.

---

## 7. Exact Code Diffs

### 7.1 Diff for `include/SlipEngine/render/SlipMaterial.h`

```diff
--- a/include/SlipEngine/render/SlipMaterial.h
+++ b/include/SlipEngine/render/SlipMaterial.h
@@ -6,8 +6,12 @@
 
 #include <glm/glm.hpp>
 #include <vector>
+#include <string>
+#include <unordered_map>
+#include <memory>
 
 #include "SlipEngine.h"
+#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
 
 #include "SlipShader.h"
 #include "SlipCamera.h"
@@ -17,8 +21,10 @@ class SLIPENGINE_API SlipMaterial
 {
 private:
-
-	//void createTexture(const char* type);
+	std::shared_ptr<SlipShader> m_CustomShader;
 
 	void loadMatFromFile(std::string path);
+	void bindGraphParameters(SlipShader* targetShader);
 public:
 	std::string path;
 
@@ -39,6 +45,13 @@ public:
 	char shaderType[32] = "Lit";
 	SlipShader* shader = nullptr;
 
+	// Shader Graph Integration
+	std::string shaderGraphPath;
+	std::unordered_map<std::string, float> graphFloatParams;
+	std::unordered_map<std::string, glm::vec4> graphVectorParams;
+	std::unordered_map<std::string, std::string> graphTextureParams;
+	std::vector<SlipEngine::ShaderProperty> graphProperties;
+
 	char diffPath[192] = "";
 	SlipTexture* diffuseTexture = nullptr;
 
@@ -77,6 +90,20 @@ public:
 	void init();
 	void bind(glm::mat4& model);
 	void bindMaterialProperties(SlipShader* targetShader);
+
+	// Shader Graph Methods
+	bool LoadShaderGraph(const std::string& path);
+	void SetGraphFloat(const std::string& name, float value);
+	void SetGraphVector(const std::string& name, const glm::vec4& value);
+	void SetGraphTexture(const std::string& name, const std::string& texturePath);
+
+	float GetGraphFloat(const std::string& name, float fallback = 0.0f) const;
+	glm::vec4 GetGraphVector(const std::string& name, const glm::vec4& fallback = glm::vec4(0.0f)) const;
+	std::string GetGraphTexture(const std::string& name, const std::string& fallback = "") const;
+	bool HasGraphFloat(const std::string& name) const;
+	bool HasGraphVector(const std::string& name) const;
+	bool HasGraphTexture(const std::string& name) const;
+	const std::vector<SlipEngine::ShaderProperty>& GetGraphProperties() const { return graphProperties; }
 };
 
 #endif // !SLIP_MATERIAL_H
```

---

### 7.2 Diff for `src/render/SlipMaterial.cpp`

```diff
--- a/src/render/SlipMaterial.cpp
+++ b/src/render/SlipMaterial.cpp
@@ -4,6 +4,9 @@
 #include "SlipShadows.h"
 #include "SlipEngine/tools/SlipFileSystem.h"
 #include "SlipEngine/render/SlipShaderLibrary.h"
+#include "SlipEngine/render/shadergraph/ShaderGraph.h"
+#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
+#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
 #include <sstream>
 #include <algorithm>
 #include <cstring>
@@ -84,6 +87,11 @@ void SlipMaterial::loadMatFromFile(std::string matPath)
     if (!foundPath.empty()) {
         this->path = foundPath;
 
+        shaderGraphPath.clear();
+        graphFloatParams.clear();
+        graphVectorParams.clear();
+        graphTextureParams.clear();
+
         std::string content;
         if (SlipFileSystem::ReadAllText(foundPath, content)) {
             bool isText = true;
@@ -108,6 +116,49 @@ void SlipMaterial::loadMatFromFile(std::string matPath)
                     if (key == "shader" || key == "shaderType") {
                         std::strncpy(shaderType, val.c_str(), sizeof(shaderType) - 1);
                         if (std::string(shaderType) == "default") std::strncpy(shaderType, "Lit", sizeof(shaderType) - 1);
+                    } else if (key == "shaderGraph" || key == "shaderGraphPath") {
+                        shaderGraphPath = val;
+                    } else if (key.rfind("graphFloat_", 0) == 0) {
+                        std::string pName = key.substr(11);
+                        graphFloatParams[pName] = std::stof(val);
+                    } else if (key.rfind("graphVector_", 0) == 0) {
+                        std::string pName = key.substr(12);
+                        std::stringstream vs(val);
+                        glm::vec4 v(0.0f);
+                        vs >> v.x >> v.y >> v.z >> v.w;
+                        graphVectorParams[pName] = v;
+                    } else if (key.rfind("graphTexture_", 0) == 0) {
+                        std::string pName = key.substr(13);
+                        graphTextureParams[pName] = val;
+                    } else if (key.rfind("graphParam.float.", 0) == 0) {
+                        std::string pName = key.substr(17);
+                        graphFloatParams[pName] = std::stof(val);
+                    } else if (key.rfind("graphParam.vec4.", 0) == 0) {
+                        std::string pName = key.substr(16);
+                        std::stringstream vs(val);
+                        glm::vec4 v(0.0f);
+                        vs >> v.x >> v.y >> v.z >> v.w;
+                        graphVectorParams[pName] = v;
+                    } else if (key.rfind("graphParam.texture.", 0) == 0) {
+                        std::string pName = key.substr(19);
+                        graphTextureParams[pName] = val;
+                    } else if (key.rfind("graphParam.", 0) == 0) {
+                        std::string pName = key.substr(11);
+                        std::stringstream vs(val);
+                        std::vector<float> floats;
+                        float f;
+                        while (vs >> f) floats.push_back(f);
+                        if (floats.size() > 1) {
+                            glm::vec4 v(0.0f);
+                            if (floats.size() >= 1) v.x = floats[0];
+                            if (floats.size() >= 2) v.y = floats[1];
+                            if (floats.size() >= 3) v.z = floats[2];
+                            if (floats.size() >= 4) v.w = floats[3];
+                            graphVectorParams[pName] = v;
+                        } else if (floats.size() == 1) {
+                            graphFloatParams[pName] = floats[0];
+                        } else {
+                            graphTextureParams[pName] = val;
+                        }
                     } else if (key == "color" || key == "albedo") {
                         std::stringstream vs(val);
                         vs >> color.r >> color.g >> color.b;
@@ -254,6 +305,23 @@ bool SlipMaterial::Save(const std::string& savePath)
     ss << "alphaClip: " << (alphaClip ? 1 : 0) << "\n";
     ss << "alphaCutoff: " << alphaCutoff << "\n";
     ss << "invertRoughness: " << (invertRoughness ? 1 : 0) << "\n";
     ss << "doubleSided: " << (doubleSided ? 1 : 0) << "\n";
+
+    if (!shaderGraphPath.empty()) {
+        ss << "shaderGraph: " << shaderGraphPath << "\n";
+        for (const auto& pair : graphFloatParams) {
+            ss << "graphFloat_" << pair.first << ": " << pair.second << "\n";
+            ss << "graphParam." << pair.first << ": " << pair.second << "\n";
+        }
+        for (const auto& pair : graphVectorParams) {
+            ss << "graphVector_" << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
+            ss << "graphParam." << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
+        }
+        for (const auto& pair : graphTextureParams) {
+            ss << "graphTexture_" << pair.first << ": " << pair.second << "\n";
+            ss << "graphParam." << pair.first << ": " << pair.second << "\n";
+        }
+    }
 
     path = target;
     return SlipFileSystem::WriteAllText(target, ss.str());
@@ -267,6 +335,7 @@ SlipMaterial::~SlipMaterial()
 {
     // El shader es gestionado por SlipShaderLibrary y las texturas por el caché de SlipTexture
+    m_CustomShader.reset();
     shader = nullptr;
     diffuseTexture = nullptr;
     normalTexture = nullptr;
@@ -280,11 +349,150 @@ void SlipMaterial::init()
 {
     if (!glad_glCreateProgram) return;
+
+    if (!shaderGraphPath.empty()) {
+        if (LoadShaderGraph(shaderGraphPath)) {
+            return;
+        }
+    }
+
+    m_CustomShader.reset();
     std::string sType = std::string(shaderType);
-    if (sType.empty() || sType == "default") sType = "Lit";
+    if (sType.empty() || sType == "default" || sType == "ShaderGraph") sType = "Lit";
     shader = SlipShaderLibrary::LoadOrGet(sType);
 }
 
+bool SlipMaterial::LoadShaderGraph(const std::string& sgPath)
+{
+    if (sgPath.empty()) {
+        shaderGraphPath = "";
+        m_CustomShader.reset();
+        shader = nullptr;
+        graphProperties.clear();
+        init();
+        return true;
+    }
+
+    std::string resolved = SlipEngine::ShaderGraphSerializer::ResolveGraphPath(sgPath);
+    if (!SlipFileSystem::Exists(resolved)) {
+        return false;
+    }
+
+    SlipEngine::ShaderGraph graph;
+    std::string errorMsg;
+    if (!SlipEngine::ShaderGraphSerializer::DeserializeFromFile(graph, resolved, &errorMsg)) {
+        return false;
+    }
+
+    SlipEngine::GeneratedShaderCode code = SlipEngine::ShaderGraphCompiler::Compile(graph);
+    if (!code.success) {
+        return false;
+    }
+
+    SlipShader* newShader = SlipShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
+    if (!newShader || newShader->ID == 0) {
+        if (newShader) delete newShader;
+        return false;
+    }
+
+    shaderGraphPath = sgPath;
+    graphProperties = graph.properties;
+
+    m_CustomShader = std::shared_ptr<SlipShader>(newShader, [](SlipShader* s) {
+        if (s) {
+            s->destroy();
+            delete s;
+        }
+    });
+    shader = m_CustomShader.get();
+
+    std::strncpy(shaderType, (graph.materialType == SlipEngine::ShaderGraphType::Unlit) ? "Unlit" : "Lit", sizeof(shaderType) - 1);
+    shaderType[sizeof(shaderType) - 1] = '\0';
+
+    return true;
+}
+
+void SlipMaterial::SetGraphFloat(const std::string& name, float value)
+{
+    graphFloatParams[name] = value;
+}
+
+void SlipMaterial::SetGraphVector(const std::string& name, const glm::vec4& value)
+{
+    graphVectorParams[name] = value;
+}
+
+void SlipMaterial::SetGraphTexture(const std::string& name, const std::string& texturePath)
+{
+    graphTextureParams[name] = texturePath;
+}
+
+float SlipMaterial::GetGraphFloat(const std::string& name, float fallback) const
+{
+    auto it = graphFloatParams.find(name);
+    if (it != graphFloatParams.end()) return it->second;
+    for (const auto& prop : graphProperties) {
+        if (prop.name == name || prop.id == name) return prop.defaultValue.x;
+    }
+    return fallback;
+}
+
+glm::vec4 SlipMaterial::GetGraphVector(const std::string& name, const glm::vec4& fallback) const
+{
+    auto it = graphVectorParams.find(name);
+    if (it != graphVectorParams.end()) return it->second;
+    for (const auto& prop : graphProperties) {
+        if (prop.name == name || prop.id == name) return prop.defaultValue;
+    }
+    return fallback;
+}
+
+std::string SlipMaterial::GetGraphTexture(const std::string& name, const std::string& fallback) const
+{
+    auto it = graphTextureParams.find(name);
+    if (it != graphTextureParams.end()) return it->second;
+    for (const auto& prop : graphProperties) {
+        if (prop.name == name || prop.id == name) return prop.defaultTexturePath;
+    }
+    return fallback;
+}
+
+bool SlipMaterial::HasGraphFloat(const std::string& name) const
+{
+    return graphFloatParams.find(name) != graphFloatParams.end();
+}
+
+bool SlipMaterial::HasGraphVector(const std::string& name) const
+{
+    return graphVectorParams.find(name) != graphVectorParams.end();
+}
+
+bool SlipMaterial::HasGraphTexture(const std::string& name) const
+{
+    return graphTextureParams.find(name) != graphTextureParams.end();
+}
+
+void SlipMaterial::bindGraphParameters(SlipShader* targetShader)
+{
+    if (!targetShader) return;
+
+    unsigned int customTexUnit = 20;
+    std::vector<std::string> boundProps;
+
+    for (const auto& prop : graphProperties) {
+        std::string uName = prop.GetUniformName();
+        boundProps.push_back(prop.name);
+
+        switch (prop.type) {
+            case SlipEngine::PinType::Float: {
+                float val = prop.defaultValue.x;
+                auto it = graphFloatParams.find(prop.name);
+                if (it != graphFloatParams.end()) val = it->second;
+                else {
+                    auto it2 = graphFloatParams.find(uName);
+                    if (it2 != graphFloatParams.end()) val = it2->second;
+                }
+                targetShader->setFloat(uName, val);
+                break;
+            }
+            case SlipEngine::PinType::Vec2: {
+                glm::vec2 val = glm::vec2(prop.defaultValue);
+                auto it = graphVectorParams.find(prop.name);
+                if (it != graphVectorParams.end()) val = glm::vec2(it->second);
+                else {
+                    auto it2 = graphVectorParams.find(uName);
+                    if (it2 != graphVectorParams.end()) val = glm::vec2(it2->second);
+                }
+                targetShader->setVec2(uName, val);
+                break;
+            }
+            case SlipEngine::PinType::Vec3: {
+                glm::vec3 val = glm::vec3(prop.defaultValue);
+                auto it = graphVectorParams.find(prop.name);
+                if (it != graphVectorParams.end()) val = glm::vec3(it->second);
+                else {
+                    auto it2 = graphVectorParams.find(uName);
+                    if (it2 != graphVectorParams.end()) val = glm::vec3(it2->second);
+                }
+                targetShader->setVec3(uName, val);
+                break;
+            }
+            case SlipEngine::PinType::Vec4: {
+                glm::vec4 val = prop.defaultValue;
+                auto it = graphVectorParams.find(prop.name);
+                if (it != graphVectorParams.end()) val = it->second;
+                else {
+                    auto it2 = graphVectorParams.find(uName);
+                    if (it2 != graphVectorParams.end()) val = it2->second;
+                }
+                targetShader->setVec4(uName, val);
+                break;
+            }
+            case SlipEngine::PinType::Boolean: {
+                bool val = (prop.defaultValue.x > 0.5f);
+                auto it = graphFloatParams.find(prop.name);
+                if (it != graphFloatParams.end()) val = (it->second > 0.5f);
+                targetShader->setBool(uName, val);
+                break;
+            }
+            case SlipEngine::PinType::Texture2D: {
+                std::string texPath = prop.defaultTexturePath;
+                auto it = graphTextureParams.find(prop.name);
+                if (it != graphTextureParams.end() && !it->second.empty()) {
+                    texPath = it->second;
+                } else {
+                    auto it2 = graphTextureParams.find(uName);
+                    if (it2 != graphTextureParams.end() && !it2->second.empty()) {
+                        texPath = it2->second;
+                    }
+                }
+
+                if (!texPath.empty() && customTexUnit < 32) {
+                    SlipTexture* tex = SlipTexture::load(texPath.c_str());
+                    if (tex) {
+                        targetShader->setInt(uName, (int)customTexUnit);
+                        tex->bind(customTexUnit);
+                    }
+                }
+                customTexUnit++;
+                break;
+            }
+            default:
+                break;
+        }
+    }
+
+    for (const auto& pair : graphFloatParams) {
+        if (std::find(boundProps.begin(), boundProps.end(), pair.first) != boundProps.end()) continue;
+        std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
+        targetShader->setFloat(uName, pair.second);
+    }
+    for (const auto& pair : graphVectorParams) {
+        if (std::find(boundProps.begin(), boundProps.end(), pair.first) != boundProps.end()) continue;
+        std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
+        targetShader->setVec4(uName, pair.second);
+    }
+    for (const auto& pair : graphTextureParams) {
+        if (std::find(boundProps.begin(), boundProps.end(), pair.first) != boundProps.end()) continue;
+        if (pair.second.empty() || customTexUnit >= 32) continue;
+        std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
+        SlipTexture* tex = SlipTexture::load(pair.second.c_str());
+        if (tex) {
+            targetShader->setInt(uName, (int)customTexUnit);
+            tex->bind(customTexUnit);
+        }
+        customTexUnit++;
+    }
+}
+
 void SlipMaterial::bindMaterialProperties(SlipShader* targetShader)
 {
     if (!targetShader) return;
+
+    if (!shaderGraphPath.empty()) {
+        bindGraphParameters(targetShader);
+    }
 
     std::string sType = std::string(shaderType);
     if (sType == "Unlit") {
```

---

### 7.3 Diff for `src/render/SlipRenderQueue.cpp`

```diff
--- a/src/render/SlipRenderQueue.cpp
+++ b/src/render/SlipRenderQueue.cpp
@@ -55,7 +55,11 @@ void SlipRenderQueue::Flush(const std::function<void(SlipShader*)>& onBindShader
     bool currentWireframe = false;
 
     for (const auto& cmd : m_Commands) {
-        SlipShader* shader = nullptr;
-        std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
-        if (sType == "default") sType = "Lit";
-        shader = SlipShaderLibrary::Get(sType);
-        if (!shader) shader = SlipShaderLibrary::Get("Lit");
+        SlipShader* shader = (cmd.material && cmd.material->shader) ? cmd.material->shader : nullptr;
+        if (!shader) {
+            std::string sType = (cmd.material && !std::string(cmd.material->shaderType).empty()) ? cmd.material->shaderType : "Lit";
+            if (sType == "default") sType = "Lit";
+            shader = SlipShaderLibrary::Get(sType);
+            if (!shader) shader = SlipShaderLibrary::Get("Lit");
+        }
         if (!shader) continue;
```

---

## 8. Verification Strategy & Test Alignment

1. **Unit & Integration Tests**:
   - `test_shader_graph.cpp` Feature F12:
     - `Test_T1_F12_01_AssignGraphShaderPointerToMaterial`: tests `LoadShaderGraph(path)` assigning valid `mat.shader`.
     - `Test_T1_F12_02_MaterialShaderTypeGraphTagging`: tests `shaderGraphPath` persistence.
     - `Test_T1_F12_04_RecompileGraphUpdatesHandleSafely`: tests re-calling `LoadShaderGraph` updates program ID.
     - `Test_T1_F12_05_MaterialDestructionCleansResources`: tests scoped material clean exit.
   - `test_shader_graph.cpp` Feature F14:
     - `Test_T1_F14_01_OverrideFloatParameter`
     - `Test_T1_F14_02_OverrideVectorColorParameter`
     - `Test_T1_F14_03_OverrideTextureParameter`
     - `Test_T1_F14_04_ParameterOverridesBindToGLSL`
   - `test_shader_graph.cpp` Feature F15:
     - `Test_T1_F15_01_MatFileWritesShaderGraphPath`
     - `Test_T1_F15_02_MatFileWritesParameterOverrides`
     - `Test_T1_F15_03_MatFileDeserializationRestoresOverrides`
     - `Test_T1_F15_04_LoadMatRecompilesShaderGraph`
     - `Test_T1_F15_05_MissingGraphFallbackToDefaultLit`
   - Scenario 5: `Test_T4_05_FullRoundTripMaterialWorkflow` (end-to-end authoring -> `.shadergraph` -> material assignment -> parameter overrides -> `.mat` save -> `.mat` load -> uniform binding & execution).
2. **Build Target**:
   - `cmake --build build --config Debug --target SlipEngine`
   - `cmake --build build --config Debug --target SlipTestShaderGraph`
3. **Runtime Viewport Verification**:
   - Load sample animated procedural graph asset on a mesh entity.
   - Modify public properties in material inspector; verify viewport mesh immediately reflects changes.
