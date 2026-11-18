# Survey Report: SlipEngine Editor Architecture & UI System

**Explorer ID**: explorer_survey_2 (Editor UI System Explorer)  
**Date**: 2026-11-08  
**Scope**: In-depth survey of SlipEngine's editor architecture, UI libraries, panel lifecycle, Inspector panel, material property editing, and canvas/node editor blueprints for Shader Graph integration.  

---

## 1. Executive Summary

SlipEngine features a modular, well-architected desktop and mobile editor built on **Dear ImGui version 1.92.9b (docking branch)** with SDL2 and OpenGL (OpenGL 3.3 Core on Windows MSVC, OpenGL ES 3.0 on Android arm64).

Key takeaways for the Shader Graph implementation:
1. **No External Node Editor Library**: There is currently no third-party node library (`imnodes`, `imgui-node-editor`, or `ImGuizmo`) in `vcpkg` or the codebase.
2. **Proven Canonical Node Graph Implementation**: The editor already contains a battle-tested node graph canvas in `src/editor/panels/AnimatorGraphPanel.cpp` implemented directly using Dear ImGui's `ImDrawList`. It features infinite panning, mouse-centered zooming, background grid rendering, node dragging, interactive connection wire creation, and context menus.
3. **Clean Panel Lifecycle**: All editor windows inherit from `EditorPanel` (`include/SlipEngine/editor/EditorPanel.h`) and are managed uniformly by `SlipEditor` (`src/editor/SlipEditor.cpp`). Adding `ShaderGraphPanel` follows an established, straightforward pattern.
4. **Mature Material & Inspector Workflow**: Materials (`.mat` files and `SlipMaterial`) are edited in `InspectorPanel.cpp` via `RenderSlipMaterialPropertiesEditor`. The workflow supports Lit/Unlit switching, PBR properties, texture slots with drag-and-drop from `ContentBrowserPanel`, disk serialization, and automatic scene entity propagation.
5. **Technical Recommendation**: Implement the Node Graph Editor UI using a dedicated `ShaderGraphPanel` built on ImGui's `ImDrawList` following the design pattern of `AnimatorGraphPanel`. This preserves seamless cross-platform build stability across Windows MSVC and Android NDK arm64, avoids ImGui 1.92 docking version incompatibilities, and provides full control over port styling, Bezier connection curves, and Unity/UE5-style aesthetics.

---

## 2. UI Framework & Vendor Ecosystem

### 2.1 Dependencies and Versions

| Library | Version / Feature | Location | Usage in SlipEngine |
| :--- | :--- | :--- | :--- |
| **Dear ImGui** | **1.92.9b** (`docking-experimental`, `opengl3-binding`) | `vcpkg.json`, `build/vcpkg_installed/x64-windows/include/imgui.h` | Entire editor UI, docking, widgets, styling, and drawlists |
| **SDL2** | 2.x | `vcpkg.json`, `SDL2::SDL2`, `SDL2main` | Window creation, OpenGL context, input & touch events |
| **GLAD / OpenGL** | Desktop OpenGL 3.3 Core | `CMakeLists.txt`, `gladLoadGLLoader` | Desktop rendering backend |
| **OpenGL ES 3.0 / EGL** | GLES 3.0 | `android/`, NDK libraries | Mobile TBDR runtime and mobile editor backend |
| **ImGuiColorTextEdit** | Embedded vendor | `src/editor/texteditor/TextEditor.h/cpp` | ScriptEditorPanel Lua syntax highlighting & text editing |
| **ImNodes / imgui-node-editor** | **None** | *Not present* | No external node editor library in vcpkg or source tree |
| **ImGuizmo** | **None** | *Not present* | Viewport gizmos are handled directly |

### 2.2 Cross-Platform Target Considerations

SlipEngine is actively configured to build for both:
- **Windows (Desktop)**: MSVC x64, `/utf-8 /bigobj /FS`, OpenGL 3.3 Core.
- **Android**: NDK arm64-v8a, OpenGL ES 3.0 TBDR, touch gestures (`SDL_FINGERDOWN`, `SDL_FINGERMOTION`, pinch zoom, long-press simulation).

*Implication for Shader Graph*: Any UI solution must compile cleanly on both Windows MSVC and Android Clang NDK without adding complex external dependencies that fail vcpkg cross-compilation or break against ImGui 1.92 internal APIs.

---

## 3. Editor Architecture & Panel Lifecycle

### 3.1 The `EditorPanel` Base Class

Every panel in SlipEngine inherits from `EditorPanel`:
- Header: `include/SlipEngine/editor/EditorPanel.h`
- Pure virtual render loop: `virtual void OnImGuiRender() = 0;`
- Context injection:
  - `virtual void SetContext(SlipLevel* level);`
  - `virtual void SetSelectedEntity(SlipEntity* entity);`
  - `virtual void SetSelectedAsset(const std::string& assetPath);`
- Visibility: `bool m_IsOpen = true; void SetOpen(bool open);`

### 3.2 Panel Registration in `SlipEditor`

In `src/editor/SlipEditor.cpp`:
1. `SlipEditor::SlipEditor()` instantiates all modular panels into `std::vector<std::unique_ptr<EditorPanel>> m_Panels`:
   ```cpp
   m_Panels.push_back(std::move(hierarchy));
   m_Panels.push_back(std::make_unique<InspectorPanel>());
   m_Panels.push_back(std::make_unique<ConsolePanel>());
   m_Panels.push_back(std::make_unique<ViewportPanel>(onEntitySelected));
   m_Panels.push_back(std::move(contentBrowser));
   m_Panels.push_back(std::make_unique<ScriptEditorPanel>());
   m_Panels.push_back(std::make_unique<PrefabEditorPanel>());
   m_Panels.push_back(std::make_unique<ProjectSettingsPanel>());
   m_Panels.push_back(std::make_unique<AnimationEditorPanel>());
   m_Panels.push_back(std::make_unique<AnimatorGraphPanel>());
   m_Panels.push_back(std::make_unique<PixelArtEditorPanel>());
   // Integration Point:
   // m_Panels.push_back(std::make_unique<ShaderGraphPanel>());
   ```

2. Main Loop Execution:
   - `SlipEditor::BeginFrame()`:
     - Handles input (`SlipInput::BeginFrame()`).
     - Prepares ImGui frame (`ImGui_ImplOpenGL3_NewFrame()`, `ImGui_ImplSDL2_NewFrame()`, `ImGui::NewFrame()`).
     - Invokes `RenderDockSpace()`: calls `ImGui::DockSpaceOverViewport(dockspaceId, viewport);`.
     - Invokes `RenderToolbar()`: creates top menu bar (`Archivo`, `Editar`, `GameObject`, `Proyecto`, `Ventanas`).
   - `SlipEditor::RenderPanels()`:
     - Iterates through `m_Panels`: calls `panel->OnImGuiRender()` if open.
     - Calls modal dialogs (`SlipFileDialog::Instance().Render()`).
   - `SlipEditor::EndFrame()`:
     - Calls `ImGui::Render()`, `ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData())`.

3. Docking Layout & Persistence:
   - `SlipEditor::ResetDefaultLayout()` uses ImGui's internal `ImGui::DockBuilder*` APIs:
     - Splitting root dock node into Left (Hierarchy), Right (Inspector), Bottom (Console, Content Browser, Script Editor), and Center (Viewport).
     - Individual window docking: `ImGui::DockBuilderDockWindow("Window Title", dockLocation);`.

4. Opening Dedicated Assets:
   - Helper methods on `SlipEditor` find the panel via `GetPanel<T>()` and trigger open:
     - `OpenScript(const std::string& path, int line)`
     - `OpenPrefab(const std::string& path)`
     - `OpenAnimatorController(const std::string& path)`
     - Pattern for Shader Graph: `void OpenShaderGraph(const std::string& graphPath);`.

---

## 4. Existing Node Canvas Architecture (`AnimatorGraphPanel`)

SlipEngine already contains an in-house node canvas in `include/SlipEngine/editor/panels/AnimatorGraphPanel.h` and `src/editor/panels/AnimatorGraphPanel.cpp`. This provides the exact architectural foundation required for `ShaderGraphPanel`.

### 4.1 Canvas Layout Pattern
`AnimatorGraphPanel` divides its window into three columns:
```
+---------------------------------------------------------------------------------------+
| Toolbar: [Guardar] [Abrir] [+ Nuevo] [Tipo: Lit/Unlit] [Compilar] [Centrar Vista]     |
+-------------------+-----------------------------------------------+-------------------+
| 1. Parámetros /   | 2. Grafo Interactivo (ImDrawList Canvas)      | 3. Inspector de   |
|    Blackboard     |    - Cuadrícula con Zoom & Pan                |    Nodo / Preview |
|    - Float        |    - Nodos con Puertos Tipados                |    - Propiedades  |
|    - Color/Vec3   |    - Cables Cúbicos Bézier                    |    - Vista Previa |
|    - Texturas     |    - Menú contextual con buscador             |                   |
+-------------------+-----------------------------------------------+-------------------+
```

### 4.2 Canvas Math & Interactions

1. **Child Window Isolation**:
   ```cpp
   ImGui::BeginChild("GraphCanvasArea", ImVec2(canvasWidth, 0), true,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
   ImDrawList* drawList = ImGui::GetWindowDrawList();
   ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
   ImVec2 canvasSize = ImGui::GetContentRegionAvail();
   ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);
   drawList->PushClipRect(canvasP0, canvasP1, true);
   ```

2. **Coordinate Transformations**:
   - Graph coordinates $(X_g, Y_g)$ to Screen coordinates $(X_s, Y_s)$:
     $$X_s = canvasP0.x + offset.x + X_g \times zoom$$
     $$Y_s = canvasP0.y + offset.y + Y_g \times zoom$$
   - Screen coordinates $(X_s, Y_s)$ to Graph coordinates $(X_g, Y_g)$:
     $$X_g = \frac{X_s - canvasP0.x - offset.x}{zoom}$$
     $$Y_g = \frac{Y_s - canvasP0.y - offset.y}{zoom}$$

3. **Panning & Zooming**:
   - **Panning**: Right-click drag or Middle-click drag:
     ```cpp
     if (isCanvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                             ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
         m_CanvasOffset.x += io.MouseDelta.x;
         m_CanvasOffset.y += io.MouseDelta.y;
     }
     ```
   - **Focal Zoom**: Mouse wheel zooms toward the mouse cursor position:
     ```cpp
     if (isCanvasHovered && io.MouseWheel != 0.0f) {
         float prevZoom = m_Zoom;
         m_Zoom = std::clamp(m_Zoom + io.MouseWheel * 0.08f, 0.4f, 2.0f);
         ImVec2 mousePosCanvas = ImVec2((io.MousePos.x - canvasP0.x - m_CanvasOffset.x) / prevZoom,
                                        (io.MousePos.y - canvasP0.y - m_CanvasOffset.y) / prevZoom);
         m_CanvasOffset.x = io.MousePos.x - canvasP0.x - mousePosCanvas.x * m_Zoom;
         m_CanvasOffset.y = io.MousePos.y - canvasP0.y - mousePosCanvas.y * m_Zoom;
     }
     ```

4. **Grid Drawing**:
   - Renders filled background `IM_COL32(28, 30, 36, 255)` followed by modulo lines:
     ```cpp
     float gridSize = 64.0f * m_Zoom;
     for (float x = std::fmod(m_CanvasOffset.x, gridSize); x < canvasSize.x; x += gridSize)
         drawList->AddLine(ImVec2(canvasP0.x + x, canvasP0.y), ImVec2(canvasP0.x + x, canvasP1.y), IM_COL32(40, 44, 52, 200), 1.0f);
     for (float y = std::fmod(m_CanvasOffset.y, gridSize); y < canvasSize.y; y += gridSize)
         drawList->AddLine(ImVec2(canvasP0.x, canvasP0.y + y), ImVec2(canvasP1.x, canvasP0.y + y), IM_COL32(40, 44, 52, 200), 1.0f);
     ```

5. **Node Dragging**:
   - When a node is dragged with the left mouse button:
     ```cpp
     node.posX += io.MouseDelta.x / m_Zoom;
     node.posY += io.MouseDelta.y / m_Zoom;
     ```

6. **Interactive Wire / Link Creation**:
   - Click on an output port sets `m_IsMakingTransition = true` and `m_TransitionSourceStateIdx = i`.
   - While dragging, draws dynamic wire to `io.MousePos`.
   - Releasing/clicking on destination port adds connection.
   - Cancel via Right-Click or ESC key.

---

## 5. Inspector Panel & Material Property Editing

### 5.1 Structure of `InspectorPanel.cpp`

The Inspector operates in two main modes:
1. **Asset Inspection Mode** (`!m_SelectedAssetPath.empty()`):
   - Model assets (`.fbx`, `.obj`): Import scale, bake transforms, skeleton/animation clips, meta file serialization.
   - Texture assets (`.png`, `.jpg`): Filter mode, wrap mode, mipmaps, preview.
   - **Material assets (`.mat`)**:
     - Automatically loads `s_InspectedMat = SlipMaterial(m_SelectedAssetPath);`.
     - Invokes `RenderSlipMaterialPropertiesEditor(s_InspectedMat, "InspectedMatAsset", assetsDir);`.
     - Provides `[ Guardar Material (.mat) ]` button: calls `s_InspectedMat.Save(m_SelectedAssetPath)`.
     - Updates all active scene entities using this material across all submesh slots.
     - Provides `[ Asignar Material al Objeto Seleccionado ]` button.

2. **Entity Component Inspection Mode** (`m_SelectedEntity != nullptr`):
   - Iterates over entity components.
   - For `MeshComponent` and `SkinnedMeshComponent`:
     - Displays mesh selector combo + drag & drop target.
     - Multi-material slot support (`GetMaterialSlotCount()`).
     - Combo selector of project `.mat` files + drag & drop target from `ContentBrowserPanel`.
     - `[+] Nuevo` button to create a new material file in `assets/Materials/`.
     - Inline material properties editor: `RenderSlipMaterialPropertiesEditor(*mc->material, ...)`.
     - `[ Guardar Material (.mat) ]` button.

### 5.2 `RenderSlipMaterialPropertiesEditor`

Defined at line 79 of `src/editor/panels/InspectorPanel.cpp`:
- **Shader Mode Combo**: Currently selects between `"Lit"` and `"Unlit"`.
- **Unlit Mode**:
  - `ColorEdit3("Color Base", &mat.color.r);`
  - Texture slot: `RenderTextureSlotControl("Textura Base (Albedo)", ...)`
  - Alpha clip & cutoff slider.
- **Lit Mode**:
  - Albedo color (`ColorEdit3`), Roughness slider (`0.0..1.0`), Metallic slider (`0.0..1.0`), AO slider (`0.0..1.0`), Emissive color & intensity.
  - PBR texture slots: Albedo, Normal (with Normal scale), Roughness (with invert checkbox), Metallic, AO, Emissive, Opacity.
  - Alpha clip cutoff, Double-sided checkbox.
- **`RenderTextureSlotControl`**:
  - Shows combo of project textures (`.png,.jpg,.jpeg,.tga,.bmp`).
  - Supports `CONTENT_BROWSER_ITEM` drag & drop.
  - Displays tooltip preview and clear button `[X]`.
  - Automatically triggers `mat.reloadTextures()`.

### 5.3 `SlipMaterial` Implementation Analysis

Defined in `include/SlipEngine/render/SlipMaterial.h` and `src/render/SlipMaterial.cpp`:
- Properties stored:
  - `char shaderType[32]`: currently `"Lit"` or `"Unlit"`.
  - Colors and scalars: `color`, `roughness`, `metallic`, `ao`, `normalScale`, `emissiveColor`, `emissiveIntensity`, `alphaClip`, `alphaCutoff`.
  - Filepaths: `diffPath`, `normalPath`, `roughnessPath`, `metallicPath`, `aoPath`, `emissivePath`, `opacityPath`.
  - Texture pointers: `diffuseTexture`, `normalTexture`, `roughnessTexture`, etc.
  - Shader pointer: `SlipShader* shader`.
- Key methods:
  - `loadMatFromFile(path)`: parses key-value text file format (`key: value`).
  - `Save(savePath)`: serializes material to `.mat` text format.
  - `init()`: retrieves shader from `SlipShaderLibrary::LoadOrGet(shaderType)`.
  - `bindMaterialProperties(SlipShader* targetShader)`: sets uniforms (`u_Albedo`, `u_Roughness`, `u_Metallic`, `u_AO`, `u_EmissiveColor`, texture units `0, 1, 2, 4, 15, 16, 17`).

---

## 6. Architectural Blueprints for Shader Graph

### 6.1 Architecture Overview

```
                      +-----------------------------+
                      |     ShaderGraphAsset        |
                      |  (.shadergraph JSON/Text)   |
                      +--------------+--------------+
                                     |
              +----------------------+----------------------+
              |                                             |
              v                                             v
+---------------------------+                 +---------------------------+
|     ShaderGraphPanel      |                 |   ShaderGraphCompiler     |
|   (Interactive Node UI)   |                 | (GLSL Generator: Lit/Unlit|
+-------------+-------------+                 +-------------+-------------+
              |                                             |
              | User edits graph & parameters               | Compiles to GLSL vertex/frag
              v                                             v
+---------------------------+                 +---------------------------+
|    Blackboard / Exposed   |                 |    SlipShaderLibrary      |
|    Parameters Inspector   |                 |   (SlipShader Program)    |
+-------------+-------------+                 +-------------+-------------+
              |                                             |
              +----------------------+----------------------+
                                     |
                                     v
                      +-----------------------------+
                      |        SlipMaterial         |
                      |  - shaderGraphPath          |
                      |  - uniform values dictionary|
                      |  - texture slot assignments |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |       InspectorPanel        |
                      | (Dynamic Exposed Properties)|
                      +-----------------------------+
```

### 6.2 Component 1: `ShaderGraphPanel` Blueprint

**Header**: `include/SlipEngine/editor/panels/ShaderGraphPanel.h`  
**Source**: `src/editor/panels/ShaderGraphPanel.cpp`

```cpp
#pragma once
#include "SlipEngine/editor/EditorPanel.h"
#include <string>
#include <vector>
#include <memory>
#include <imgui.h>

enum class ShaderMaterialType {
    Lit = 0,
    Unlit = 1
};

enum class PinType {
    Float,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Texture2D,
    Matrix4
};

enum class PinKind {
    Input,
    Output
};

struct GraphPin {
    int id;
    int nodeId;
    std::string name;
    PinType type;
    PinKind kind;
    float defaultValueFloat = 0.0f;
    float defaultValueVec4[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct GraphLink {
    int id;
    int sourcePinId;
    int targetPinId;
};

struct GraphNode {
    int id;
    std::string title;
    std::string category;
    std::string opCode;       // e.g. "Add", "Multiply", "Sin", "Time", "DeltaTime", "MasterNode"
    ImVec2 pos;
    ImVec2 size;
    std::vector<GraphPin> inputs;
    std::vector<GraphPin> outputs;
    bool isMasterNode = false;
    std::string propertyReference; // If linked to an exposed blackboard property
};

struct ExposedProperty {
    std::string id;
    std::string name;             // Display name (e.g. "Base Color")
    std::string uniformName;      // GLSL uniform (e.g. "u_Graph_BaseColor")
    PinType type = PinType::Float;
    float floatValue = 1.0f;
    float vectorValue[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string texturePath = "";
};

class ShaderGraphPanel : public EditorPanel {
public:
    ShaderGraphPanel();
    ~ShaderGraphPanel() override;

    void OnImGuiRender() override;
    void OpenGraph(const std::string& filePath);
    void NewGraph(ShaderMaterialType type = ShaderMaterialType::Lit);
    bool SaveGraph(const std::string& filePath = "");
    bool CompileAndApply();

    static ShaderGraphPanel* GetInstance() { return s_Instance; }

private:
    void RenderToolbar();
    void RenderBlackboardSidebar();
    void RenderGraphCanvas();
    void RenderNodePropertiesSidebar();

    void DrawGrid(ImDrawList* drawList, const ImVec2& canvasP0, const ImVec2& canvasSize);
    void DrawNode(ImDrawList* drawList, const ImVec2& canvasP0, GraphNode& node);
    void DrawConnectionWire(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, PinType type);
    void ConfigureMasterNode(ShaderMaterialType type);

    static ImU32 GetPinColor(PinType type);

    std::string m_CurrentFilePath;
    bool m_IsDirty = false;
    ShaderMaterialType m_MaterialType = ShaderMaterialType::Lit;

    // Blackboard / Public Parameters
    std::vector<ExposedProperty> m_ExposedProperties;

    // Graph Elements
    std::vector<GraphNode> m_Nodes;
    std::vector<GraphLink> m_Links;
    int m_MasterNodeId = 1;
    int m_NextId = 100;

    // Canvas Navigation
    ImVec2 m_CanvasOffset = ImVec2(350.0f, 250.0f);
    float m_Zoom = 1.0f;

    // Interaction State
    int m_SelectedNodeId = -1;
    int m_SelectedLinkId = -1;
    int m_DraggingNodeId = -1;
    bool m_IsDraggingWire = false;
    int m_WireSourcePinId = -1;
    char m_NodeSearchFilter[64] = "";

    inline static ShaderGraphPanel* s_Instance = nullptr;
};
```

### 6.3 Master Node Specification & Material Type Switching (R1)

When the user selects **Lit** vs. **Unlit** via the top toolbar combo:
- `ConfigureMasterNode(m_MaterialType)` is executed:
  - **In Lit Mode**:
    The Master Node creates input ports:
    1. `Base Color (Vector3 / Color)` — Default: `(1.0, 1.0, 1.0)`
    2. `Normal (Vector3)` — Default: `(0.0, 0.0, 1.0)`
    3. `Metallic (Float)` — Default: `0.0`
    4. `Roughness (Float)` — Default: `0.5`
    5. `Ambient Occlusion (Float)` — Default: `1.0`
    6. `Emissive (Vector3 / Color)` — Default: `(0.0, 0.0, 0.0)`
    7. `Alpha (Float)` — Default: `1.0`
    8. `Alpha Clip Cutoff (Float)` — Default: `0.5`
  - **In Unlit Mode**:
    The Master Node creates input ports:
    1. `Color / Emissive (Vector3 / Color)` — Default: `(1.0, 1.0, 1.0)`
    2. `Alpha (Float)` — Default: `1.0`
    3. `Alpha Clip Cutoff (Float)` — Default: `0.5`
- Existing connections to preserved ports are maintained; invalid connections are safely pruned.

### 6.4 Node Palette & Library Specifications (R1)

The searchable creation popup (`Spacebar`, `Tab`, or Right-Click on canvas) exposes:

| Category | Node Name | Description / Inputs -> Outputs |
| :--- | :--- | :--- |
| **Math** | Add, Subtract, Multiply, Divide | 2 numeric inputs -> 1 result |
| **Math** | Power, Sqrt, Abs, Negate, Min, Max, Clamp, Lerp | Standard HLSL/GLSL scalar & vector math |
| **Math** | Step, Smoothstep | Threshold and smooth hermite interpolation |
| **Trigonometry** | Sin, Cos, Tan, Asin, Acos, Atan, Atan2 | Angle / wave calculations |
| **Vector** | Dot Product, Cross Product, Normalize, Length | Vector operations |
| **Vector** | Split | Vector4/Color input -> R, G, B, A (Float outputs) |
| **Vector** | Combine | R, G, B, A (Float inputs) -> Vector4 output |
| **Procedural** | Simple Noise (Perlin) | UV input, Scale -> procedural gradient noise |
| **Procedural** | Voronoi | UV input, Density, AngleOffset -> Voronoi cells & distance |
| **Procedural** | Checkerboard | UV input, ColorA, ColorB, Frequency -> 2D checker pattern |
| **Input / Time** | Time | Outputs `Time`, `SinTime`, `CosTime` |
| **Input / Time** | DeltaTime | Frame step for smooth procedural animations |
| **Input / Geometry** | UV Coordinates | Mesh texture coordinates (vec2) |
| **Input / Geometry** | World Position | Interpolated fragment world position (vec3) |
| **Input / Geometry** | World Normal | Fragment normal vector (vec3) |
| **Input / Geometry** | View Direction | CameraPos - WorldPos normalized vector (vec3) |
| **Input / Camera** | Camera Position | World-space camera position (vec3) |
| **Texture** | Texture Sample 2D | Texture asset or Property input, UV coord -> Color & Alpha |
| **Parameters** | Property Node | References an exposed blackboard parameter |
| **Master** | Master Surface Output | Final shading destination (Lit or Unlit) |

### 6.5 Bezier Cubic Connections & Port Color Coding

Connections are rendered using cubic Bezier curves matching UE5 / Unity aesthetics:
```cpp
ImVec2 p1 = startPos;
ImVec2 p4 = endPos;
float dx = std::abs(p4.x - p1.x) * 0.5f;
if (dx < 35.0f * m_Zoom) dx = 35.0f * m_Zoom;
ImVec2 p2 = ImVec2(p1.x + dx, p1.y);
ImVec2 p3 = ImVec2(p4.x - dx, p4.y);

drawList->AddBezierCubic(p1, p2, p3, p4, GetPinColor(pinType), 2.5f * m_Zoom);
```

**Port Colors by Data Type**:
- `Float`: Cyan `IM_COL32(80, 210, 255, 255)`
- `Vector2`: Pastel Green `IM_COL32(110, 230, 130, 255)`
- `Vector3` / `Color`: Bright Yellow-Orange `IM_COL32(255, 200, 60, 255)`
- `Vector4`: Deep Lavender `IM_COL32(180, 130, 255, 255)`
- `Texture2D`: Vibrant Magenta `IM_COL32(255, 80, 160, 255)`
- `Matrix4`: Crisp White `IM_COL32(240, 240, 245, 255)`

### 6.6 Inspector Integration for Public / Exposed Parameters (R3)

When inspecting a `SlipMaterial` that references a `.shadergraph`:
1. In `src/editor/panels/InspectorPanel.cpp` inside `RenderSlipMaterialPropertiesEditor`:
   ```cpp
   if (mat.UsesShaderGraph()) {
       ImGui::Separator();
       ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Parámetros Expuestos del Shader Graph:");
       
       for (auto& prop : mat.GetShaderGraphExposedProperties()) {
           ImGui::PushID(prop.name.c_str());
           if (prop.type == PinType::Float) {
               float val = mat.GetGraphFloat(prop.uniformName, prop.defaultValueFloat);
               if (ImGui::DragFloat(prop.name.c_str(), &val, 0.02f)) {
                   mat.SetGraphFloat(prop.uniformName, val);
               }
           } else if (prop.type == PinType::Color || prop.type == PinType::Vector3) {
               glm::vec3 col = mat.GetGraphVec3(prop.uniformName, prop.defaultValueVec3);
               if (ImGui::ColorEdit3(prop.name.c_str(), &col.r)) {
                   mat.SetGraphVec3(prop.uniformName, col);
               }
           } else if (prop.type == PinType::Vector4) {
               glm::vec4 v = mat.GetGraphVec4(prop.uniformName, prop.defaultValueVec4);
               if (ImGui::DragFloat4(prop.name.c_str(), &v.x, 0.02f)) {
                   mat.SetGraphVec4(prop.uniformName, v);
               }
           } else if (prop.type == PinType::Texture2D) {
               char texBuf[256];
               std::string currentPath = mat.GetGraphTexturePath(prop.uniformName);
               std::strncpy(texBuf, currentPath.c_str(), sizeof(texBuf) - 1);
               if (RenderTextureSlotControl(prop.name.c_str(), prop.uniformName.c_str(), texBuf, sizeof(texBuf), texFiles)) {
                   mat.SetGraphTexture(prop.uniformName, texBuf);
               }
           }
           ImGui::PopID();
       }
   }
   ```
2. Value modifications immediately update `mat`'s uniform dictionary and propagate in real time to all meshes rendered with this material in the Viewport.

### 6.7 Content Browser & Project Hub Integration (R2)

In `src/editor/panels/ContentBrowserPanel.cpp`:
1. **Double-Click**:
   ```cpp
   else if (entry.extension == ".shadergraph") {
       if (SlipEditor::GetInstance()) {
           SlipEditor::GetInstance()->OpenShaderGraph(entry.fullPath);
       }
   }
   ```
2. **Context Menu (`ContentBrowserContextMenu`)**:
   ```cpp
   if (ImGui::MenuItem("Nuevo Shader Graph (.shadergraph)")) {
       std::string baseName = "NuevoShaderGraph";
       std::string newPath = SlipFileSystem::CombinePath(m_CurrentDirectory, baseName + ".shadergraph");
       int counter = 1;
       while (SlipFileSystem::Exists(newPath)) {
           newPath = SlipFileSystem::CombinePath(m_CurrentDirectory, baseName + "_" + std::to_string(counter++) + ".shadergraph");
       }
       ShaderGraphPanel::CreateDefaultGraphAsset(newPath, ShaderMaterialType::Lit);
       RefreshCurrentDirectory();
       OpenRenameModal(newPath);
   }
   ```
3. **Item Context Menu**:
   ```cpp
   else if (entry.extension == ".shadergraph") {
       if (ImGui::MenuItem("Editar en Shader Graph")) {
           if (SlipEditor::GetInstance()) {
               SlipEditor::GetInstance()->OpenShaderGraph(entry.fullPath);
           }
       }
       ImGui::Separator();
   }
   ```

---

## 7. Comparison: Custom ImGui Canvas vs External Library

| Criterion | Custom ImGui DrawList Canvas | External Library (`imnodes` / `imgui-node-editor`) |
| :--- | :--- | :--- |
| **Existing Codebase Synergy** | **100% matches `AnimatorGraphPanel`** | Disjoint API, conflicting paradigms |
| **Android NDK Compatibility** | **Guaranteed** (already proven in production) | Risky (potential build errors, touch input bugs) |
| **ImGui 1.92 Docking Safety** | **Immune** (only uses public standard ImGui APIs) | High risk (often breaks on ImGui internal struct changes) |
| **Dependency Footprint** | **0 new dependencies** | New thirdparty library or submodule |
| **Custom Styling (Unity/UE5)** | **Full control** over colors, Bezier arcs, badges | Restricted by library theme abstractions |
| **Maintenance & Portability** | Easy to maintain by existing engine devs | Third-party upkeep required |

**Verdict**: The custom ImGui DrawList approach is strongly recommended.

---

## 8. Concrete Implementation Checklist

### Step 1: Core Graph Data & Serialization
- [ ] Create `include/SlipEngine/render/SlipShaderGraph.h` and `src/render/SlipShaderGraph.cpp`
- [ ] Implement node, pin, link, and exposed parameter data structures
- [ ] Implement JSON / text serialization and deserialization (`SaveToFile`, `LoadFromFile`)

### Step 2: GLSL Code Generator & Material Integration
- [ ] Implement `SlipShaderGraphCompiler` generating GLSL for both Lit (PBR) and Unlit pipelines
- [ ] Update `SlipMaterial` to reference `.shadergraph` assets, invoke graph compilation, and bind dynamic exposed uniforms
- [ ] Add caching into `SlipShaderLibrary`

### Step 3: `ShaderGraphPanel` UI
- [ ] Create `include/SlipEngine/editor/panels/ShaderGraphPanel.h` and `src/editor/panels/ShaderGraphPanel.cpp`
- [ ] Register panel in `SlipEditor.h` and `SlipEditor.cpp` (`m_Panels`, `OpenShaderGraph`, menu bar)
- [ ] Implement 3-column studio UI: Blackboard sidebar, interactive canvas, node inspector
- [ ] Implement zoom, pan, grid, node dragging, and cubic Bezier wire creation
- [ ] Implement material type toggle (Lit vs Unlit) dynamically configuring the Master Node
- [ ] Implement searchable node palette with full math, procedural, time, and texture operations

### Step 4: Inspector & Asset Pipeline Integration
- [ ] Integrate exposed parameters into `InspectorPanel.cpp` (`RenderSlipMaterialPropertiesEditor`)
- [ ] Integrate `.shadergraph` double-click and right-click asset creation in `ContentBrowserPanel.cpp`

### Step 5: Verification & Samples
- [ ] Write automated tests in `tests/test_shader_graph.cpp` covering serialization, code generation (Lit/Unlit), and material assignment
- [ ] Build a sample animated procedural material (using `deltaTime`, Perlin noise, and trigonometry)

---

*End of Survey Report*
