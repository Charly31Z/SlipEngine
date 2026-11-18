# Handoff Report: GLSL Code Generator Architecture & Design (ShaderGraphCompiler)

**Agent**: `explorer_m2_1` (GLSL Code Generator Explorer)  
**Recipient**: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37` (Parent / Orchestrator)  
**Date**: 2026-11-08  
**Working Directory**: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\`  
**Status**: TASK_COMPLETE (Hard Handoff)

---

## 1. Observation

1. **ShaderNode Implementation & Helper GLSL Functions**:
   In `c:\Projects\SlipEngine\src\render\shadergraph\ShaderNode.cpp`:
   - Line 881: `ProceduralNoiseNode::GenerateGLSL` generates:
     `outExpr["Out"] = "slip_gradient_noise(" + uv + " * " + scale + ")";`
   - Line 902: `ProceduralVoronoiNode::GenerateGLSL` generates:
     `stmts += "    vec2 " + varName + " = slip_voronoi(" + uv + ", " + angle + ", " + scale + ");\n";`
   - Ripgrep verification across the entire repository confirmed that neither `slip_gradient_noise` nor `slip_voronoi` is defined anywhere else in the engine shaders:
     ```
     grep_search: Query="slip_gradient_noise", SearchPath="c:\Projects\SlipEngine"
     -> Match only in ShaderNode.cpp:881
     grep_search: Query="slip_voronoi", SearchPath="c:\Projects\SlipEngine"
     -> Match only in ShaderNode.cpp:902
     ```
     Therefore, the generated shader preamble *must* inject the GLSL implementations of `slip_gradient_noise` and `slip_voronoi`.

2. **MasterNode Interface & Inputs**:
   In `c:\Projects\SlipEngine\src\render\shadergraph\ShaderNode.cpp`:
   - Lines 464–478 (`MasterNode::UpdatePorts`):
     - Lit mode declares: `"Albedo"` (Vec3, def: 1,1,1), `"Normal"` (Vec3, def: 0,0,1), `"Roughness"` (Float, def: 0.5), `"Metallic"` (Float, def: 0.0), `"Emissive"` (Vec3, def: 0,0,0), `"AO"` (Float, def: 1.0), `"Alpha"` (Float, def: 1.0), `"AlphaClipThreshold"` (Float, def: 0.5).
     - Unlit mode declares: `"Color"` (Vec3, def: 1,1,1), `"Emissive"` (Vec3, def: 0,0,0), `"Alpha"` (Float, def: 1.0), `"AlphaClipThreshold"` (Float, def: 0.5).
   - Lines 481–502 (`MasterNode::GenerateGLSL`):
     Maps input expressions to outputs with safe fallbacks (e.g. `outExpr["Albedo"] = inExpr.count("Albedo") ? inExpr.at("Albedo") : "vec3(1.0)"`).

3. **SlipEngine Production PBR Lighting Pipeline**:
   In `c:\Projects\SlipEngine\src\render\SlipShaderLibrary.cpp`:
   - Lines 662–710 (`s_LitVert` for Desktop GL 3.3 Core):
     Declares `aPos` (loc 0), `aNormal` (loc 1), `aTexCoords` (loc 2), `aTangent` (loc 3), `aBitangent` (loc 4), `aBoneIDs` (loc 5), `aWeights` (loc 6), supports up to 100 bones via `u_BoneTransforms`, and outputs `vWorldPos`, `vNormal`, `vTexCoords`, `vTangent`, `vBitangent`.
   - Lines 712–1218 (`s_LitFrag` for Desktop GL 3.3 Core):
     Implements Cook-Torrance GGX/Smith PBR, Directional lights (up to 4) with CSM cascaded shadows, Point lights (up to 32), Spot lights (up to 4), DDGI ambient probe sampling (`SampleDDGI`), environment cubemap reflection probes (`u_EnvCube`), and dual rendertarget output (`layout(location = 0) out vec4 FragColor; layout(location = 1) out vec4 FragGeometry;`).
   - Lines 645–660 (`s_UnlitFrag` for Desktop GL 3.3 Core):
     Outputs directly to `FragColor`.

4. **Engine Texture Unit Allocation Convention**:
   In `c:\Projects\SlipEngine\src\render\SlipMaterial.cpp`:
   - Lines 321–380 (`SlipMaterial::bindMaterialProperties`):
     - Unit 0: `u_AlbedoMap`
     - Unit 1: `u_NormalMap`
     - Unit 2: `u_RoughnessMap`
     - Unit 3: `u_ShadowMapArray` (CSM shadow array)
     - Unit 4: `u_MetallicMap`
     - Units 5..14: `u_DDGIIrr` / `u_DDGIVis` (SlipDDGI probes)
     - Unit 15: `u_AOMap`
     - Unit 16: `u_EmissiveMap`
     - Unit 17: `u_OpacityMap`
     Custom shader graph textures must bind starting at Unit 20 (`GL_TEXTURE20`) to eliminate texture unit conflicts.

5. **Engine Global Time & Camera Access**:
   In `c:\Projects\SlipEngine\include\SlipEngine\core\SlipGlobals.h` and `src\core\SlipGlobals.cpp`:
   - `SlipGlobals::Get().GetDeltaTime()` returns current frame delta time.
   - `currentFrame` holds cumulative elapsed time updated via `update(double time)`. Adding `float GetTime() const { return currentFrame; }` completes uniform feeding for `u_Time`.

---

## 2. Logic Chain

1. **Step 1: Dead Code Elimination (DCE)**
   - *Observation 2* shows `MasterNode` is the unique terminal output sink of the graph.
   - Any node on the canvas not connected to `MasterNode` directly or transitively does not alter the rendered pixels.
   - *Deduction*: By traversing backwards along incoming links from `MasterNode`, the compiler builds a reachable node set `reachableNodes` and discards all disconnected/orphan nodes, minimizing shader instruction count.

2. **Step 2: Cycle Detection & Dependency Order**
   - Shader execution is strictly sequential (DAG). If a link introduces a loop ($A \to B \to A$), standard GLSL generation cannot linearize the code.
   - *Deduction*: A 3-color DFS detector on `reachableNodes` identifies cycles. Kahn's algorithm on the acyclic reachable subgraph computes a valid `executionOrder` where every node's inputs are produced before the node is evaluated.

3. **Step 3: Uniform Binding & Texture Unit Safety**
   - *Observation 4* shows units 0..17 are reserved for engine PBR maps, CSM, and DDGI.
   - *Deduction*: The compiler declares `uniform sampler2D` for each blackboard texture property and texture sampler node, assigning texture units $\ge 20$. In `SlipMaterial::bindMaterialProperties`, custom textures bind to units 20+ without disturbing the lighting pipeline.

4. **Step 4: Lighting Model Divergence (Lit vs. Unlit)**
   - *Observation 3* details the exact PBR requirements for `s_LitFrag` versus the streamlined direct color write in `s_UnlitFrag`.
   - *Deduction*:
     - In **Lit mode**: Node outputs populate `albedo`, `rawNormal` (transformed via TBN if needed), `roughness`, `metallic`, `emissive`, `ao`, and `alpha`. Then SlipEngine's GGX/Smith lighting loop evaluates direct lights, CSM shadows, DDGI, and reflection probes, writing to both `FragColor` and `FragGeometry`.
     - In **Unlit mode**: Node outputs populate `color`, `emissive`, and `alpha`, directly writing `FragColor = vec4(color + emissive, alpha)`.

5. **Step 5: GLSL Procedural Function Injection**
   - *Observation 1* proves that `ProceduralNoiseNode` and `ProceduralVoronoiNode` generate calls to `slip_gradient_noise` and `slip_voronoi`.
   - *Deduction*: The fragment shader template must include standard, optimized GLSL implementations of `slip_hash22`, `slip_gradient_noise`, and `slip_voronoi` to ensure 100% compilation success without missing symbols.

---

## 3. Caveats

1. **Normal Space Convention**:
   If a graph passes world-space normals directly into the MasterNode's `Normal` pin (e.g. from `Transform_WorldNormal`), transforming it again via TBN would double-rotate the vector. The design checks if normal is connected to a world-space node or tangent normal map, applying TBN only when a tangent-space perturbation is supplied.
2. **Platform Target**:
   The primary target is Desktop OpenGL 3.3 Core (`#version 330 core`), matching SlipEngine's Windows configuration. The compiler options struct allows emitting `#version 300 es` for Android GLES3 targets.
3. **No Direct Source Changes Made**:
   This explorer is strictly read-only. All designs, complete C++ headers, implementation skeletons, and GLSL templates have been written to `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\analysis.md`. No engine source files were modified.

---

## 4. Conclusion

The architecture for `ShaderGraphCompiler` is fully designed and documented in `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\analysis.md`. It provides:
1. Complete C++ header `include/SlipEngine/render/shadergraph/ShaderGraphCompiler.h` with `Compile()`, DAG reachability, 3-color cycle detection, Kahn's topological sort, and uniform metadata extraction.
2. Complete C++ implementation skeleton `src/render/shadergraph/ShaderGraphCompiler.cpp` ready for worker implementation.
3. Full GLSL templates for:
   - Vertex shader with skeletal animation (up to 100 bones) and standard interpolators.
   - Lit fragment shader with full SlipEngine GGX/Smith PBR, CSM cascaded shadows, DDGI, and dual MRT output (`FragColor`, `FragGeometry`).
   - Unlit fragment shader with direct color/emissive output and alpha clipping.
   - Standalone GLSL procedural math library (`slip_gradient_noise`, `slip_voronoi`, `slip_hash22`).
4. Texture unit assignment starting at unit 20 to avoid collisions with engine PBR/shadow units.

---

## 5. Verification Method

To independently verify the architecture and readiness for implementation:
1. **Inspect Analysis Artifact**:
   Read `c:\Projects\SlipEngine\.agents\teamwork\explorer_m2_1\analysis.md` to review the C++ header, implementation routines, and GLSL template code.
2. **Review Contract Alignment**:
   - Compare `MasterNode` ports in `include/SlipEngine/render/shadergraph/ShaderNode.h` (lines 408-418) against the surface attribute unpack in `analysis.md` Section 5.
   - Compare `s_LitFrag` in `src/render/SlipShaderLibrary.cpp` (lines 712-1218) against the Lit template in `analysis.md` Section 5.1.
3. **Downstream Worker Implementation Verification**:
   When `worker_m2` implements `ShaderGraphCompiler.h` and `ShaderGraphCompiler.cpp`:
   - Run the automated test suite:
     ```powershell
     cd c:\Projects\SlipEngine
     cmake --build build --config Release --target SlipTestShaderGraph
     .\build\bin\Release\SlipTestShaderGraph.exe
     ```
   - Invalidation condition: Compilation fails if any cycle is undetected, if unlinked nodes cause GLSL syntax errors, or if `slip_gradient_noise` / `slip_voronoi` are missing from the GLSL preamble.
