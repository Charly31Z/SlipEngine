# Handoff Report: Shader Graph Model & Node Library Architecture

**Agent**: `explorer_m1_1` (Shader Graph Model & Node Library Explorer)  
**Recipient**: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37` (Parent)  
**Date**: 2026-11-08  
**Working Directory**: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\`

---

## 1. Observation

1. **SlipEngine Source Layout and CMake Build Structure**:
   - `CMakeLists.txt` lines 95–100:
     ```cmake
     file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS 
         "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp"
         ...
     ```
     Any new `.cpp` files added under `src/render/shadergraph/*.cpp` are automatically gathered into `ENGINE_SOURCES` without modifying `CMakeLists.txt`.
   - `CMakeLists.txt` lines 112–116:
     `${CMAKE_CURRENT_SOURCE_DIR}/include/SlipEngine/render` is a public include directory.
   - `include/SlipEngine/core/SlipExport.h` defines `SLIPENGINE_API` (`__declspec(dllexport)` / `__declspec(dllimport)`).

2. **Existing Material & Shader Pipeline**:
   - `include/SlipEngine/render/SlipMaterial.h` lines 30–38:
     SlipEngine uses a PBR Lit model with `roughness`, `metallic`, `ao`, `normalScale`, `emissiveColor`, `emissiveIntensity`, and `shaderType` (`"Lit"` vs `"Unlit"`).
   - `src/render/SlipShaderLibrary.cpp` lines 180–553 and lines 712–800:
     SlipEngine's built-in Lit shaders implement GGX microfacet specular BRDF, Schlick Fresnel, Smith geometry masking/shadowing, cascaded shadow maps (`CalculateShadowCSM`), and ambient lighting. Unlit shaders output `u_Color * texture(u_MainTexture, vTexCoords)`.
   - `include/SlipEngine/render/SlipShader.h` line 31:
     `static SlipShader* CreateFromSource(const char* vertexSrc, const char* fragmentSrc);` enables runtime compilation of dynamically generated GLSL shaders.

3. **External Dependencies**:
   - `rapidjson` is present in `build/vcpkg_installed/x64-windows/include/rapidjson` and `vcpkg_installed/arm64-android/include/rapidjson`.
   - GLM headers are available at `<glm/glm.hpp>`.

4. **Peer Explorer Coordination**:
   - `explorer_m1_2` requires a clean way to deserialize nodes by string type name and restore port values/positions.
   - `explorer_m1_3` requires compilation and unit testing verification commands for Milestone 1.

---

## 2. Logic Chain

1. **Model Decoupling & Port Architecture**:
   - A shader graph requires directed dataflow where upstream output pins feed downstream input pins.
   - By creating `ShaderPort` with an `isOutput` flag, unique ID, `PinType`, and fallback `defaultValue` (`glm::vec4`), unlinked input ports cleanly evaluate to user-configurable constants without requiring dummy constant nodes.
   - By implementing `ArePinsCompatible(from, to)` and `ConvertGLSLExpression(from, to, expr)`, scalar promotion (`float` -> `vec3(val)`) and vector truncation/swizzling (`vec4` -> `val.xyz`) operate automatically, mirroring Unity and Unreal Engine behavior.

2. **Master Output Node Reconfigurability**:
   - In `PROJECT.md` Feature F07 and Acceptance Criteria, switching the graph material type between Lit and Unlit must adapt the master node's input pins.
   - By implementing `MasterNode::SetMaterialType(ShaderGraphType)`, the node dynamically updates its `inputs` vector:
     - **Lit**: 8 inputs (`Albedo`, `Normal`, `Roughness`, `Metallic`, `Emissive`, `AO`, `Alpha`, `AlphaClipThreshold`).
     - **Unlit**: 4 inputs (`Color`, `Emissive`, `Alpha`, `AlphaClipThreshold`).
   - Connections to ports present in both modes (e.g. `Emissive`, `Alpha`, `AlphaClipThreshold`) remain intact when toggling.

3. **Node Registry & Lossless Serialization**:
   - Deserialization in `explorer_m1_2` requires converting string `typeName` (e.g. `"Math_Add"`, `"Procedural_Voronoi"`) back into heap-allocated `ShaderNode` subclasses.
   - `ShaderNodeRegistry` provides `CreateNode(typeName)` via factory lambdas, completely decoupling the serializer from concrete node class definitions.
   - It also exposes `GetRegisteredNodes()` to feed Milestone 3's context menus and searchable node palette in `ShaderGraphPanel`.

4. **GLSL Code Generation Extensibility (Forward-Compatibility for M2)**:
   - Each `ShaderNode` implements `GenerateGLSL(inExpr, outExpr, stmts)`.
   - Procedural functions (`slip_gradient_noise`, `slip_voronoi`, `slip_remap`) are pure GLSL math implementations with zero texture dependencies, guaranteeing cross-platform parity on Desktop OpenGL Core 3.3 and Android OpenGL ES 3.0.

---

## 3. Caveats

1. **Cycles in Graph**: Node graphs must be directed acyclic graphs (DAGs) for fragment/vertex code generation. Cyclic graphs cannot be evaluated in GLSL without recursion (which is unsupported in GLSL 300 es / 330 core). A cycle validation helper `ShaderGraph::HasCycles()` is designed in `ShaderGraph.h`.
2. **Multiple Connections to One Input Port**: Input ports accept at most one incoming link. If a new link connects to an already-connected input port, the previous link must be disconnected.
3. **Texture Sampler Node vs Property**: A `TextureSamplerNode` can either take a texture sampler handle from an exposed `PropertyNode`, or use its own internal `defaultTexturePath` property when unlinked.

---

## 4. Conclusion

The architecture designed in `analysis.md` completely fulfills all requirements for Milestone 1 (F01–F07, F13):
1. **`include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`**: Specifies `PinType`, `ShaderGraphType`, `ShaderProperty`, `ShaderPort`, `ShaderLink`, and compatibility/conversion utilities.
2. **`include/SlipEngine/render/shadergraph/ShaderNode.h` and `src/render/shadergraph/ShaderNode.cpp`**: Specifies the polymorphic `ShaderNode` base, `ShaderNodeRegistry`, and 35+ production nodes (Math, Procedural, Time, Camera/Transform, Texture, Inputs, Master Output).
3. **`include/SlipEngine/render/shadergraph/ShaderGraph.h` and `src/render/shadergraph/ShaderGraph.cpp`**: Specifies the container managing graph topology, Blackboard public parameters, link validations, and file persistence.

All designs are non-destructive to existing SlipEngine code and ready for implementation.

---

## 5. Verification Method

1. **Inspect Analysis and Skeletons**:
   - Check `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\analysis.md` for complete class headers, type definitions, and node implementations.
2. **Compilation Verification (When Worker Implements)**:
   - Command:
     ```powershell
     cmake --build build --config Debug --target SlipEngine
     ```
   - Expected result: Clean build with 0 errors and 0 warnings.
3. **Unit Test Verification**:
   - Verify `ShaderNodeRegistry::Get().CreateNode("Math_Add")` returns a valid `MathAddNode`.
   - Verify `MasterNode` port count is 8 when `Lit` and 4 when `Unlit`.
   - Verify `ArePinsCompatible(PinType::Float, PinType::Vec3)` returns `true` and `ArePinsCompatible(PinType::Texture2D, PinType::Float)` returns `false`.
   - Verify link additions and removals on `ShaderGraph`.
