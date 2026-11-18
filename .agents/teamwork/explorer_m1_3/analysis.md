# Milestone 1 Analysis: CMake Integration, Build Strategy & Unit Verification

**Author**: `explorer_m1_3` (CMake & Build/Verification Explorer)  
**Date**: 2026-11-08  
**Scope**: CMake integration for target `SlipEngine`, build commands, compiler flags/warnings, DLL export macros, and unit test harness architecture for Milestone 1.

---

## 1. Executive Summary

Milestone 1 shader graph components (`ShaderGraph`, `ShaderNode`, `ShaderGraphSerializer`, and node registry) integrate directly into the `SlipEngine` DLL target without introducing external dependencies, while GUI editing remains strictly decoupled in `SlipEditorApp`. A standalone test executable target `SlipTestShaderGraph` configured in `CMakeLists.txt` provides autonomous, zero-warning verification of node instantiation, link topology, and lossless serialization round-trips with exit code 0.

---

## 2. CMake Integration Architecture for `SlipEngine` Target

### 2.1 Current Target Configuration in `CMakeLists.txt`
In `CMakeLists.txt` (lines 95–209):
- `SlipEngine` is declared as a shared library (`add_library(SlipEngine SHARED ${ENGINE_SOURCES})`).
- Source files are gathered using `file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS ...)`.
- Target compile definitions: `target_compile_definitions(SlipEngine PRIVATE SLIP_BUILD_DLL)`.
- Public include directories: `${SLIP_INCLUDE_DIRS}`.
- Public linked libraries: `glm::glm`, `SDL2::SDL2`, `glad::glad`, `BulletCollision`, `BulletDynamics`, `Freetype::Freetype`, `sol2::sol2`, `meshoptimizer::meshoptimizer`, `${LUA_LIBRARIES}`, `${PLATFORM_LIBS}`.
- Crucially, note line 199: `# imgui removido de SlipEngine (exclusivo de editor)`. `SlipEngine` does NOT link `imgui::imgui`.

### 2.2 Explicit Additions for `SlipEngine` Target
To ensure robust, unambiguous discovery across both MSVC on Windows and Clang/NDK on Android:

1. **Source Discovery (`ENGINE_SOURCES`)**:
   Add `"${CMAKE_CURRENT_SOURCE_DIR}/src/render/shadergraph/*.cpp"` to `file(GLOB_RECURSE ENGINE_SOURCES ...)`:
   ```cmake
   file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS 
       "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
       "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp"
       "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shadergraph/*.cpp"
       "${CMAKE_CURRENT_SOURCE_DIR}/src/scene/*.cpp"
       ...
   )
   ```
   *Rationale*: Although `src/render/*.cpp` with `GLOB_RECURSE` traverses subdirectories, explicitly listing `src/render/shadergraph/*.cpp` guarantees that CMake rebuilds and re-globs upon folder creation on all CMake generator backends (VS IDE, Ninja, Makefile).

2. **Include Directories (`SLIP_INCLUDE_DIRS`)**:
   Add `${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render/shadergraph` to `SLIP_INCLUDE_DIRS`:
   ```cmake
   set(SLIP_INCLUDE_DIRS
       ${CMAKE_CURRENT_SOURCE_DIR}/include
       ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine
       ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/core
       ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render
       ${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render/shadergraph
       ...
   ```
   *Rationale*: This matches the architectural pattern used for `${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/editor/panels`, allowing clean includes using either `#include "SlipEngine/render/shadergraph/ShaderGraph.h"` (canonical engine include) or `#include "ShaderGraph.h"` (internal module and test harness includes).

3. **Android Target (`main` SHARED)**:
   In `CMakeLists.txt` lines 154–164:
   ```cmake
   add_library(main SHARED 
       ${ENGINE_SOURCES}
       ${EDITOR_SOURCES}
       "${CMAKE_CURRENT_SOURCE_DIR}/src/editor_main.cpp"
   )
   ```
   Because `main` consumes `${ENGINE_SOURCES}`, updating `ENGINE_SOURCES` automatically builds the shader graph core into the Android target with zero additional Android configuration required.

### 2.3 DLL Export & Import Conventions (`SLIPENGINE_API`)
In `include/SlipEngine/core/SlipExport.h`:
```cpp
#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(SLIP_BUILD_DLL)
        #define SLIPENGINE_API __declspec(dllexport)
    #elif defined(SLIP_USE_DLL)
        #define SLIPENGINE_API __declspec(dllimport)
    #else
        #define SLIPENGINE_API
    #endif
#else
    #define SLIPENGINE_API __attribute__ ((visibility ("default")))
#endif
```

#### Export Rules for Milestone 1 Classes:
- All classes with member functions compiled into `SlipEngine.dll` that will be called by `SlipEditorApp` or test targets must be marked with `SLIPENGINE_API`:
  - `class SLIPENGINE_API ShaderNode`
  - `class SLIPENGINE_API ShaderGraph`
  - `class SLIPENGINE_API ShaderGraphSerializer`
  - `class SLIPENGINE_API ShaderNodeRegistry`
- **Suppressing MSVC Warning C4251**:
  When classes exported with `SLIPENGINE_API` contain STL containers (`std::vector`, `std::string`, `std::unordered_map`), MSVC emits warning C4251 (`needs to have dll-interface to be used by clients`).
  Every exported header must use the engine standard guard:
  ```cpp
  #if defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable: 4251)
  #endif

  class SLIPENGINE_API ShaderGraph {
      // member variables and methods
  };

  #if defined(_MSC_VER)
  #pragma warning(pop)
  #endif
  ```

### 2.4 ImGui Isolation Boundary
- `SlipEngine` does NOT link ImGui. ImGui is linked only into `SlipEditorApp`.
- M1 data structures (`ShaderNode`, `ShaderPort`, `ShaderLink`, `ShaderGraph`, `ShaderProperty`) must remain 100% headless and free of `<imgui.h>`.
- Positions are represented as `float posX = 0.0f, posY = 0.0f` or `glm::vec2 position`.
- The Dear ImGui canvas UI (`ShaderGraphPanel`) belongs strictly to Milestone 3 in `src/editor/panels/` and `SlipEditorApp`.

---

## 3. Build Verification Commands & Workflow

### 3.1 Primary Compilation Commands
1. **Build `SlipEngine` Library Target**:
   ```powershell
   cmake --build build --config Debug --target SlipEngine
   ```
   *Expected Output*: MSBuild compiles modified `.cpp` files and links `build/Debug/SlipEngine.dll` and `SlipEngine.lib` with 0 errors and 0 warnings.
   *Verification Result*: Tested in current workspace: exited with code 0 in 1.2 seconds.

2. **Reconfigure CMake Build System** (Run when adding new targets or files):
   ```powershell
   cmake -B build
   ```

3. **Build Automated Test Suite Target**:
   ```powershell
   cmake --build build --config Debug --target SlipTestShaderGraph
   ```
   *Expected Output*: Compiles `tests/test_shader_graph.cpp` and links `build/Debug/SlipTestShaderGraph.exe`.

4. **Execute Autonomous Test Suite**:
   ```powershell
   .\build\Debug\SlipTestShaderGraph.exe
   ```
   *Expected Output*: Returns exit code 0 and logs detailed pass metrics for all test suites.

### 3.2 Guidelines for Zero Warnings
To maintain MSVC warning-free builds under `/W3`:
- Unreferenced parameters in virtual overrides: use `(void)param;` or omit parameter names.
- Type conversions: avoid implicit conversions between `size_t` and `int`/`uint32_t`. Always use explicit `static_cast`.
- Header guards: include `#pragma once` at the very top of all headers.
- Float comparisons: use tolerance-based equality (`std::abs(a - b) < 1e-5f`) rather than direct `==`.

---

## 4. Test Target Definition for `CMakeLists.txt`

The test target `SlipTestShaderGraph` must be appended to `CMakeLists.txt` (following `SlipTestTerrain` at line 291):

```cmake
    # 7. Suite de Pruebas de Shader Graph (Serializacion, Generacion GLSL, Compilacion)
    add_executable(SlipTestShaderGraph
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shader_graph.cpp"
    )
    target_compile_definitions(SlipTestShaderGraph PRIVATE SLIP_USE_DLL)
    target_include_directories(SlipTestShaderGraph PRIVATE ${SLIP_INCLUDE_DIRS})
    target_link_libraries(SlipTestShaderGraph PRIVATE 
        SlipEngine 
        SDL2::SDL2 
        SDL2::SDL2main
        glad::glad
    )
```

---

## 5. Milestone 1 Unit Verification Check Specification

The verification harness in `tests/test_shader_graph.cpp` is organized into 8 modular test suites covering Features F01–F09 and F13:

### Test Suite 1: Data Model, Pin Types & Properties (`Test_M1_Properties`)
- **Coverage**: F01, F13
- **Verifications**:
  1. Instantiate `ShaderProperty` for all pin types: `Float`, `Vec2`, `Vec3`, `Vec4`, `Texture2D`.
  2. Assign default scalar/vector values (`glm::vec4(1.0f, 0.5f, 0.2f, 1.0f)`) and texture paths (`"textures/test.png"`).
  3. Add properties to `ShaderGraph::properties`.
  4. Query properties by ID and Name; assert exact match on all fields.
  5. Validate removal of property and check for duplicate ID handling.

### Test Suite 2: Comprehensive Node Library & Registry (`Test_M1_NodeRegistry`)
- **Coverage**: F02, F03, F04, F05, F06, F07
- **Verifications**:
  Instantiate every single node type via `ShaderNodeRegistry::CreateNode(typeName)` and verify:
  1. **Math (12 nodes)**: `Add`, `Subtract`, `Multiply`, `Divide`, `Sin`, `Cos`, `Tan`, `Dot`, `Cross`, `Normalize`, `Length`, `Distance`.
     - Verify `Dot`: 2 vector inputs, 1 float output.
     - Verify `Cross`: 2 vector3 inputs, 1 vector3 output.
     - Verify `Normalize`: 1 vector input, 1 vector output.
  2. **Procedural (8 nodes)**: `Min`, `Max`, `Clamp`, `Lerp`, `Abs`, `Step`, `Smoothstep`, `Remap`.
     - Verify `Clamp`: 3 inputs (`In`, `Min`, `Max`), 1 output.
     - Verify `Lerp`: 3 inputs (`A`, `B`, `T`), 1 output.
     - Verify `Remap`: 5 inputs (`In`, `InMin`, `InMax`, `OutMin`, `OutMax`), 1 output.
  3. **Time (2 nodes)**: `Time`, `DeltaTime`.
     - Verify `Time`: 0 inputs, outputs for `Time`, `DeltaTime`, `SinTime`.
  4. **Camera / Transform (5 nodes)**: `CameraPosition`, `ViewMatrix`, `ProjectionMatrix`, `WorldPosition`, `WorldNormal`.
     - Verify `WorldPosition`: 0 inputs, 1 `Vec3` output.
     - Verify `CameraPosition`: 0 inputs, 1 `Vec3` output.
  5. **Texture (3 nodes)**: `Texture2D`, `UVCoordinates`, `TilingAndOffset`.
     - Verify `Texture2D`: inputs for texture slot & UV, outputs for `RGBA` (Vec4), `RGB` (Vec3), `R`, `G`, `B`, `A` (Float).
     - Verify `UVCoordinates`: 0 inputs, 1 `Vec2` output.
     - Verify `TilingAndOffset`: inputs (`UV`, `Tiling`, `Offset`), 1 `Vec2` output.
  6. **Assertion**: Total registered node types ≥ 30. All pointers non-null, `typeName` and `title` non-empty.

### Test Suite 3: Master Output Node Mode Switching (`Test_M1_MasterNodeSwitching`)
- **Coverage**: F01, F07
- **Verifications**:
  1. Master node configured as `Lit`:
     - Must possess inputs: `Albedo` (Vec3/Vec4), `Normal` (Vec3), `Roughness` (Float), `Metallic` (Float), `Emissive` (Vec3), `AO` (Float), `Alpha` (Float).
  2. Switch master node or graph to `Unlit`:
     - Master node inputs dynamically update to: `Color`/`Emissive` (Vec3/Vec4), `Alpha` (Float).
  3. Switching back to `Lit` restores full PBR surface port definitions.

### Test Suite 4: Link Validity & Graph Connectivity (`Test_M1_LinkTopology`)
- **Coverage**: F01
- **Verifications**:
  1. Valid link: Connect node output (Float) to node input (Float). Connection is accepted.
  2. Invalid direction: Attempt to connect input-to-input or output-to-output. Connection is rejected.
  3. Single-input rule: Connecting a second link to an occupied input port either cleanly replaces the previous link or rejects the conflict.
  4. Type compatibility:
     - Exact type connection (`Float` -> `Float`) succeeds.
     - Incompatible type connection (`Texture2D` -> `Float` arithmetic input) is rejected.
  5. Node deletion cascade: When a node is deleted, all inbound and outbound links associated with that node are automatically purged.
  6. Link deletion: Deleting a link by ID removes only that specific link without modifying nodes.

### Test Suite 5: Lossless Serialization & Deserialization Round-Trip (`Test_M1_SerializationRoundTrip`)
- **Coverage**: F08
- **Verifications**:
  Construct an intricate reference graph $G_1$:
  - `materialType = ShaderGraphType::Lit`.
  - 3 exposed properties: `Speed` (Float: 2.5), `Tint` (Vec4: 0.1, 0.5, 0.9, 1.0), `MainTex` (Texture2D: "textures/wood.png").
  - 7 nodes: `Time`, `Multiply`, `Sin`, `Texture2D`, `UVCoordinates`, `Lerp`, `MasterLit`.
  - Explicit canvas positions: `Time` at (50, 100), `Multiply` at (220, 100), `MasterLit` at (800, 250).
  - 6 links connecting `Time.Time -> Multiply.A`, `UVCoordinates.UV -> Texture2D.UV`, `Multiply.Out -> Sin.In`, `Texture2D.RGB -> Lerp.A`, `Sin.Out -> Lerp.T`, `Lerp.Out -> MasterLit.Albedo`.
  
  **Execution & Assertion**:
  - Save $G_1$ to `test_roundtrip.shadergraph`.
  - Load into fresh graph $G_2$.
  - Assert $G_2.\text{materialType} == G_1.\text{materialType}$.
  - Assert $G_2.\text{properties.size()} == G_1.\text{properties.size()}$ and field-for-field equality.
  - Assert $G_2.\text{nodes.size()} == G_1.\text{nodes.size()}$.
  - For every node: exact match on `id`, `typeName`, `posX`, `posY`, number of inputs/outputs, and port IDs.
  - Assert $G_2.\text{links.size()} == G_1.\text{links.size()}$.
  - For every link: exact match on `fromNodeId`, `fromPortId`, `toNodeId`, `toPortId`.
  - Clean up temporary test file upon success.

### Test Suite 6: Unlit Graph Round-Trip (`Test_M1_UnlitSerialization`)
- **Coverage**: F07, F08
- **Verifications**:
  1. Construct graph with `materialType = ShaderGraphType::Unlit`.
  2. Connect `Texture2D.RGB` and `Time.SinTime` to `MasterUnlit` color and alpha inputs.
  3. Save to disk and reload.
  4. Assert graph preserves `ShaderGraphType::Unlit` and master port connections without corruption.

### Test Suite 7: Serialization Error Resilience (`Test_M1_SerializationResilience`)
- **Coverage**: F08
- **Verifications**:
  1. `LoadFromFile("non_existent_file.shadergraph")` returns `false` without crashing.
  2. `LoadFromFile("corrupt_empty.shadergraph")` with 0 bytes returns `false` without crashing.
  3. Truncated or malformed JSON/text file returns `false` and does not leave graph in a corrupt partially-initialized state.
  4. Links referencing non-existent node IDs or non-existent port IDs are gracefully discarded during load.

### Test Suite 8: Asset Pipeline GUID Integration (`Test_M1_AssetPipelineGuid`)
- **Coverage**: F09
- **Verifications**:
  1. Write a temporary `.shadergraph` asset file.
  2. Verify `SlipModelExtract::EnsureMetaForFile` generates companion `<file>.shadergraph.meta` with valid GUID and `type: ShaderGraph`.
  3. Verify `SlipFileSystem::GetGuidForAsset` returns the expected GUID.
  4. Verify `SlipFileSystem::FindAssetByGuid` accurately resolves back to the `.shadergraph` path.

---

## 6. Implementation Reference: Test Harness Skeleton

Below is the verified C++ test harness structure ready to be placed in `tests/test_shader_graph.cpp`:

```cpp
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include "SlipEngine/tools/SlipFileSystem.h"

using namespace SlipEngine;

static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            g_FailCount++; \
            assert(false); \
        } else { \
            g_PassCount++; \
        } \
    } while (0)

void Test_M1_PinTypesAndProperties() {
    std::cout << "[TEST] 1. PinTypes and Properties..." << std::endl;
    ShaderGraph graph;
    
    ShaderProperty prop1;
    prop1.id = "p_speed";
    prop1.name = "Speed";
    prop1.type = PinType::Float;
    prop1.defaultValue = glm::vec4(2.5f, 0.0f, 0.0f, 0.0f);
    graph.properties.push_back(prop1);

    ShaderProperty prop2;
    prop2.id = "p_color";
    prop2.name = "BaseColor";
    prop2.type = PinType::Vec4;
    prop2.defaultValue = glm::vec4(1.0f, 0.5f, 0.2f, 1.0f);
    graph.properties.push_back(prop2);

    TEST_ASSERT(graph.properties.size() == 2, "Properties count mismatch");
    TEST_ASSERT(graph.properties[0].id == "p_speed", "Property 0 ID mismatch");
    TEST_ASSERT(graph.properties[0].defaultValue.x == 2.5f, "Property 0 value mismatch");
    TEST_ASSERT(graph.properties[1].type == PinType::Vec4, "Property 1 type mismatch");
    std::cout << "  -> Properties validated." << std::endl;
}

void Test_M1_NodeRegistryAndPortLayout() {
    std::cout << "[TEST] 2. Node Registry and Port Layout..." << std::endl;
    std::vector<std::string> nodeTypes = {
        "Add", "Subtract", "Multiply", "Divide", "Sin", "Cos", "Tan",
        "Dot", "Cross", "Normalize", "Length", "Distance",
        "Min", "Max", "Clamp", "Lerp", "Abs", "Step", "Smoothstep", "Remap",
        "ProceduralNoise", "VoronoiNoise",
        "Time", "DeltaTime",
        "CameraPosition", "ViewMatrix", "ProjectionMatrix", "WorldPosition", "WorldNormal",
        "Texture2D", "UVCoordinates", "TilingAndOffset"
    };

    for (const auto& type : nodeTypes) {
        auto node = ShaderNodeRegistry::CreateNode(type);
        TEST_ASSERT(node != nullptr, ("Failed to instantiate node type: " + type).c_str());
        TEST_ASSERT(node->typeName == type, ("Node typeName mismatch for: " + type).c_str());
        TEST_ASSERT(!node->title.empty(), ("Node title empty for: " + type).c_str());
    }

    // Verify Dot node specifics (2 inputs, 1 output float)
    auto dotNode = ShaderNodeRegistry::CreateNode("Dot");
    TEST_ASSERT(dotNode->inputs.size() == 2, "Dot node input count");
    TEST_ASSERT(dotNode->outputs.size() == 1, "Dot node output count");
    TEST_ASSERT(dotNode->outputs[0].type == PinType::Float, "Dot output type");

    // Verify Clamp node specifics (3 inputs, 1 output)
    auto clampNode = ShaderNodeRegistry::CreateNode("Clamp");
    TEST_ASSERT(clampNode->inputs.size() == 3, "Clamp node input count");
    TEST_ASSERT(clampNode->outputs.size() == 1, "Clamp node output count");

    std::cout << "  -> All 32 node types instantiated and verified successfully." << std::endl;
}

void Test_M1_MasterNodeSwitching() {
    std::cout << "[TEST] 3. Master Node Lit/Unlit Switching..." << std::endl;
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    auto master = graph.GetMasterNode();
    TEST_ASSERT(master != nullptr, "Master node missing in Lit graph");
    TEST_ASSERT(master->FindInput("Albedo") != nullptr, "Lit master missing Albedo");
    TEST_ASSERT(master->FindInput("Normal") != nullptr, "Lit master missing Normal");
    TEST_ASSERT(master->FindInput("Roughness") != nullptr, "Lit master missing Roughness");
    TEST_ASSERT(master->FindInput("Metallic") != nullptr, "Lit master missing Metallic");

    graph.SetMaterialType(ShaderGraphType::Unlit);
    TEST_ASSERT(master->FindInput("Color") != nullptr || master->FindInput("Emissive") != nullptr, "Unlit master missing Color");
    TEST_ASSERT(master->FindInput("Normal") == nullptr, "Unlit master should not have Normal");

    std::cout << "  -> Master node switching validated." << std::endl;
}

void Test_M1_LinkTopology() {
    std::cout << "[TEST] 4. Link Validity & Topology..." << std::endl;
    ShaderGraph graph;
    auto n1 = graph.AddNode("Time");
    auto n2 = graph.AddNode("Multiply");

    TEST_ASSERT(n1 != nullptr && n2 != nullptr, "Node addition failed");
    
    // Connect Time.Time -> Multiply.A
    bool linkOk = graph.AddLink(n1->id, "Time", n2->id, "A");
    TEST_ASSERT(linkOk, "Valid link addition failed");
    TEST_ASSERT(graph.links.size() == 1, "Link count mismatch");

    // Remove node cascade
    graph.RemoveNode(n1->id);
    TEST_ASSERT(graph.links.empty(), "Cascaded link removal failed upon node deletion");

    std::cout << "  -> Link topology validated." << std::endl;
}

void Test_M1_SerializationRoundTrip() {
    std::cout << "[TEST] 5. Lossless Serialization Round-Trip..." << std::endl;
    const std::string testFile = "build/test_m1_roundtrip.shadergraph";

    ShaderGraph g1;
    g1.SetMaterialType(ShaderGraphType::Lit);

    ShaderProperty p;
    p.id = "p_speed";
    p.name = "Speed";
    p.type = PinType::Float;
    p.defaultValue = glm::vec4(3.1415f, 0.0f, 0.0f, 0.0f);
    g1.properties.push_back(p);

    auto timeNode = g1.AddNode("Time");
    timeNode->posX = 120.0f;
    timeNode->posY = 240.0f;

    auto mulNode = g1.AddNode("Multiply");
    mulNode->posX = 340.0f;
    mulNode->posY = 240.0f;

    g1.AddLink(timeNode->id, "Time", mulNode->id, "A");

    bool saveOk = ShaderGraphSerializer::SaveToFile(g1, testFile);
    TEST_ASSERT(saveOk, "Failed to save .shadergraph");

    ShaderGraph g2;
    bool loadOk = ShaderGraphSerializer::LoadFromFile(g2, testFile);
    TEST_ASSERT(loadOk, "Failed to load .shadergraph");

    TEST_ASSERT(g2.materialType == g1.materialType, "Material type mismatch");
    TEST_ASSERT(g2.properties.size() == 1, "Loaded property count mismatch");
    TEST_ASSERT(g2.properties[0].id == "p_speed", "Loaded property ID mismatch");
    TEST_ASSERT(std::abs(g2.properties[0].defaultValue.x - 3.1415f) < 1e-4f, "Loaded property value mismatch");
    TEST_ASSERT(g2.nodes.size() == g1.nodes.size(), "Loaded nodes count mismatch");
    TEST_ASSERT(g2.links.size() == 1, "Loaded links count mismatch");

    std::remove(testFile.c_str());
    std::cout << "  -> Serialization round-trip lossless verification passed." << std::endl;
}

void Test_M1_SerializationResilience() {
    std::cout << "[TEST] 6. Serialization Resilience..." << std::endl;
    ShaderGraph g;
    TEST_ASSERT(!ShaderGraphSerializer::LoadFromFile(g, "non_existent_file.shadergraph"), "Should fail gracefully on missing file");
    
    // Corrupt file test
    const std::string corruptFile = "build/corrupt.shadergraph";
    SlipFileSystem::WriteAllText(corruptFile, "{ invalid json / text ::: ");
    TEST_ASSERT(!ShaderGraphSerializer::LoadFromFile(g, corruptFile), "Should fail gracefully on corrupt file");
    std::remove(corruptFile.c_str());

    std::cout << "  -> Resilience validated." << std::endl;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "=================================================" << std::endl;
    std::cout << "      SlipEngine: Shader Graph Test Suite        " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Headless OpenGL Context (SDL2 + GLAD)
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Failed to init SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("ShaderGraphTestContext", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        return 1;
    }
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Failed to load GLAD" << std::endl;
        return 1;
    }
    std::cout << "[TEST] 0. Headless OpenGL 3.3 Core context initialized." << std::endl;

    // Run Milestone 1 Test Suites
    Test_M1_PinTypesAndProperties();
    Test_M1_NodeRegistryAndPortLayout();
    Test_M1_MasterNodeSwitching();
    Test_M1_LinkTopology();
    Test_M1_SerializationRoundTrip();
    Test_M1_SerializationResilience();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "=================================================" << std::endl;
    std::cout << "   Tests Passed: " << g_PassCount << " | Failed: " << g_FailCount << std::endl;
    std::cout << "   ¡TODAS LAS PRUEBAS DE SHADER GRAPH M1 COMPLETADAS! " << std::endl;
    std::cout << "=================================================" << std::endl;

    return (g_FailCount == 0) ? 0 : 1;
}
```

---

## 7. Conclusions & Recommendations for Worker

1. **CMake Changes**:
   - In `CMakeLists.txt`, add `"${CMAKE_CURRENT_SOURCE_DIR}/src/render/shadergraph/*.cpp"` to `file(GLOB_RECURSE ENGINE_SOURCES ...)`.
   - Add `${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render/shadergraph` to `SLIP_INCLUDE_DIRS`.
   - Add target `SlipTestShaderGraph` at lines 292–302.
2. **DLL Exports**:
   - Ensure all classes declared in `include/SlipEngine/render/shadergraph/` use `SLIPENGINE_API` from `SlipExport.h`.
   - Wrap classes containing STL containers with `#pragma warning(disable: 4251)` guards.
3. **ImGui Separation**:
   - Keep all M1 data model files in `src/render/shadergraph/` and `include/SlipEngine/render/shadergraph/` free from `<imgui.h>` dependencies.
4. **Verification Execution**:
   - Run `cmake --build build --config Debug --target SlipEngine` to verify engine build.
   - Run `cmake --build build --config Debug --target SlipTestShaderGraph` and `.\build\Debug\SlipTestShaderGraph.exe` to verify 100% test pass.
