# Project: SlipEngine Production-Ready Shader Graph System

## Architecture
The Shader Graph system introduces a node-based visual material authoring environment, runtime GLSL code generation, asset serialization, and engine material integration for SlipEngine.

```
                    â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                    â”‚     Shader Graph Asset       â”‚
                    â”‚      (.shadergraph)          â”‚
                    â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                                   â”‚
              â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”´â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
              â–¼                                         â–¼
  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                 â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
  â”‚   ShaderGraphPanel    â”‚                 â”‚   SlipMaterial /      â”‚
  â”‚ (Dear ImGui Canvas)   â”‚                 â”‚   ShaderGraphCompiler â”‚
  â”‚ - Node creation/drag  â”‚                 â”‚ - DAG topological sortâ”‚
  â”‚ - Pin linking/delete  â”‚                 â”‚ - GLSL Lit/Unlit gen  â”‚
  â”‚ - Lit/Unlit selector  â”‚                 â”‚ - Dynamic compilation â”‚
  â”‚ - Public parameters   â”‚                 â”‚ - Uniform binding     â”‚
  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜                 â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
              â”‚                                         â”‚
              â–¼                                         â–¼
  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                 â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
  â”‚    InspectorPanel     â”‚                 â”‚   OpenGL Pipeline     â”‚
  â”‚ - Material inspector  â”‚                 â”‚ - PBR Lit (GGX/CSM)   â”‚
  â”‚ - Dynamic exposed     â”‚â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¶â”‚ - Unlit direct color  â”‚
  â”‚   property editing    â”‚                 â”‚ - Time / DeltaTime    â”‚
  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜                 â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

### Module Boundaries
> Nota (M3): los tipos del shader graph usan el namespace `SlipShaderGraph` (renombrado desde
> `SlipEngine`) porque la clase global `SlipEngine` (`include/SlipEngine/SlipEngine.h`) colisiona
> con un namespace del mismo nombre en MSVC (C2757/C2869) cuando ambos aparecen en la misma unidad
> de compilación.
1. **Model & Serialization (`SlipShaderGraph::ShaderGraph`)**:
   - `include/SlipEngine/render/shadergraph/ShaderGraph.h`
   - `include/SlipEngine/render/shadergraph/ShaderNode.h`
   - `src/render/shadergraph/ShaderGraph.cpp`
   - `src/render/shadergraph/ShaderGraphSerializer.cpp`
   - Asset format: `.shadergraph` with companion `.shadergraph.meta`.
2. **Compiler & Material Integration (`SlipShaderGraph::ShaderGraphCompiler`)**:
   - `include/SlipEngine/render/shadergraph/ShaderGraphCompiler.h`
   - `src/render/shadergraph/ShaderGraphCompiler.cpp`
   - `include/SlipEngine/render/SlipMaterial.h` & `src/render/SlipMaterial.cpp`
   - `include/SlipEngine/core/SlipGlobals.h` (`GetTime()`)
3. **Editor UI Panel & Inspector (`SlipEditor`)**:
   - `include/SlipEngine/editor/panels/ShaderGraphPanel.h`
   - `src/editor/panels/ShaderGraphPanel.cpp`
   - `src/editor/panels/InspectorPanel.cpp` (`RenderSlipMaterialPropertiesEditor`)
   - `src/editor/panels/ContentBrowserPanel.cpp`
   - `src/editor/SlipEditor.cpp`
4. **Automated Testing Suite**:
   - `tests/test_shader_graph.cpp` (Target `SlipTestShaderGraph` in CMake)
   - Sample assets in `assets/materials/` (`sample_animated_procedural.shadergraph`, `sample_lit_pbr.shadergraph`, etc.)

---

## Code Layout
- `include/SlipEngine/render/shadergraph/` â€” Public headers for Shader Graph model, nodes, serializer, and compiler.
- `src/render/shadergraph/` â€” Core implementations of graph data structures, node registry, serializer, and GLSL code generation.
- `include/SlipEngine/editor/panels/ShaderGraphPanel.h` â€” Editor panel header for node graph canvas.
- `src/editor/panels/ShaderGraphPanel.cpp` â€” ImGui-based node graph editor implementation.
- `tests/test_shader_graph.cpp` â€” Comprehensive automated testing harness for shader graph serialization, GLSL generation, and compilation.
- `assets/materials/` â€” Sample shader graph assets and demonstration materials.

---

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| F01 | Graph Data Structures & Node Representation | Node, port, connection, pin types, and graph topology container | M1 | R1, R2 |
| F02 | Comprehensive Node Library (Math, Logic, Vector) | Arithmetic (+, -, *, /), trig (sin, cos, tan), vector math (dot, cross, norm, length, dist) | M1 | R1 |
| F03 | Procedural & Utility Nodes | Clamp, lerp, min, max, abs, step, smoothstep, remap, procedural noise (Perlin/Simplex/Voronoi) | M1 | R1 |
| F04 | Time & Animation Nodes | Time, DeltaTime, SinTime for dynamic animated materials | M1 | R1 |
| F05 | Camera & Transform Nodes | Camera position, view matrix, projection matrix, world pos, world normal | M1 | R1 |
| F06 | Texture Sampling Nodes | Texture2D sampler, UV coordinates, Tiling and Offset | M1 | R1 |
| F07 | Master Output Node (Lit vs. Unlit) | Master node switching between PBR surface properties (albedo, normal, roughness, metallic, emissive, AO, alpha) and Unlit color | M1 | R1 |
| F08 | Asset File Serialization & Deserialization | Dedicated `.shadergraph` file format save/load round-trip with lossless topology | M1 | R2 |
| F09 | Engine Asset Pipeline Registration | `.shadergraph` and `.meta` GUID integration into `SlipFileSystem` and `SlipModelExtract` | M1 | R2 |
| F10 | GLSL Code Generation (Topological Sort) | Emit valid, optimized GLSL vertex and fragment shaders for Lit and Unlit graphs | M2 | R1, R2 |
| F11 | Global Time & DeltaTime Uniform Binding | Expose `GetTime()` in `SlipGlobals` and bind `u_Time`, `u_DeltaTime` in render pipeline | M2 | R1 |
| F12 | SlipMaterial Integration | Support referencing `.shadergraph` assets in `SlipMaterial`, dynamic shader compilation via `SlipShader::CreateFromSource` | M2 | R2 |
| F13 | Public / Exposed Property Declaration | Support declaring public variables (float, vec2, vec3, vec4/color, texture) in graph | M1, M2 | R3 |
| F14 | Material Parameter Uniform Overrides & Binding | Bind public parameter overrides to GLSL uniforms and custom texture units (20+) in `bindMaterialProperties` | M2 | R3 |
| F15 | Material File (.mat) Graph Persistence | Save and load `.shadergraph` reference and parameter override values in `.mat` files | M2 | R2, R3 |
| F16 | Interactive Node Canvas UI | Dear ImGui `ImDrawList` canvas with pan, zoom, grid, draggable nodes, and pin linking | M3 | R1 |
| F17 | Node Palette & Context Menu | Right-click context menu and node search palette to spawn nodes | M3 | R1 |
| F18 | Material Type Selector in Editor UI | Switch Lit/Unlit from canvas toolbar, dynamically updating master node inputs | M3 | R1 |
| F19 | Exposed Parameters Blackboard UI | Left sidebar in `ShaderGraphPanel` to add, delete, rename, and set defaults for public parameters | M3 | R3 |
| F20 | Editor Menu & Asset Double-Click Integration | Open `.shadergraph` via ContentBrowser double-click and Ventanas menu toggle | M3 | R1, R2 |
| F21 | Material Inspector Parameter Editing | Dynamic controls (sliders, color pickers, texture slots) in `InspectorPanel.cpp` for graph materials | M3 | R3 |
| F22 | Real-time Viewport Material Update | Modifying exposed properties or graph immediately reflects in editor viewport rendering | M3 | R3 |
| F23 | Automated Test Suite (Serialization & CodeGen) | Standalone test executable `SlipTestShaderGraph` verifying save/load, GLSL gen, and GL compilation | M4 / Test Track | R4 |
| F24 | Sample Animated Procedural Shader Graph Asset | Functional sample graph using deltaTime and procedural math rendering in engine | M4 / Test Track | R4 |

---

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| M1 | Core Model, Node Library & Serialization | Features F01-F09, F13. Headers and core implementations of `ShaderGraph`, node types, `.shadergraph` serializer, asset type registration. | none | DONE (Verified: 2 Reviewers APPROVE, 2 Challengers APPROVE, Forensic Auditor CLEAN) |
| M2 | GLSL Compiler & SlipMaterial Runtime Integration | Features F10-F12, F14, F15. Topological GLSL generator (Lit & Unlit), `SlipGlobals` time binding, `SlipMaterial` graph execution and uniform binding. | M1 | PLANNED |
| M3 | Shader Graph Editor Panel & Inspector Integration | Features F16-F22. `ShaderGraphPanel` ImGui canvas, blackboard sidebar, Lit/Unlit selector, `ContentBrowserPanel` & `InspectorPanel` integration. | M1, M2 | PLANNED |
| M4 | Final Integration, E2E Verification & Hardening | Features F23, F24. Pass 100% of E2E test suite (Tiers 1-4), sample animated graph verification, and Tier 5 adversarial hardening. | M1, M2, M3, Test Track | PLANNED |

---

## Interface Contracts

### 1. `ShaderGraph` â†” `ShaderGraphSerializer`
```cpp
namespace SlipShaderGraph {
    enum class ShaderGraphType { Lit, Unlit };
    enum class PinType { Float, Vec2, Vec3, Vec4, Texture2D };

    struct ShaderProperty {
        std::string id;
        std::string name;
        PinType type;
        glm::vec4 defaultValue;
        std::string defaultTexturePath;
    };

    struct ShaderPort {
        std::string id;
        std::string name;
        PinType type;
        bool isOutput;
        glm::vec4 defaultValue;
    };

    class ShaderNode {
    public:
        std::string id;
        std::string typeName;
        std::string title;
        float posX = 0.0f, posY = 0.0f;
        std::vector<ShaderPort> inputs;
        std::vector<ShaderPort> outputs;
        // Node-specific constants/parameters
        virtual ~ShaderNode() = default;
    };

    struct ShaderLink {
        std::string id;
        std::string fromNodeId;
        std::string fromPortId;
        std::string toNodeId;
        std::string toPortId;
    };

    class ShaderGraph {
    public:
        ShaderGraphType materialType = ShaderGraphType::Lit;
        std::vector<std::unique_ptr<ShaderNode>> nodes;
        std::vector<ShaderLink> links;
        std::vector<ShaderProperty> properties;
        
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);
    };
}
```

### 2. `ShaderGraphCompiler` â†” `SlipMaterial`
```cpp
namespace SlipShaderGraph {
    struct GeneratedShaderCode {
        std::string vertexSource;
        std::string fragmentSource;
        bool success = false;
        std::string errorMessage;
    };

    class ShaderGraphCompiler {
    public:
        static GeneratedShaderCode Compile(const ShaderGraph& graph);
    };
}
```
In `SlipMaterial`:
```cpp
// In SlipMaterial.h
std::string shaderGraphPath;
std::unordered_map<std::string, float> graphFloatParams;
std::unordered_map<std::string, glm::vec4> graphVectorParams;
std::unordered_map<std::string, std::string> graphTextureParams;

bool LoadShaderGraph(const std::string& path);
void SetGraphFloat(const std::string& name, float value);
void SetGraphVector(const std::string& name, const glm::vec4& value);
void SetGraphTexture(const std::string& name, const std::string& texturePath);
```

### 3. `ShaderGraphPanel` â†” `SlipEditor`
```cpp
class ShaderGraphPanel : public EditorPanel {
public:
    ShaderGraphPanel();
    ~ShaderGraphPanel() override;
    void OnImGuiRender() override;
    void OpenGraph(const std::string& filepath);
    void NewGraph();
    void SaveGraph(const std::string& filepath);
};
```
