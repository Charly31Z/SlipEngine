# Architecture & Design Analysis: ShaderGraphCompiler

**Author**: `explorer_m2_1` (GLSL Code Generator Explorer)  
**Target Module**: `SlipEngine::ShaderGraphCompiler`  
**Target Files**:
- `include/SlipEngine/render/shadergraph/ShaderGraphCompiler.h`
- `src/render/shadergraph/ShaderGraphCompiler.cpp`  
**Date**: 2026-11-08  
**Milestone**: Milestone 2 (GLSL Code Generator & Material Runtime Integration)

---

## 1. Executive Summary & Architecture Overview

The `ShaderGraphCompiler` transforms a high-level visual node graph (`SlipEngine::ShaderGraph`) into valid, compilable OpenGL shading language (GLSL) code for both **Vertex** and **Fragment** shaders. It supports both **Lit** (physically based rendering via SlipEngine's Cook-Torrance GGX/Smith pipeline with CSM cascaded shadows, point/spot lights, DDGI, and reflection probes) and **Unlit** (direct color and emissive output) material models.

```
                    ┌──────────────────────────────┐
                    │      SlipEngine::ShaderGraph │
                    │ - Nodes (Master, Math, etc.) │
                    │ - Links                      │
                    │ - Properties (Blackboard)    │
                    └──────────────┬───────────────┘
                                   │
                                   ▼
                    ┌──────────────────────────────┐
                    │     ShaderGraphCompiler      │
                    ├──────────────────────────────┤
                    │ 1. MasterNode Reachability   │ (Prunes dead nodes)
                    │ 2. Cycle Detection (3-Color) │ (Guarantees DAG)
                    │ 3. Topological Sort (Kahn)   │ (Strict dependency order)
                    │ 4. Uniform Emission          │ (Blackboard + Engine globals)
                    │ 5. Code Emission (Lit/Unlit) │ (GLSL 330 core / 300 es)
                    └──────────────┬───────────────┘
                                   │
                                   ▼
                    ┌──────────────────────────────┐
                    │     GeneratedShaderCode      │
                    │ - vertexSource               │
                    │ - fragmentSource             │
                    │ - textureBindings (Units 20+)│
                    │ - success / errorMessage     │
                    └──────────────┬───────────────┘
                                   │
                                   ▼
                    ┌──────────────────────────────┐
                    │ SlipShader::CreateFromSource │
                    │  (Dynamic OpenGL Program)    │
                    └──────────────────────────────┘
```

### Key Architectural Pillars
1. **Dead Code Elimination (DCE) via Backward Reachability**:
   The compiler starts strictly from `MasterNode` and follows incoming links backward. Orphan nodes placed on the canvas that do not contribute to the final surface are discarded, reducing shader complexity and eliminating dead code.
2. **Cycle-Safe Topological Sort**:
   A robust 3-color DFS cycle detector detects recursive links before topological sorting. Kahn's algorithm or reverse post-order DFS sorts reachable nodes into an exact linear execution sequence where every variable dependency is resolved before evaluation.
3. **Implicit Type Promotion and Conversion**:
   Using `ConvertGLSLExpression` and `ArePinsCompatible`, scalar floats are promoted to vectors (`vec2`, `vec3`, `vec4`) and swizzles are resolved safely when connecting ports of different dimensions.
4. **SlipEngine PBR Alignment**:
   In **Lit** mode, the compiler evaluates the surface graph into PBR material properties (`albedo`, `normal`, `roughness`, `metallic`, `emissive`, `ao`, `alpha`, `alphaClipThreshold`), then passes them directly into SlipEngine's production Cook-Torrance GGX/Smith lighting loop. In **Unlit** mode, it emits direct color and emissive evaluation.
5. **Texture Unit Strategy**:
   Engine PBR textures and lighting maps occupy texture units 0..19. Custom texture parameters declared in the shader graph are assigned dedicated texture units starting at index 20 (`GL_TEXTURE20`).

---

## 2. Complete C++ Class Designs

### 2.1 Public Header: `include/SlipEngine/render/shadergraph/ShaderGraphCompiler.h`

```cpp
#pragma once

#ifndef SLIP_SHADER_GRAPH_COMPILER_H
#define SLIP_SHADER_GRAPH_COMPILER_H

#include "SlipEngine/core/SlipExport.h"
#include "ShaderGraphTypes.h"
#include "ShaderGraph.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace SlipEngine {

    enum class GLSLTargetVersion {
        Desktop_GL330_Core,  // OpenGL 3.3 Core (Windows/Linux/macOS)
        Mobile_GLES300_ES    // OpenGL ES 3.0 (Android / iOS)
    };

    struct SLIPENGINE_API ShaderCompilerOptions {
        GLSLTargetVersion targetVersion = GLSLTargetVersion::Desktop_GL330_Core;
        bool enableSkinning = true;
        int maxBones = 100;
        bool enableDDGI = true;
        bool enableCascadedShadows = true;
        bool enableCloudShadows = true;
        bool enableEnvironmentCube = true;
        bool enableToneMapping = true;
        bool forceLinearOutput = false;
        bool debugComments = true;
    };

    struct SLIPENGINE_API CompiledTextureBinding {
        std::string propertyId;
        std::string uniformName;
        int textureUnit = 20;
        std::string defaultTexturePath;
    };

    struct SLIPENGINE_API GeneratedShaderCode {
        std::string vertexSource;
        std::string fragmentSource;
        bool success = false;
        std::string errorMessage;
        std::vector<CompiledTextureBinding> textureBindings;
        std::vector<std::string> exposedUniformNames;
        std::vector<std::string> executionOrder;
    };

    class SLIPENGINE_API ShaderGraphCompiler {
    public:
        // Main Compilation Entry Point
        static GeneratedShaderCode Compile(
            const ShaderGraph& graph,
            const ShaderCompilerOptions& options = ShaderCompilerOptions()
        );

        // DAG Graph Analysis Utilities
        static MasterNode* LocateMasterNode(const ShaderGraph& graph);
        static std::unordered_set<std::string> CollectReachableNodes(
            const ShaderGraph& graph,
            const MasterNode* master
        );
        static bool DetectCycle(
            const ShaderGraph& graph,
            const std::unordered_set<std::string>& reachableNodes,
            std::string* outCycleDiagnostic = nullptr
        );
        static std::vector<std::string> ComputeTopologicalOrder(
            const ShaderGraph& graph,
            const std::unordered_set<std::string>& reachableNodes,
            bool& outHasCycle
        );

        // Identifiers & Helper Formatting
        static std::string SanitizeIdentifier(const std::string& raw);
        static std::string FormatFloat(float value);

    private:
        // Shader Section Generators
        static std::string EmitVertexShader(
            const ShaderGraph& graph,
            const ShaderCompilerOptions& options,
            const std::unordered_set<std::string>& reachableNodes,
            const std::vector<std::string>& executionOrder
        );

        static std::string EmitFragmentShader(
            const ShaderGraph& graph,
            const ShaderCompilerOptions& options,
            const std::unordered_set<std::string>& reachableNodes,
            const std::vector<std::string>& executionOrder,
            std::vector<CompiledTextureBinding>& outTexBindings,
            std::vector<std::string>& outExposedUniforms
        );

        // GLSL Library Snippet Injectors
        static void AppendGLSLVersionHeader(std::ostringstream& ss, GLSLTargetVersion target);
        static void AppendProceduralMathLibrary(std::ostringstream& ss);
        static void AppendPBRFunctions(std::ostringstream& ss, const ShaderCompilerOptions& options);
        static void AppendLightingStructsAndUniforms(std::ostringstream& ss, const ShaderCompilerOptions& options);
    };

} // namespace SlipEngine

#endif // SLIP_SHADER_GRAPH_COMPILER_H
```

---

### 2.2 Core Implementation: `src/render/shadergraph/ShaderGraphCompiler.cpp`

```cpp
#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
#include <sstream>
#include <iomanip>
#include <queue>
#include <stack>
#include <algorithm>

namespace SlipEngine {

    std::string ShaderGraphCompiler::SanitizeIdentifier(const std::string& raw) {
        if (raw.empty()) return "_anon";
        std::string result;
        result.reserve(raw.size());
        for (char c : raw) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
                result.push_back(c);
            } else {
                result.push_back('_');
            }
        }
        if (result[0] >= '0' && result[0] <= '9') {
            result = "_" + result;
        }
        return result;
    }

    std::string ShaderGraphCompiler::FormatFloat(float value) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << value;
        return ss.str();
    }

    MasterNode* ShaderGraphCompiler::LocateMasterNode(const ShaderGraph& graph) {
        // Direct query
        MasterNode* master = graph.GetMasterNode();
        if (master) return master;

        // Fallback search by typeName or title
        for (const auto& node : graph.nodes) {
            if (node->typeName == "MasterOutput" || node->typeName == "MasterLit" || node->typeName == "MasterUnlit") {
                return dynamic_cast<MasterNode*>(node.get());
            }
        }
        for (const auto& node : graph.nodes) {
            if (node->title.rfind("Master", 0) == 0) {
                return dynamic_cast<MasterNode*>(node.get());
            }
        }
        return nullptr;
    }

    std::unordered_set<std::string> ShaderGraphCompiler::CollectReachableNodes(
        const ShaderGraph& graph,
        const MasterNode* master
    ) {
        std::unordered_set<std::string> reachable;
        if (!master) return reachable;

        // Build inverse adjacency map: toNodeId -> list of fromNodeId
        std::unordered_map<std::string, std::vector<std::string>> incomingMap;
        for (const auto& link : graph.links) {
            incomingMap[link.toNodeId].push_back(link.fromNodeId);
        }

        std::queue<std::string> queue;
        queue.push(master->id);
        reachable.insert(master->id);

        while (!queue.empty()) {
            std::string currId = queue.front();
            queue.pop();

            auto it = incomingMap.find(currId);
            if (it != incomingMap.end()) {
                for (const auto& upstreamId : it->second) {
                    if (reachable.find(upstreamId) == reachable.end()) {
                        reachable.insert(upstreamId);
                        queue.push(upstreamId);
                    }
                }
            }
        }

        return reachable;
    }

    bool ShaderGraphCompiler::DetectCycle(
        const ShaderGraph& graph,
        const std::unordered_set<std::string>& reachableNodes,
        std::string* outCycleDiagnostic
    ) {
        // 0 = unvisited (white), 1 = visiting (gray), 2 = visited (black)
        std::unordered_map<std::string, int> state;
        for (const auto& id : reachableNodes) {
            state[id] = 0;
        }

        // Build forward adjacency map restricted to reachable nodes
        std::unordered_map<std::string, std::vector<std::string>> forwardMap;
        for (const auto& link : graph.links) {
            if (reachableNodes.count(link.fromNodeId) && reachableNodes.count(link.toNodeId)) {
                forwardMap[link.fromNodeId].push_back(link.toNodeId);
            }
        }

        std::vector<std::string> path;

        std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
            state[u] = 1;
            path.push_back(u);

            auto it = forwardMap.find(u);
            if (it != forwardMap.end()) {
                for (const auto& v : it->second) {
                    if (state[v] == 1) {
                        if (outCycleDiagnostic) {
                            std::ostringstream ss;
                            ss << "Cycle detected in node chain: ";
                            for (const auto& p : path) ss << p << " -> ";
                            ss << v;
                            *outCycleDiagnostic = ss.str();
                        }
                        return true;
                    }
                    if (state[v] == 0) {
                        if (dfs(v)) return true;
                    }
                }
            }

            state[u] = 2;
            path.pop_back();
            return false;
        };

        for (const auto& id : reachableNodes) {
            if (state[id] == 0) {
                if (dfs(id)) return true;
            }
        }

        return false;
    }

    std::vector<std::string> ShaderGraphCompiler::ComputeTopologicalOrder(
        const ShaderGraph& graph,
        const std::unordered_set<std::string>& reachableNodes,
        bool& outHasCycle
    ) {
        outHasCycle = false;
        std::vector<std::string> order;
        if (reachableNodes.empty()) return order;

        std::unordered_map<std::string, int> inDegree;
        std::unordered_map<std::string, std::vector<std::string>> forwardAdj;

        for (const auto& id : reachableNodes) {
            inDegree[id] = 0;
        }

        for (const auto& link : graph.links) {
            if (reachableNodes.count(link.fromNodeId) && reachableNodes.count(link.toNodeId)) {
                forwardAdj[link.fromNodeId].push_back(link.toNodeId);
                inDegree[link.toNodeId]++;
            }
        }

        std::queue<std::string> q;
        for (const auto& pair : inDegree) {
            if (pair.second == 0) {
                q.push(pair.first);
            }
        }

        while (!q.empty()) {
            std::string u = q.front();
            q.pop();
            order.push_back(u);

            auto it = forwardAdj.find(u);
            if (it != forwardAdj.end()) {
                for (const auto& v : it->second) {
                    inDegree[v]--;
                    if (inDegree[v] == 0) {
                        q.push(v);
                    }
                }
            }
        }

        if (order.size() != reachableNodes.size()) {
            outHasCycle = true;
        }

        return order;
    }

    void ShaderGraphCompiler::AppendGLSLVersionHeader(std::ostringstream& ss, GLSLTargetVersion target) {
        if (target == GLSLTargetVersion::Desktop_GL330_Core) {
            ss << "#version 330 core\n";
        } else {
            ss << "#version 300 es\n"
               << "precision highp float;\n"
               << "precision mediump int;\n"
               << "precision mediump sampler2D;\n"
               << "precision highp sampler2DArray;\n"
               << "precision mediump samplerCube;\n";
        }
    }

    void ShaderGraphCompiler::AppendProceduralMathLibrary(std::ostringstream& ss) {
        ss << "// --- SlipEngine Shader Graph Procedural Noise & Voronoi Library ---\n"
           << "vec2 slip_hash22(vec2 p) {\n"
           << "    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));\n"
           << "    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);\n"
           << "}\n"
           << "float slip_gradient_noise(vec2 p) {\n"
           << "    vec2 pi = floor(p);\n"
           << "    vec2 pf = fract(p);\n"
           << "    vec2 w = pf * pf * (3.0 - 2.0 * pf);\n"
           << "    return mix(\n"
           << "        mix(dot(slip_hash22(pi + vec2(0.0, 0.0)), pf - vec2(0.0, 0.0)),\n"
           << "            dot(slip_hash22(pi + vec2(1.0, 0.0)), pf - vec2(1.0, 0.0)), w.x),\n"
           << "        mix(dot(slip_hash22(pi + vec2(0.0, 1.0)), pf - vec2(0.0, 1.0)),\n"
           << "            dot(slip_hash22(pi + vec2(1.0, 1.0)), pf - vec2(1.0, 1.0)), w.x),\n"
           << "        w.y) * 0.5 + 0.5;\n"
           << "}\n"
           << "vec2 slip_voronoi(vec2 uv, float angleOffset, float cellDensity) {\n"
           << "    vec2 g = floor(uv * cellDensity);\n"
           << "    vec2 f = fract(uv * cellDensity);\n"
           << "    float minDist = 8.0;\n"
           << "    float cellVal = 0.0;\n"
           << "    for (int y = -1; y <= 1; ++y) {\n"
           << "        for (int x = -1; x <= 1; ++x) {\n"
           << "            vec2 lattice = vec2(float(x), float(y));\n"
           << "            vec2 offset = slip_hash22(g + lattice) * 0.5 + 0.5;\n"
           << "            offset = 0.5 + 0.5 * sin(angleOffset + 6.2831853 * offset);\n"
           << "            vec2 r = lattice + offset - f;\n"
           << "            float d = dot(r, r);\n"
           << "            if (d < minDist) {\n"
           << "                minDist = d;\n"
           << "                cellVal = offset.x;\n"
           << "            }\n"
           << "        }\n"
           << "    }\n"
           << "    return vec2(sqrt(minDist), cellVal);\n"
           << "}\n\n";
    }

    GeneratedShaderCode ShaderGraphCompiler::Compile(
        const ShaderGraph& graph,
        const ShaderCompilerOptions& options
    ) {
        GeneratedShaderCode result;

        // Step 1: Locate Master Terminal
        MasterNode* master = LocateMasterNode(graph);
        if (!master) {
            result.success = false;
            result.errorMessage = "ShaderGraphCompiler Error: Graph does not contain a Master Output node.";
            return result;
        }

        // Synchronize graph and master material types
        const_cast<ShaderGraph&>(graph).materialType = master->materialType;

        // Step 2: Backward reachability to eliminate unused/orphan nodes
        std::unordered_set<std::string> reachableNodes = CollectReachableNodes(graph, master);

        // Step 3: Cycle detection on the reachable subgraph
        std::string cycleDiagnostic;
        if (DetectCycle(graph, reachableNodes, &cycleDiagnostic)) {
            result.success = false;
            result.errorMessage = cycleDiagnostic.empty() ? "ShaderGraphCompiler Error: Cycle detected in graph DAG topology." : cycleDiagnostic;
            return result;
        }

        // Step 4: Topological Sort
        bool hasCycle = false;
        std::vector<std::string> executionOrder = ComputeTopologicalOrder(graph, reachableNodes, hasCycle);
        if (hasCycle) {
            result.success = false;
            result.errorMessage = "ShaderGraphCompiler Error: Topological sort failed due to cycle.";
            return result;
        }
        result.executionOrder = executionOrder;

        // Step 5: Emit Vertex Shader
        result.vertexSource = EmitVertexShader(graph, options, reachableNodes, executionOrder);

        // Step 6: Emit Fragment Shader
        result.fragmentSource = EmitFragmentShader(
            graph, options, reachableNodes, executionOrder,
            result.textureBindings, result.exposedUniformNames
        );

        result.success = true;
        return result;
    }

} // namespace SlipEngine
```

---

## 3. Vertex Shader Generation Architecture

The Vertex Shader emitted by `ShaderGraphCompiler` maintains strict binary compatibility with SlipEngine's `MeshRenderer`, `SlipModelExtract`, and animation systems:
- Vertex attributes: `aPos` (loc 0), `aNormal` (loc 1), `aTexCoords` (loc 2), `aTangent` (loc 3), `aBitangent` (loc 4), `aBoneIDs` (loc 5), `aWeights` (loc 6).
- Skeletal skinning: Evaluates up to 100 bone transforms according to `u_SkinningQuality` (0 = Static, 1 = 1-bone, 2 = 2-bone blended, 4 = 4-bone blended).
- Output interpolators: `vWorldPos`, `vNormal`, `vTexCoords`, `vTangent`, `vBitangent`.
- Time and camera variables: `u_Time`, `u_DeltaTime`, `u_CameraPos`, `u_View`, `u_Projection`, `u_Model`.

### 3.1 Vertex Shader Template Code

```glsl
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIDs;
layout (location = 6) in vec4 aWeights;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoords;
out vec3 vTangent;
out vec3 vBitangent;

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform vec3 u_CameraPos;
uniform float u_Time;
uniform float u_DeltaTime;

#define MAX_BONES 100
uniform mat4 u_BoneTransforms[MAX_BONES];
uniform int u_SkinningQuality;
uniform bool u_HasBones;

void main() {
    mat4 skinMatrix = mat4(1.0);
    if (u_HasBones && u_SkinningQuality > 0) {
        if (u_SkinningQuality >= 4) {
            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * aWeights[0]
                       + u_BoneTransforms[aBoneIDs[1]] * aWeights[1]
                       + u_BoneTransforms[aBoneIDs[2]] * aWeights[2]
                       + u_BoneTransforms[aBoneIDs[3]] * aWeights[3];
        } else if (u_SkinningQuality == 2) {
            float totalW = aWeights[0] + aWeights[1];
            float w0 = (totalW > 0.0) ? (aWeights[0] / totalW) : 1.0;
            float w1 = (totalW > 0.0) ? (aWeights[1] / totalW) : 0.0;
            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * w0 + u_BoneTransforms[aBoneIDs[1]] * w1;
        } else if (u_SkinningQuality == 1) {
            skinMatrix = u_BoneTransforms[aBoneIDs[0]];
        }
    }
    vec4 localPos = skinMatrix * vec4(aPos, 1.0);
    vec4 worldPos = u_Model * localPos;
    vWorldPos = worldPos.xyz;
    mat3 skin3 = mat3(skinMatrix);
    mat3 model3 = mat3(u_Model);
    vNormal = normalize(model3 * (skin3 * aNormal));
    vTangent = model3 * (skin3 * aTangent);
    vBitangent = model3 * (skin3 * aBitangent);
    vTexCoords = aTexCoords;
    gl_Position = u_Projection * u_View * worldPos;
}
```

---

## 4. Fragment Shader Generation (Lit vs. Unlit)

### 4.1 Lit Mode Architecture
In Lit mode, the graph determines standard PBR surface properties:
1. `Albedo` (`vec3`): Diffuse base color.
2. `Normal` (`vec3`): Surface normal in tangent or world space.
3. `Roughness` (`float`): Microfacet roughness parameter $\alpha = \text{roughness}^2$.
4. `Metallic` (`float`): Metallic conductor factor (interpolates $F_0$ from 0.04 to Albedo).
5. `Emissive` (`vec3`): Self-illuminating radiance.
6. `AO` (`float`): Baked ambient occlusion scalar.
7. `Alpha` (`float`): Opacity scalar.
8. `AlphaClipThreshold` (`float`): Alpha test cutoff (discards if `alpha < threshold`).

After evaluating these surface properties from the topological DAG nodes, the fragment shader invokes SlipEngine's standard lighting pipeline:
- **Cook-Torrance Specular BRDF**:
  $$f_r = k_d \frac{\text{albedo}}{\pi} + \frac{D(N, H, \alpha) G(N, V, L, \alpha) F(V, H, F_0)}{4 (N \cdot V)(N \cdot L)}$$
  where:
  - $D(N, H, \alpha)$: GGX / Trowbridge-Reitz normal distribution.
  - $G(N, V, L, \alpha)$: Smith height-correlated masking-shadowing function.
  - $F(V, H, F_0)$: Schlick's approximation for Fresnel reflectance.
- **Lighting Loops**:
  - Up to 4 Directional Lights (`u_NumDirLights`) with CSM cascaded shadows and cloud shadows.
  - Up to 32 Point Lights (`u_NumPointLights`) with quadratic inverse-distance falloff.
  - Up to 4 Spot Lights (`u_NumSpotLights`) with smooth angular penumbra.
- **Indirect & Ambient Light**:
  - SlipEngine DDGI irradiance grid probes (`SampleDDGI(vWorldPos, N)`) or hemisphere ambient fallback.
  - Pre-filtered environment cubemap reflections (`u_EnvCube`) with roughness LOD mapping.
- **Dual Rendertarget Outputs**:
  - `layout(location = 0) out vec4 FragColor;` (Final lit color).
  - `layout(location = 1) out vec4 FragGeometry;` (Octahedral normal encoding + roughness + metallic for G-Buffer / DDGI).

### 4.2 Unlit Mode Architecture
In Unlit mode:
1. Evaluates graph nodes for `Color` (`vec3`), `Emissive` (`vec3`), and `Alpha` (`float`).
2. Discards fragment if `alpha < alphaClipThreshold`.
3. Directly writes `FragColor = vec4(color + emissive, alpha);`.
4. Bypasses all lighting, shadow calculations, and probe uniforms for maximum TBDR efficiency.

---

## 5. Complete GLSL Fragment Shader Templates

### 5.1 Lit Fragment Shader Template (Full SlipEngine PBR)

```glsl
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;
in vec3 vTangent;
in vec3 vBitangent;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragGeometry;

// --- Global Engine Uniforms ---
uniform vec3 u_CameraPos;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform float u_Time;
uniform float u_DeltaTime;

// --- Blackboard Exposed Property Uniforms ---
/* [[EXPOSED_PROPERTY_UNIFORMS]] */

// --- Custom Graph Texture Samplers (Units 20+) ---
/* [[GRAPH_TEXTURE_SAMPLERS]] */

// Fallback texture unit for unlinked sampler nodes
uniform sampler2D u_MainTexture;

// --- DDGI Uniforms ---
uniform bool u_DDGIEnabled;
uniform float u_DDGIIntensity;
uniform vec3 u_DDGISkyColor;
uniform vec3 u_DDGIGroundColor;
uniform int u_DDGIOctRes;
uniform int u_DDGIProbeTexel;
uniform sampler2D u_DDGIIrr[5];
uniform sampler2D u_DDGIVis[5];
uniform vec3 u_DDGIOrigin[5];
uniform vec3 u_DDGISpacing[5];
uniform ivec3 u_DDGICounts[5];
uniform vec2 u_DDGIAtlas[5];
uniform int u_DDGIFollow[5];
uniform int u_DDGIOn[5];
uniform int u_DDGIRow[5];

// --- Environment Reflection Cubemap ---
uniform samplerCube u_EnvCube;
uniform bool u_HasEnvCube;
uniform float u_EnvIntensity;
uniform float u_ReflectionRoughnessCutoff;

// --- Lighting Structs and Arrays ---
struct DirectionalLight {
    vec3 direction;
    vec3 color;
    float intensity;
};
struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float range;
};
struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngle;
};

#define MAX_DIR_LIGHTS 4
#define MAX_POINT_LIGHTS 32
#define MAX_SPOT_LIGHTS 4
uniform int u_NumDirLights;
uniform DirectionalLight u_DirLights[MAX_DIR_LIGHTS];
uniform int u_NumPointLights;
uniform PointLight u_PointLights[MAX_POINT_LIGHTS];
uniform int u_NumSpotLights;
uniform SpotLight u_SpotLights[MAX_SPOT_LIGHTS];

// --- Cascaded Shadow Maps (CSM) ---
uniform highp sampler2DArray u_ShadowMapArray;
uniform mat4 u_LightSpaceMatrices[4];
uniform float u_CascadeEndClipSpace[4];
uniform int u_CascadeCount;
uniform float u_ShadowBias;
uniform bool u_ShadowsEnabled;
uniform int u_PCFQuality;

// --- Cloud Shadows ---
uniform bool u_CloudShadowsEnabled;
uniform float u_CloudShadowIntensity;
uniform sampler2D u_CloudShadowNoise;
uniform float u_CloudBaseHeight;
uniform float u_CloudScale;
uniform vec2 u_CloudWindOffset;
uniform float u_CloudCoverage;
uniform float u_CloudLayerThickness;
uniform float u_CloudDensity;
uniform float u_CloudEdgeErosion;
uniform float u_CloudDetailScale;
uniform float u_Absorption;

// --- Procedural Math Functions ---
vec2 slip_hash22(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}
float slip_gradient_noise(vec2 p) {
    vec2 pi = floor(p);
    vec2 pf = fract(p);
    vec2 w = pf * pf * (3.0 - 2.0 * pf);
    return mix(
        mix(dot(slip_hash22(pi + vec2(0.0, 0.0)), pf - vec2(0.0, 0.0)),
            dot(slip_hash22(pi + vec2(1.0, 0.0)), pf - vec2(1.0, 0.0)), w.x),
        mix(dot(slip_hash22(pi + vec2(0.0, 1.0)), pf - vec2(0.0, 1.0)),
            dot(slip_hash22(pi + vec2(1.0, 1.0)), pf - vec2(1.0, 1.0)), w.x),
        w.y) * 0.5 + 0.5;
}
vec2 slip_voronoi(vec2 uv, float angleOffset, float cellDensity) {
    vec2 g = floor(uv * cellDensity);
    vec2 f = fract(uv * cellDensity);
    float minDist = 8.0;
    float cellVal = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 lattice = vec2(float(x), float(y));
            vec2 offset = slip_hash22(g + lattice) * 0.5 + 0.5;
            offset = 0.5 + 0.5 * sin(angleOffset + 6.2831853 * offset);
            vec2 r = lattice + offset - f;
            float d = dot(r, r);
            if (d < minDist) {
                minDist = d;
                cellVal = offset.x;
            }
        }
    }
    return vec2(sqrt(minDist), cellVal);
}

// --- DDGI & Octahedral Encoding ---
vec2 DDGIOctEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 0.000001);
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return n.xy * 0.5 + 0.5;
}
vec3 DDGIOctDecode(vec2 e) {
    vec2 f = e * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    return normalize(n);
}

vec3 SampleDDGIVolume(sampler2D irrTex, sampler2D visTex, vec3 worldPos, vec3 normal, vec3 origin, vec3 spacing, ivec3 counts, vec2 atlasSize, int row) {
    if (counts.x <= 0 || counts.y <= 0 || counts.z <= 0) return vec3(-1.0);
    vec3 spacingSafe = max(spacing, vec3(0.001));
    float spacingMin = min(spacingSafe.x, min(spacingSafe.y, spacingSafe.z));
    vec3 biased = worldPos + normal * (spacingMin * 0.45);
    vec3 grid = (biased - origin) / spacingSafe - vec3(0.5);
    ivec3 maxIdx = max(counts - ivec3(1), ivec3(0));
    ivec3 nearest = clamp(ivec3(round(grid)), ivec3(0), maxIdx);
    vec3 irr = vec3(0.0);
    float wSum = 0.0;
    for (int i = 0; i < 27; ++i) {
        ivec3 offset = ivec3(i % 3, (i / 3) % 3, i / 9) - ivec3(1);
        ivec3 idx = nearest + offset;
        if (any(lessThan(idx, ivec3(0))) || any(greaterThan(idx, maxIdx))) continue;
        vec3 delta = vec3(idx) - grid;
        float w = exp(-dot(delta, delta) * 0.85);
        vec3 probePos = origin + (vec3(idx) + vec3(0.5)) * spacingSafe;
        vec3 toPoint = biased - probePos;
        float dist = length(toPoint);
        vec3 dir = toPoint / max(dist, 0.0001);
        float wrap = clamp(dot(-dir, normal) * 0.5 + 0.5, 0.0, 1.0);
        wrap = wrap * wrap;
        int probeIndex = idx.x + idx.y * counts.x + idx.z * counts.x * counts.y;
        ivec2 probeCoord = ivec2(probeIndex % max(row, 1), probeIndex / max(row, 1));
        vec2 uv = (vec2(probeCoord * u_DDGIProbeTexel) + vec2(1.0) + DDGIOctEncode(dir) * float(u_DDGIOctRes)) / max(atlasSize, vec2(1.0));
        vec4 probeTex = texture(irrTex, uv);
        if (probeTex.a < 0.04) continue;
        w *= probeTex.a;
        vec2 moments = texture(visTex, uv).rg;
        float mean = moments.r + spacingMin * 0.08;
        float variance = abs(moments.g - moments.r * moments.r);
        float cheb = 1.0;
        if (dist > mean) {
            float d = dist - mean;
            cheb = variance / (variance + d * d + 0.0001);
            cheb = cheb * cheb * cheb;
        }
        w *= wrap * cheb;
        irr += probeTex.rgb * w * u_DDGIIntensity;
        wSum += w;
    }
    if (wSum < 0.0001) return vec3(-1.0);
    return irr / wSum;
}

vec3 SampleDDGI(vec3 worldPos, vec3 normal) {
    float hemiFactor = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyCol = (length(u_DDGISkyColor) > 0.001) ? u_DDGISkyColor : vec3(0.24, 0.27, 0.32);
    vec3 gndCol = (length(u_DDGIGroundColor) > 0.001) ? u_DDGIGroundColor : vec3(0.08, 0.085, 0.095);
    vec3 skyFallback = mix(gndCol, skyCol, hemiFactor) * max(u_DDGIIntensity, 0.5);
    if (!u_DDGIEnabled) return skyFallback;
    return skyFallback;
}

// --- Cascaded Shadow Maps Function ---
float CalculateShadowCSM(vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (!u_ShadowsEnabled || u_CascadeCount <= 0) return 0.0;
    vec4 fragPosView = u_View * vec4(worldPos, 1.0);
    float depthValue = abs(fragPosView.z);
    int layer = -1;
    for (int c = 0; c < u_CascadeCount && c < 4; ++c) {
        if (depthValue < u_CascadeEndClipSpace[c]) {
            layer = c;
            break;
        }
    }
    if (layer == -1) return 0.0;
    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);
    float bias = max(u_ShadowBias * 3.0 * (1.0 - cosTheta), u_ShadowBias * 0.4);
    vec3 biasedPos = worldPos + normal * (u_ShadowBias * 4.0 * (1.0 - cosTheta) + 0.003);
    vec4 fragPosLightSpace = u_LightSpaceMatrices[layer] * vec4(biasedPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    float currentDepth = projCoords.z;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapArray, 0).xy);
    if (u_PCFQuality == 0) {
        float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy, float(layer))).r;
        shadow = (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    } else {
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * texelSize, float(layer))).r;
                shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
            }
        }
        shadow /= 9.0;
    }
    return shadow;
}

// --- Cloud Shadows Function ---
float CalculateCloudShadow(vec3 worldPos, vec3 lightDir) {
    if (!u_CloudShadowsEnabled || lightDir.y <= 0.02) return 0.0;
    return 0.0;
}

// --- Cook-Torrance GGX/Smith PBR Functions ---
const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / max(denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / max(denom, 0.0000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // --- Topological Shader Graph Node Execution ---
/* [[NODE_STATEMENTS_BLOCK]] */

    // --- Master Node Surface Attribute Outputs ---
    vec3 albedo = /* [[EXPR_ALBEDO]] */;
    vec3 rawNormal = /* [[EXPR_NORMAL]] */;
    float roughness = clamp(/* [[EXPR_ROUGHNESS]] */, 0.04, 1.0);
    float metallic = clamp(/* [[EXPR_METALLIC]] */, 0.0, 1.0);
    vec3 emissive = /* [[EXPR_EMISSIVE]] */;
    float ao = clamp(/* [[EXPR_AO]] */, 0.0, 1.0);
    float alpha = clamp(/* [[EXPR_ALPHA]] */, 0.0, 1.0);
    float alphaClipThreshold = /* [[EXPR_ALPHA_CLIP]] */;

    if (alpha < alphaClipThreshold) discard;

    // Normal evaluation (Transform from tangent space if needed)
    vec3 geomN = length(vNormal) > 0.001 ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);
    vec3 N = geomN;
    if (length(rawNormal) > 0.001) {
        vec3 T = normalize(vTangent - dot(vTangent, geomN) * geomN);
        vec3 B = cross(geomN, T);
        mat3 TBN = mat3(T, B, geomN);
        N = normalize(TBN * rawNormal);
    }

    vec3 V = normalize(u_CameraPos - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    // Directional Lights Loop
    for (int i = 0; i < u_NumDirLights && i < MAX_DIR_LIGHTS; ++i) {
        vec3 L = normalize(-u_DirLights[i].direction);
        vec3 H = normalize(V + L);
        vec3 radiance = u_DirLights[i].color * u_DirLights[i].intensity;
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);
        float shadowFactor = (i == 0) ? CalculateShadowCSM(vWorldPos, N, L) : 0.0;
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);
    }

    // Point Lights Loop
    for (int i = 0; i < u_NumPointLights && i < MAX_POINT_LIGHTS; ++i) {
        vec3 lightDir = u_PointLights[i].position - vWorldPos;
        float dist = length(lightDir);
        if (dist > u_PointLights[i].range && u_PointLights[i].range > 0.0) continue;
        vec3 L = normalize(lightDir);
        vec3 H = normalize(V + L);
        float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));
        if (u_PointLights[i].range > 0.0) attenuation *= clamp(1.0 - (dist / u_PointLights[i].range), 0.0, 1.0);
        vec3 radiance = u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // Spot Lights Loop
    for (int i = 0; i < u_NumSpotLights && i < MAX_SPOT_LIGHTS; ++i) {
        vec3 lightDir = u_SpotLights[i].position - vWorldPos;
        float dist = length(lightDir);
        if (dist > u_SpotLights[i].range && u_SpotLights[i].range > 0.0) continue;
        vec3 L = normalize(lightDir);
        float theta = dot(L, normalize(-u_SpotLights[i].direction));
        float innerCut = cos(radians(u_SpotLights[i].spotAngle * 0.5));
        float outerCut = cos(radians(u_SpotLights[i].spotAngle * 0.65));
        float spotFactor = clamp((theta - outerCut) / max(innerCut - outerCut, 0.0001), 0.0, 1.0);
        if (spotFactor <= 0.0) continue;
        vec3 H = normalize(V + L);
        float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));
        vec3 radiance = u_SpotLights[i].color * u_SpotLights[i].intensity * attenuation * spotFactor;
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // Indirect Ambient & DDGI
    float hemiAmbient = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(vec3(0.08, 0.085, 0.095), vec3(0.24, 0.27, 0.32), hemiAmbient) * albedo * ao;
    if (u_DDGIEnabled) {
        vec3 irr = SampleDDGI(vWorldPos, N);
        ambient = vec3(1.0 - metallic) * irr * albedo * ao;
    }

    vec3 color = ambient + Lo + emissive;

    // Environment Reflection Cubemap
    if (u_HasEnvCube) {
        vec3 R = reflect(-V, N);
        vec3 envColor = textureLod(u_EnvCube, R, roughness * 6.0).rgb;
        vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        float roughLimit = max(u_ReflectionRoughnessCutoff, 0.05);
        float roughFade = smoothstep(roughLimit, roughLimit * 0.15, roughness);
        vec3 envSpecular = envColor * F * roughFade;
        color += envSpecular * max(u_EnvIntensity, 0.0);
    }

    FragColor = vec4(max(color, vec3(0.0)), alpha);
    FragGeometry = vec4(DDGIOctEncode(N), roughness, metallic);
}
```

---

### 5.2 Unlit Fragment Shader Template

```glsl
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoords;

layout(location = 0) out vec4 FragColor;

// --- Global Engine Uniforms ---
uniform vec3 u_CameraPos;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform float u_Time;
uniform float u_DeltaTime;

// --- Blackboard Exposed Property Uniforms ---
/* [[EXPOSED_PROPERTY_UNIFORMS]] */

// --- Custom Graph Texture Samplers (Units 20+) ---
/* [[GRAPH_TEXTURE_SAMPLERS]] */

// Fallback texture unit for unlinked sampler nodes
uniform sampler2D u_MainTexture;

// --- Procedural Math Functions ---
vec2 slip_hash22(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}
float slip_gradient_noise(vec2 p) {
    vec2 pi = floor(p);
    vec2 pf = fract(p);
    vec2 w = pf * pf * (3.0 - 2.0 * pf);
    return mix(
        mix(dot(slip_hash22(pi + vec2(0.0, 0.0)), pf - vec2(0.0, 0.0)),
            dot(slip_hash22(pi + vec2(1.0, 0.0)), pf - vec2(1.0, 0.0)), w.x),
        mix(dot(slip_hash22(pi + vec2(0.0, 1.0)), pf - vec2(0.0, 1.0)),
            dot(slip_hash22(pi + vec2(1.0, 1.0)), pf - vec2(1.0, 1.0)), w.x),
        w.y) * 0.5 + 0.5;
}
vec2 slip_voronoi(vec2 uv, float angleOffset, float cellDensity) {
    vec2 g = floor(uv * cellDensity);
    vec2 f = fract(uv * cellDensity);
    float minDist = 8.0;
    float cellVal = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 lattice = vec2(float(x), float(y));
            vec2 offset = slip_hash22(g + lattice) * 0.5 + 0.5;
            offset = 0.5 + 0.5 * sin(angleOffset + 6.2831853 * offset);
            vec2 r = lattice + offset - f;
            float d = dot(r, r);
            if (d < minDist) {
                minDist = d;
                cellVal = offset.x;
            }
        }
    }
    return vec2(sqrt(minDist), cellVal);
}

void main() {
    // --- Topological Shader Graph Node Execution ---
/* [[NODE_STATEMENTS_BLOCK]] */

    // --- Master Unlit Surface Attribute Outputs ---
    vec3 color = /* [[EXPR_COLOR]] */;
    vec3 emissive = /* [[EXPR_EMISSIVE]] */;
    float alpha = clamp(/* [[EXPR_ALPHA]] */, 0.0, 1.0);
    float alphaClipThreshold = /* [[EXPR_ALPHA_CLIP]] */;

    if (alpha < alphaClipThreshold) discard;

    vec3 finalColor = color + emissive;
    FragColor = vec4(finalColor, alpha);
}
```

---

## 6. Detailed Node Code Generation & Variable Scoping

### 6.1 Statement Generation & Unique Variable Naming
To prevent variable name collisions when multiple nodes of the same type exist in a graph (e.g. 5 `Math_Add` nodes or 3 `Texture_Sampler` nodes), the compiler allocates a deterministic, sanitized identifier for each output pin:
$$\text{varName} = \text{"n\_"} + \text{SanitizeIdentifier}(nodeId) + \text{"\_"} + \text{SanitizeIdentifier}(outPortId);$$

For nodes that emit statements (e.g. `TextureSamplerNode` emitting `vec4 texSample_<id> = texture(...);` or `ProceduralVoronoiNode` emitting `vec2 voronoi_<id> = slip_voronoi(...);`), the generated variables are bound into the compiler's output expression map.

### 6.2 Table of Node Code Generation Skeletons

| Node Type | Category | Inputs Used | Output Port | Generated GLSL Expression |
|---|---|---|---|---|
| `Math_Add` | Math | `A`, `B` | `Out` | `(A + B)` |
| `Math_Subtract` | Math | `A`, `B` | `Out` | `(A - B)` |
| `Math_Multiply` | Math | `A`, `B` | `Out` | `(A * B)` |
| `Math_Divide` | Math | `A`, `B` | `Out` | `(A / (B != 0.0 ? B : 0.00001))` |
| `Math_Sin` | Trig | `In` | `Out` | `sin(In)` |
| `Math_Cos` | Trig | `In` | `Out` | `cos(In)` |
| `Math_Tan` | Trig | `In` | `Out` | `tan(In)` |
| `Math_Dot` | Vector | `A`, `B` | `Out` | `dot(A, B)` |
| `Math_Cross` | Vector | `A`, `B` | `Out` | `cross(A, B)` |
| `Math_Normalize` | Vector | `In` | `Out` | `normalize(In)` |
| `Math_Length` | Vector | `In` | `Out` | `length(In)` |
| `Math_Distance` | Vector | `A`, `B` | `Out` | `distance(A, B)` |
| `Procedural_Min` | Procedural | `A`, `B` | `Out` | `min(A, B)` |
| `Procedural_Max` | Procedural | `A`, `B` | `Out` | `max(A, B)` |
| `Procedural_Clamp` | Procedural | `In`, `Min`, `Max` | `Out` | `clamp(In, Min, Max)` |
| `Procedural_Lerp` | Procedural | `A`, `B`, `T` | `Out` | `mix(A, B, T)` |
| `Procedural_Abs` | Procedural | `In` | `Out` | `abs(In)` |
| `Procedural_Step` | Procedural | `Edge`, `In` | `Out` | `step(Edge, In)` |
| `Procedural_Smoothstep`| Procedural | `Edge1`, `Edge2`, `In` | `Out` | `smoothstep(Edge1, Edge2, In)` |
| `Procedural_Remap` | Procedural | `In`, `InMin`, `InMax`, `OutMin`, `OutMax` | `Out` | `(OutMin + ((In - InMin) / max(InMax - InMin, 0.00001)) * (OutMax - OutMin))` |
| `Procedural_Noise` | Procedural | `UV`, `Scale` | `Out` | `slip_gradient_noise(UV * Scale)` |
| `Procedural_Voronoi`| Procedural | `UV`, `AngleOffset`, `Scale` | `Out`, `Cells` | Statement: `vec2 vor = slip_voronoi(UV, Angle, Scale);` |
| `Time_Time` | Time | *(none)* | `Time`, `SinTime`, `CosTime`, `DeltaTime` | `u_Time`, `sin(u_Time)`, `cos(u_Time)`, `u_DeltaTime` |
| `Time_DeltaTime` | Time | *(none)* | `DeltaTime` | `u_DeltaTime` |
| `Transform_CameraPosition` | Transform | *(none)* | `Position` | `u_CameraPos` |
| `Transform_ViewMatrix` | Transform | *(none)* | `View` | `u_View` |
| `Transform_ProjectionMatrix` | Transform | *(none)* | `Projection` | `u_Projection` |
| `Transform_WorldPosition` | Transform | *(none)* | `Position` | `vWorldPos` |
| `Transform_WorldNormal` | Transform | *(none)* | `Normal` | `vNormal` |
| `Texture_UV` | Texture | *(none)* | `UV` | `vTexCoords` |
| `Texture_TilingAndOffset` | Texture | `UV`, `Tiling`, `Offset` | `Out` | `(UV * Tiling + Offset)` |
| `Texture_Sampler` | Texture | `UV`, `Sampler` | `RGBA`, `RGB`, `R`, `G`, `B`, `A` | Statement: `vec4 texSample = texture(Sampler, UV);` |
| `Input_Property` | Input | *(none)* | `Out` | `u_<propertyName>` |
| `Input_Float` | Input | *(none)* | `Out` | Literal formatted float (e.g. `1.5000`) |
| `Input_Vec2` | Input | *(none)* | `Out` | `vec2(x, y)` |
| `Input_Vec3` | Input | *(none)* | `Out` | `vec3(x, y, z)` |
| `Input_Vec4` | Input | *(none)* | `Out` | `vec4(x, y, z, w)` |
| `Vector_Split` | Vector | `In` | `X`, `Y`, `Z`, `W` | `(In).x`, `(In).y`, `(In).z`, `(In).w` |
| `Vector_Combine` | Vector | `X`, `Y`, `Z`, `W` | `Vec4`, `Vec3`, `Vec2` | `vec4(X, Y, Z, W)`, `vec3(X, Y, Z)`, `vec2(X, Y)` |

---

## 7. SlipMaterial Runtime Integration Contract

In Milestone 2, `SlipMaterial` is extended with shader graph loading and dynamic compilation:

```cpp
// In include/SlipEngine/render/SlipMaterial.h:
public:
    std::string shaderGraphPath;
    std::unordered_map<std::string, float> graphFloatParams;
    std::unordered_map<std::string, glm::vec4> graphVectorParams;
    std::unordered_map<std::string, std::string> graphTextureParams;
    std::unordered_map<std::string, SlipTexture*> graphLoadedTextures;

    bool LoadShaderGraph(const std::string& graphPath);
    void SetGraphFloat(const std::string& name, float value);
    void SetGraphVector(const std::string& name, const glm::vec4& value);
    void SetGraphTexture(const std::string& name, const std::string& texturePath);
    bool RecompileGraphShader();
```

### Binding in `SlipMaterial::bindMaterialProperties(SlipShader* targetShader)`:
1. Bind global time and deltaTime:
   ```cpp
   targetShader->setFloat("u_Time", SlipGlobals::Get().GetTime());
   targetShader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
   ```
2. Bind blackboard exposed property overrides:
   - Floats: `targetShader->setFloat("u_" + name, val);`
   - Vec2/3/4: `targetShader->setVec4("u_" + name, val);`
3. Bind custom texture samplers starting at texture unit 20:
   ```cpp
   int texUnit = 20;
   for (const auto& binding : m_CompiledTextureBindings) {
       targetShader->setInt(binding.uniformName, texUnit);
       SlipTexture* tex = graphLoadedTextures[binding.propertyId];
       if (tex) {
           tex->bind(texUnit);
       }
       texUnit++;
   }
   ```

---

## 8. Robustness & Invariant Guarantees

1. **Cycle Prevention**:
   The compiler uses strict 3-color DFS cycle detection. Graphs containing cyclical connections are rejected before code generation with a human-readable diagnostic error message.
2. **Dead Code Elimination**:
   Starting from `MasterNode` backwards guarantees that only nodes actively contributing to the master outputs are compiled. Any unused or unlinked experimental nodes on the canvas are omitted.
3. **Identifier Sanitization**:
   All node IDs and property names with special characters, spaces, hyphens, or leading digits are sanitized into standard GLSL identifier tokens (`[a-zA-Z_][a-zA-Z0-9_]*`).
4. **Division by Zero Protection**:
   All math division and normalization nodes feature epsilon clamps (`max(..., 0.00001)`) to avoid GLSL undefined division-by-zero behavior and NaN generation.
5. **Texture Unit Collision Safety**:
   Custom graph textures are assigned texture units 20+, strictly isolating them from engine PBR units (0..17) and DDGI/reflection units (3, 5..14).
