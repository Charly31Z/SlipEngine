#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include <sstream>
#include <iomanip>
#include <queue>
#include <stack>
#include <algorithm>
#include <functional>

namespace SlipShaderGraph {

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
        MasterNode* master = graph.GetMasterNode();
        if (master) return master;

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

    bool ShaderGraphCompiler::DetectCycle(const ShaderGraph& graph) {
        std::unordered_set<std::string> allNodes;
        for (const auto& node : graph.nodes) {
            allNodes.insert(node->id);
        }
        return DetectCycle(graph, allNodes, nullptr);
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
        for (const auto& id : reachableNodes) {
            if (inDegree[id] == 0) {
                q.push(id);
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

    std::vector<std::string> ShaderGraphCompiler::TopologicalSort(
        const ShaderGraph& graph,
        bool& outHasCycle
    ) {
        std::unordered_set<std::string> allNodes;
        for (const auto& node : graph.nodes) {
            allNodes.insert(node->id);
        }
        return ComputeTopologicalOrder(graph, allNodes, outHasCycle);
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

    void ShaderGraphCompiler::AppendLightingStructsAndUniforms(std::ostringstream& ss, const ShaderCompilerOptions& options) {
        (void)options;
        ss << "// --- DDGI Uniforms ---\n"
           << "uniform bool u_DDGIEnabled;\n"
           << "uniform float u_DDGIIntensity;\n"
           << "uniform vec3 u_DDGISkyColor;\n"
           << "uniform vec3 u_DDGIGroundColor;\n"
           << "uniform int u_DDGIOctRes;\n"
           << "uniform int u_DDGIProbeTexel;\n"
           << "uniform sampler2D u_DDGIIrr[5];\n"
           << "uniform sampler2D u_DDGIVis[5];\n"
           << "uniform vec3 u_DDGIOrigin[5];\n"
           << "uniform vec3 u_DDGISpacing[5];\n"
           << "uniform ivec3 u_DDGICounts[5];\n"
           << "uniform vec2 u_DDGIAtlas[5];\n"
           << "uniform int u_DDGIFollow[5];\n"
           << "uniform int u_DDGIOn[5];\n"
           << "uniform int u_DDGIRow[5];\n\n"
           << "// --- Environment Reflection Cubemap ---\n"
           << "uniform samplerCube u_EnvCube;\n"
           << "uniform bool u_HasEnvCube;\n"
           << "uniform float u_EnvIntensity;\n"
           << "uniform float u_ReflectionRoughnessCutoff;\n\n"
           << "// --- Lighting Structs and Arrays ---\n"
           << "struct DirectionalLight {\n"
           << "    vec3 direction;\n"
           << "    vec3 color;\n"
           << "    float intensity;\n"
           << "};\n"
           << "struct PointLight {\n"
           << "    vec3 position;\n"
           << "    vec3 color;\n"
           << "    float intensity;\n"
           << "    float range;\n"
           << "};\n"
           << "struct SpotLight {\n"
           << "    vec3 position;\n"
           << "    vec3 direction;\n"
           << "    vec3 color;\n"
           << "    float intensity;\n"
           << "    float range;\n"
           << "    float spotAngle;\n"
           << "};\n\n"
           << "#define MAX_DIR_LIGHTS 4\n"
           << "#define MAX_POINT_LIGHTS 32\n"
           << "#define MAX_SPOT_LIGHTS 4\n"
           << "uniform int u_NumDirLights;\n"
           << "uniform DirectionalLight u_DirLights[MAX_DIR_LIGHTS];\n"
           << "uniform int u_NumPointLights;\n"
           << "uniform PointLight u_PointLights[MAX_POINT_LIGHTS];\n"
           << "uniform int u_NumSpotLights;\n"
           << "uniform SpotLight u_SpotLights[MAX_SPOT_LIGHTS];\n"
           << "struct GpuClusteredLight {\n"
           << "    vec4 position_range;\n"
           << "    vec4 color_intensity;\n"
           << "    vec4 direction_spotAngle;\n"
           << "    uvec4 params;\n"
           << "};\n"
           << "layout(std430, binding = 2) readonly buffer LightsBuffer {\n"
           << "    GpuClusteredLight u_Lights[];\n"
           << "};\n"
           << "layout(std430, binding = 4) readonly buffer ClusterGridBuffer {\n"
           << "    uvec2 u_ClusterGrid[];\n"
           << "};\n"
           << "layout(std430, binding = 5) readonly buffer LightIndexBuffer {\n"
           << "    uint u_GlobalLightIndexList[];\n"
           << "};\n"
           << "uniform bool u_ClusteredLightingEnabled;\n"
           << "uniform uvec3 u_ClusterGridDim;\n"
           << "uniform vec2 u_ClusterTileSize;\n"
           << "uniform float u_ClusterZNear;\n"
           << "uniform float u_ClusterZFar;\n"
           << "uniform float u_ClusterLogZFactor;\n"
           << "uniform bool u_HasLightmap;\n"
           << "uniform sampler2D u_LightmapAtlas;\n"
           << "uniform vec4 u_LightmapScaleOffset;\n\n"
           << "// --- Cascaded Shadow Maps (CSM) ---\n"
           << "uniform highp sampler2DArray u_ShadowMapArray;\n"
           << "uniform mat4 u_LightSpaceMatrices[4];\n"
           << "uniform float u_CascadeEndClipSpace[4];\n"
           << "uniform int u_CascadeCount;\n"
           << "uniform float u_ShadowBias;\n"
           << "uniform bool u_ShadowsEnabled;\n"
           << "uniform int u_PCFQuality;\n\n"
           << "// --- Cloud Shadows ---\n"
           << "uniform bool u_CloudShadowsEnabled;\n"
           << "uniform float u_CloudShadowIntensity;\n"
           << "uniform sampler2D u_CloudShadowNoise;\n"
           << "uniform float u_CloudBaseHeight;\n"
           << "uniform float u_CloudScale;\n"
           << "uniform vec2 u_CloudWindOffset;\n"
           << "uniform float u_CloudCoverage;\n"
           << "uniform float u_CloudLayerThickness;\n"
           << "uniform float u_CloudDensity;\n"
           << "uniform float u_CloudEdgeErosion;\n"
           << "uniform float u_CloudDetailScale;\n"
           << "uniform float u_Absorption;\n\n";
    }

    void ShaderGraphCompiler::AppendPBRFunctions(std::ostringstream& ss, const ShaderCompilerOptions& options) {
        (void)options;
        ss << "// --- DDGI & Octahedral Encoding ---\n"
           << "vec2 DDGIOctEncode(vec3 n) {\n"
           << "    n /= (abs(n.x) + abs(n.y) + abs(n.z) + 0.000001);\n"
           << "    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);\n"
           << "    return n.xy * 0.5 + 0.5;\n"
           << "}\n"
           << "vec3 DDGIOctDecode(vec2 e) {\n"
           << "    vec2 f = e * 2.0 - 1.0;\n"
           << "    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));\n"
           << "    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);\n"
           << "    return normalize(n);\n"
           << "}\n\n"
           << "vec3 SampleDDGI(vec3 worldPos, vec3 normal) {\n"
           << "    float hemiFactor = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);\n"
           << "    vec3 skyCol = (length(u_DDGISkyColor) > 0.001) ? u_DDGISkyColor : vec3(0.24, 0.27, 0.32);\n"
           << "    vec3 gndCol = (length(u_DDGIGroundColor) > 0.001) ? u_DDGIGroundColor : vec3(0.08, 0.085, 0.095);\n"
           << "    vec3 skyFallback = mix(gndCol, skyCol, hemiFactor) * u_DDGIIntensity;\n"
           << "    if (!u_DDGIEnabled) return skyFallback;\n"
           << "    return skyFallback;\n"
           << "}\n\n"
           << "// --- Cascaded Shadow Maps Function ---\n"
           << "float CalculateShadowCSM(vec3 worldPos, vec3 normal, vec3 lightDir) {\n"
           << "    if (!u_ShadowsEnabled || u_CascadeCount <= 0) return 0.0;\n"
           << "    vec4 fragPosView = u_View * vec4(worldPos, 1.0);\n"
           << "    float depthValue = abs(fragPosView.z);\n"
           << "    int layer = -1;\n"
           << "    for (int c = 0; c < u_CascadeCount && c < 4; ++c) {\n"
           << "        if (depthValue < u_CascadeEndClipSpace[c]) {\n"
           << "            layer = c;\n"
           << "            break;\n"
           << "        }\n"
           << "    }\n"
           << "    if (layer == -1) return 0.0;\n"
           << "    float cosTheta = clamp(dot(normal, lightDir), 0.0, 1.0);\n"
           << "    float bias = max(u_ShadowBias * 3.0 * (1.0 - cosTheta), u_ShadowBias * 0.4);\n"
           << "    vec3 biasedPos = worldPos + normal * (u_ShadowBias * 4.0 * (1.0 - cosTheta) + 0.003);\n"
           << "    vec4 fragPosLightSpace = u_LightSpaceMatrices[layer] * vec4(biasedPos, 1.0);\n"
           << "    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;\n"
           << "    projCoords = projCoords * 0.5 + 0.5;\n"
           << "    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {\n"
           << "        return 0.0;\n"
           << "    }\n"
           << "    float currentDepth = projCoords.z;\n"
           << "    float shadow = 0.0;\n"
           << "    vec2 texelSize = 1.0 / vec2(textureSize(u_ShadowMapArray, 0).xy);\n"
           << "    if (u_PCFQuality == 0) {\n"
           << "        float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy, float(layer))).r;\n"
           << "        shadow = (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;\n"
           << "    } else {\n"
           << "        for (int x = -1; x <= 1; ++x) {\n"
           << "            for (int y = -1; y <= 1; ++y) {\n"
           << "                float pcfDepth = texture(u_ShadowMapArray, vec3(projCoords.xy + vec2(x, y) * texelSize, float(layer))).r;\n"
           << "                shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;\n"
           << "            }\n"
           << "        }\n"
           << "        shadow /= 9.0;\n"
           << "    }\n"
           << "    return shadow;\n"
           << "}\n\n"
           << "// --- Cook-Torrance GGX/Smith PBR Functions ---\n"
           << "const float PI = 3.14159265359;\n\n"
           << "float DistributionGGX(vec3 N, vec3 H, float roughness) {\n"
           << "    float a = roughness * roughness;\n"
           << "    float a2 = a * a;\n"
           << "    float NdotH = max(dot(N, H), 0.0);\n"
           << "    float NdotH2 = NdotH * NdotH;\n"
           << "    float nom = a2;\n"
           << "    float denom = (NdotH2 * (a2 - 1.0) + 1.0);\n"
           << "    denom = PI * denom * denom;\n"
           << "    return nom / max(denom, 0.0000001);\n"
           << "}\n\n"
           << "float GeometrySchlickGGX(float NdotV, float roughness) {\n"
           << "    float r = (roughness + 1.0);\n"
           << "    float k = (r * r) / 8.0;\n"
           << "    float nom = NdotV;\n"
           << "    float denom = NdotV * (1.0 - k) + k;\n"
           << "    return nom / max(denom, 0.0000001);\n"
           << "}\n\n"
           << "float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {\n"
           << "    float NdotV = max(dot(N, V), 0.0);\n"
           << "    float NdotL = max(dot(N, L), 0.0);\n"
           << "    float ggx2 = GeometrySchlickGGX(NdotV, roughness);\n"
           << "    float ggx1 = GeometrySchlickGGX(NdotL, roughness);\n"
           << "    return ggx1 * ggx2;\n"
           << "}\n\n"
           << "vec3 FresnelSchlick(float cosTheta, vec3 F0) {\n"
           << "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
           << "}\n\n"
           << "vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {\n"
           << "    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
           << "}\n\n";
    }

    std::string ShaderGraphCompiler::EmitVertexShader(
        const ShaderGraph& graph,
        const ShaderCompilerOptions& options,
        const std::unordered_set<std::string>& reachableNodes,
        const std::vector<std::string>& executionOrder
    ) {
        (void)graph;
        (void)reachableNodes;
        (void)executionOrder;
        std::ostringstream ss;
        AppendGLSLVersionHeader(ss, options.targetVersion);

        ss << "layout (location = 0) in vec3 aPos;\n"
           << "layout (location = 1) in vec3 aNormal;\n"
           << "layout (location = 2) in vec2 aTexCoords;\n"
           << "layout (location = 3) in vec3 aTangent;\n"
           << "layout (location = 4) in vec3 aBitangent;\n"
           << "layout (location = 5) in ivec4 aBoneIDs;\n"
           << "layout (location = 6) in vec4 aWeights;\n"
           << "layout (location = 7) in vec2 aTexCoords2;\n\n"
           << "out vec3 vWorldPos;\n"
           << "out vec3 vNormal;\n"
           << "out vec2 vTexCoords;\n"
           << "out vec2 vTexCoords2;\n"
           << "out vec3 vTangent;\n"
           << "out vec3 vBitangent;\n"
           << "out vec3 vViewPos;\n\n"
           << "uniform mat4 u_Model;\n"
           << "uniform mat4 u_View;\n"
           << "uniform mat4 u_Projection;\n"
           << "uniform vec3 u_CameraPos;\n"
           << "uniform float u_Time;\n"
           << "uniform float u_DeltaTime;\n\n"
           << "#define MAX_BONES 100\n"
           << "uniform mat4 u_BoneTransforms[MAX_BONES];\n"
           << "uniform int u_SkinningQuality;\n"
           << "uniform bool u_HasBones;\n\n"
           << "void main() {\n"
           << "    mat4 skinMatrix = mat4(1.0);\n"
           << "    if (u_HasBones && u_SkinningQuality > 0) {\n"
           << "        if (u_SkinningQuality >= 4) {\n"
           << "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * aWeights[0]\n"
           << "                       + u_BoneTransforms[aBoneIDs[1]] * aWeights[1]\n"
           << "                       + u_BoneTransforms[aBoneIDs[2]] * aWeights[2]\n"
           << "                       + u_BoneTransforms[aBoneIDs[3]] * aWeights[3];\n"
           << "        } else if (u_SkinningQuality == 2) {\n"
           << "            float totalW = aWeights[0] + aWeights[1];\n"
           << "            float w0 = (totalW > 0.0) ? (aWeights[0] / totalW) : 1.0;\n"
           << "            float w1 = (totalW > 0.0) ? (aWeights[1] / totalW) : 0.0;\n"
           << "            skinMatrix = u_BoneTransforms[aBoneIDs[0]] * w0 + u_BoneTransforms[aBoneIDs[1]] * w1;\n"
           << "        } else if (u_SkinningQuality == 1) {\n"
           << "            skinMatrix = u_BoneTransforms[aBoneIDs[0]];\n"
           << "        }\n"
           << "    }\n"
           << "    vec4 localPos = skinMatrix * vec4(aPos, 1.0);\n"
           << "    vec4 worldPos = u_Model * localPos;\n"
           << "    vWorldPos = worldPos.xyz;\n"
           << "    vViewPos = (u_View * worldPos).xyz;\n"
           << "    mat3 skin3 = mat3(skinMatrix);\n"
           << "    mat3 model3 = mat3(u_Model);\n"
           << "    vNormal = normalize(model3 * (skin3 * aNormal));\n"
           << "    vTangent = model3 * (skin3 * aTangent);\n"
           << "    vBitangent = model3 * (skin3 * aBitangent);\n"
           << "    vTexCoords = aTexCoords;\n"
           << "    vTexCoords2 = aTexCoords2;\n"
           << "    gl_Position = u_Projection * u_View * worldPos;\n"
           << "}\n";

        return ss.str();
    }

    std::string ShaderGraphCompiler::EmitFragmentShader(
        const ShaderGraph& graph,
        const ShaderCompilerOptions& options,
        const std::unordered_set<std::string>& reachableNodes,
        const std::vector<std::string>& executionOrder,
        std::vector<CompiledTextureBinding>& outTexBindings,
        std::vector<std::string>& outExposedUniforms
    ) {
        std::ostringstream ss;
        AppendGLSLVersionHeader(ss, options.targetVersion);

        ss << "in vec3 vWorldPos;\n"
           << "in vec3 vNormal;\n"
           << "in vec2 vTexCoords;\n"
           << "in vec2 vTexCoords2;\n"
           << "in vec3 vTangent;\n"
           << "in vec3 vBitangent;\n"
           << "in vec3 vViewPos;\n\n";

        bool isLit = (graph.materialType == ShaderGraphType::Lit);
        if (isLit) {
            ss << "layout(location = 0) out vec4 FragColor;\n"
               << "layout(location = 1) out vec4 FragGeometry;\n\n";
        } else {
            ss << "layout(location = 0) out vec4 FragColor;\n\n";
        }

        ss << "// --- Global Engine Uniforms ---\n"
           << "uniform vec3 u_CameraPos;\n"
           << "uniform mat4 u_View;\n"
           << "uniform mat4 u_Projection;\n"
           << "uniform float u_Time;\n"
           << "uniform float u_DeltaTime;\n\n";

        // Blackboard Exposed Property Uniforms
        ss << "// --- Blackboard Exposed Property Uniforms ---\n";
        int currentTexUnit = 20;
        outTexBindings.clear();
        outExposedUniforms.clear();

        for (const auto& prop : graph.properties) {
            std::string uName = prop.GetUniformName();
            outExposedUniforms.push_back(uName);

            switch (prop.type) {
                case PinType::Float:
                    ss << "uniform float " << uName << ";\n";
                    break;
                case PinType::Vec2:
                    ss << "uniform vec2 " << uName << ";\n";
                    break;
                case PinType::Vec3:
                    ss << "uniform vec3 " << uName << ";\n";
                    break;
                case PinType::Vec4:
                    ss << "uniform vec4 " << uName << ";\n";
                    break;
                case PinType::Boolean:
                    ss << "uniform bool " << uName << ";\n";
                    break;
                case PinType::Texture2D: {
                    ss << "uniform sampler2D " << uName << ";\n";
                    CompiledTextureBinding b;
                    b.propertyId = prop.id;
                    b.uniformName = uName;
                    b.textureUnit = currentTexUnit++;
                    b.defaultTexturePath = prop.defaultTexturePath;
                    outTexBindings.push_back(b);
                    break;
                }
                case PinType::Matrix4:
                    ss << "uniform mat4 " << uName << ";\n";
                    break;
            }
        }

        // Fallback main texture sampler for unconnected sampler nodes
        ss << "uniform sampler2D u_MainTexture;\n\n";

        // Procedural Math Library
        AppendProceduralMathLibrary(ss);

        // Lit-specific PBR structures and functions
        if (isLit) {
            AppendLightingStructsAndUniforms(ss, options);
            AppendPBRFunctions(ss, options);
        }

        // Main Evaluation Body
        ss << "void main() {\n";

        // Evaluate DAG nodes in topological order
        std::unordered_map<std::string, std::string> evaluatedOutputs;
        std::string masterAlbedoExpr = "vec3(1.0)";
        std::string masterNormalExpr = "vec3(0.0, 0.0, 1.0)";
        std::string masterRoughnessExpr = "0.5";
        std::string masterMetallicExpr = "0.0";
        std::string masterEmissiveExpr = "vec3(0.0)";
        std::string masterAOExpr = "1.0";
        std::string masterAlphaExpr = "1.0";
        std::string masterAlphaClipExpr = "0.5";
        std::string masterColorExpr = "vec3(1.0)";

        for (const auto& nodeId : executionOrder) {
            ShaderNode* node = graph.FindNode(nodeId);
            if (!node) continue;

            MasterNode* master = dynamic_cast<MasterNode*>(node);
            if (master) {
                // Read master inputs from evaluatedOutputs
                auto resolveInput = [&](const std::string& portName, const std::string& defaultVal, PinType targetType) -> std::string {
                    for (const auto& link : graph.links) {
                        if (link.toNodeId == master->id && (link.toPortId == portName || link.toPortId == master->FindInput(portName)->id)) {
                            std::string key = link.fromNodeId + "_" + link.fromPortId;
                            if (evaluatedOutputs.count(key)) {
                                const ShaderNode* fromN = graph.FindNode(link.fromNodeId);
                                const ShaderPort* fromP = fromN ? fromN->FindOutput(link.fromPortId) : nullptr;
                                PinType fromType = fromP ? fromP->type : targetType;
                                return ConvertGLSLExpression(fromType, targetType, evaluatedOutputs[key]);
                            }
                        }
                    }
                    const ShaderPort* p = master->FindInput(portName);
                    if (p) return FormatGLSLValue(p->type, p->defaultValue);
                    return defaultVal;
                };

                if (isLit) {
                    masterAlbedoExpr = resolveInput("Albedo", "vec3(1.0)", PinType::Vec3);
                    masterNormalExpr = resolveInput("Normal", "vec3(0.0, 0.0, 1.0)", PinType::Vec3);
                    masterRoughnessExpr = resolveInput("Roughness", "0.5", PinType::Float);
                    masterMetallicExpr = resolveInput("Metallic", "0.0", PinType::Float);
                    masterEmissiveExpr = resolveInput("Emissive", "vec3(0.0)", PinType::Vec3);
                    masterAOExpr = resolveInput("AO", "1.0", PinType::Float);
                    masterAlphaExpr = resolveInput("Alpha", "1.0", PinType::Float);
                    masterAlphaClipExpr = resolveInput("AlphaClipThreshold", "0.5", PinType::Float);
                } else {
                    masterColorExpr = resolveInput("Color", "vec3(1.0)", PinType::Vec3);
                    masterEmissiveExpr = resolveInput("Emissive", "vec3(0.0)", PinType::Vec3);
                    masterAlphaExpr = resolveInput("Alpha", "1.0", PinType::Float);
                    masterAlphaClipExpr = resolveInput("AlphaClipThreshold", "0.5", PinType::Float);
                }
                continue;
            }

            // Normal node evaluation
            std::unordered_map<std::string, std::string> inExpr;
            for (const auto& inPort : node->inputs) {
                bool foundLink = false;
                for (const auto& link : graph.links) {
                    if (link.toNodeId == node->id && (link.toPortId == inPort.id || link.toPortId == inPort.name)) {
                        std::string key = link.fromNodeId + "_" + link.fromPortId;
                        if (evaluatedOutputs.count(key)) {
                            const ShaderNode* fromNode = graph.FindNode(link.fromNodeId);
                            const ShaderPort* fromPort = fromNode ? fromNode->FindOutput(link.fromPortId) : nullptr;
                            PinType fromType = fromPort ? fromPort->type : inPort.type;
                            std::string converted = ConvertGLSLExpression(fromType, inPort.type, evaluatedOutputs[key]);
                            inExpr[inPort.name] = converted;
                            inExpr[inPort.id] = converted;
                            foundLink = true;
                            break;
                        }
                    }
                }
                if (!foundLink) {
                    std::string defVal = (inPort.type == PinType::Vec2 && inPort.defaultToMeshUV)
                        ? "vTexCoords"
                        : FormatGLSLValue(inPort.type, inPort.defaultValue);
                    inExpr[inPort.name] = defVal;
                    inExpr[inPort.id] = defVal;
                }
            }

            std::unordered_map<std::string, std::string> outExpr;
            std::string statements;
            node->GenerateGLSL(inExpr, outExpr, statements);

            if (!statements.empty()) {
                ss << statements;
            }

            for (const auto& outPort : node->outputs) {
                std::string expr;
                if (outExpr.count(outPort.name)) expr = outExpr[outPort.name];
                else if (outExpr.count(outPort.id)) expr = outExpr[outPort.id];
                else expr = FormatGLSLValue(outPort.type, outPort.defaultValue);

                std::string varName = "n_" + SanitizeIdentifier(node->id) + "_" + SanitizeIdentifier(outPort.name);
                std::string typeName = GetPinTypeGLSLName(outPort.type);
                ss << "    " << typeName << " " << varName << " = " << expr << ";\n";

                evaluatedOutputs[node->id + "_" + outPort.id] = varName;
                evaluatedOutputs[node->id + "_" + outPort.name] = varName;
            }
        }

        if (isLit) {
            ss << "\n    // --- Master Node Surface Outputs ---\n"
               << "    vec3 albedo = " << masterAlbedoExpr << ";\n"
               << "    vec3 rawNormal = " << masterNormalExpr << ";\n"
               << "    float roughness = clamp(" << masterRoughnessExpr << ", 0.04, 1.0);\n"
               << "    float metallic = clamp(" << masterMetallicExpr << ", 0.0, 1.0);\n"
               << "    vec3 emissive = " << masterEmissiveExpr << ";\n"
               << "    float ao = clamp(" << masterAOExpr << ", 0.0, 1.0);\n"
               << "    float alpha = clamp(" << masterAlphaExpr << ", 0.0, 1.0);\n"
               << "    float alphaClipThreshold = " << masterAlphaClipExpr << ";\n\n"
               << "    if (alpha < alphaClipThreshold) discard;\n\n"
               << "    vec3 geomN = (length(vNormal) > 0.001) ? normalize(vNormal) : vec3(0.0, 1.0, 0.0);\n"
               << "    vec3 N = geomN;\n"
               << "    if (length(rawNormal) > 0.001) {\n"
               << "        vec3 T = vTangent;\n"
               << "        vec3 B = vBitangent;\n"
               << "        if (dot(T, T) > 0.0001 && dot(B, B) > 0.0001) {\n"
               << "            T = normalize(T - dot(T, geomN) * geomN);\n"
               << "            B = normalize(B - dot(B, geomN) * geomN - dot(B, T) * T);\n"
               << "            if (dot(cross(geomN, T), B) < 0.0) B = -B;\n"
               << "            mat3 TBN = mat3(T, B, geomN);\n"
               << "            N = normalize(TBN * rawNormal);\n"
               << "        } else {\n"
               << "            vec3 dp1 = dFdx(vWorldPos);\n"
               << "            vec3 dp2 = dFdy(vWorldPos);\n"
               << "            vec2 duv1 = dFdx(vTexCoords);\n"
               << "            vec2 duv2 = dFdy(vTexCoords);\n"
               << "            vec3 dp2perp = cross(dp2, geomN);\n"
               << "            vec3 dp1perp = cross(geomN, dp1);\n"
               << "            T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
               << "            B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
               << "            float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 0.00001);\n"
               << "            mat3 TBN = mat3(T * invmax, B * invmax, geomN);\n"
               << "            N = normalize(TBN * rawNormal);\n"
               << "        }\n"
               << "    }\n\n"
               << "    vec3 V = normalize(u_CameraPos - vWorldPos);\n"
               << "    vec3 F0 = mix(vec3(0.04), albedo, metallic);\n"
               << "    vec3 Lo = vec3(0.0);\n\n"
               << "    // Directional Lights Loop\n"
               << "    for (int i = 0; i < u_NumDirLights && i < MAX_DIR_LIGHTS; ++i) {\n"
               << "        vec3 L = normalize(-u_DirLights[i].direction);\n"
               << "        vec3 H = normalize(V + L);\n"
               << "        vec3 radiance = u_DirLights[i].color * u_DirLights[i].intensity;\n"
               << "        float NDF = DistributionGGX(N, H, roughness);\n"
               << "        float G = GeometrySmith(N, V, L, roughness);\n"
               << "        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
               << "        vec3 numerator = NDF * G * F;\n"
               << "        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
               << "        vec3 specular = numerator / denominator;\n"
               << "        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
               << "        float NdotL = max(dot(N, L), 0.0);\n"
               << "        float shadowFactor = (i == 0) ? CalculateShadowCSM(vWorldPos, N, L) : 0.0;\n"
               << "        Lo += (kD * albedo / PI + specular) * radiance * NdotL * (1.0 - shadowFactor);\n"
               << "    }\n\n"
               << "    if (u_ClusteredLightingEnabled) {\n"
               << "        uint clusterX = uint(clamp(gl_FragCoord.x / max(u_ClusterTileSize.x, 1.0), 0.0, float(u_ClusterGridDim.x - 1u)));\n"
               << "        uint clusterY = uint(clamp(gl_FragCoord.y / max(u_ClusterTileSize.y, 1.0), 0.0, float(u_ClusterGridDim.y - 1u)));\n"
               << "        float viewZ = -vViewPos.z;\n"
               << "        uint clusterZ = 0u;\n"
               << "        if (viewZ > u_ClusterZNear && u_ClusterLogZFactor > 1e-5) {\n"
               << "            clusterZ = uint(clamp(log(viewZ / u_ClusterZNear) / u_ClusterLogZFactor, 0.0, float(u_ClusterGridDim.z - 1u)));\n"
               << "        }\n"
               << "        uint clusterIdx = clusterX + clusterY * u_ClusterGridDim.x + clusterZ * u_ClusterGridDim.x * u_ClusterGridDim.y;\n"
               << "        uvec2 clusterData = u_ClusterGrid[clusterIdx];\n"
               << "        uint clusterOffset = clusterData.x;\n"
               << "        uint clusterCount  = clusterData.y;\n"
               << "        for (uint ci = 0u; ci < clusterCount; ++ci) {\n"
               << "            uint lightIdx = u_GlobalLightIndexList[clusterOffset + ci];\n"
               << "            GpuClusteredLight cLight = u_Lights[lightIdx];\n"
               << "            uint lType = cLight.params.x;\n"
               << "            if (lType >= 2u) continue;\n"
               << "            vec3 lightPos = cLight.position_range.xyz;\n"
               << "            float lightRange = cLight.position_range.w;\n"
               << "            vec3 lightCol = cLight.color_intensity.xyz;\n"
               << "            float lightInt = cLight.color_intensity.w;\n"
               << "            vec3 lightDir = lightPos - vWorldPos;\n"
               << "            float dist = length(lightDir);\n"
               << "            if (dist > lightRange && lightRange > 0.0) continue;\n"
               << "            vec3 L = normalize(lightDir);\n"
               << "            vec3 H = normalize(V + L);\n"
               << "            float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
               << "            if (lightRange > 0.0) attenuation *= clamp(1.0 - (dist / lightRange), 0.0, 1.0);\n"
               << "            float spotFactor = 1.0;\n"
               << "            if (lType == 1u) {\n"
               << "                float theta = dot(L, normalize(-cLight.direction_spotAngle.xyz));\n"
               << "                float spotAngle = cLight.direction_spotAngle.w;\n"
               << "                float innerCut = cos(radians(spotAngle * 0.5));\n"
               << "                float outerCut = cos(radians(spotAngle * 0.65));\n"
               << "                spotFactor = clamp((theta - outerCut) / max(innerCut - outerCut, 0.0001), 0.0, 1.0);\n"
               << "                if (spotFactor <= 0.0) continue;\n"
               << "            }\n"
               << "            vec3 radiance = lightCol * lightInt * attenuation * spotFactor;\n"
               << "            float NDF = DistributionGGX(N, H, roughness);\n"
               << "            float G   = GeometrySmith(N, V, L, roughness);\n"
               << "            vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
               << "            vec3 numerator = NDF * G * F;\n"
               << "            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
               << "            vec3 specular = numerator / denominator;\n"
               << "            vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
               << "            float NdotL = max(dot(N, L), 0.0);\n"
               << "            Lo += (kD * albedo / PI + specular) * radiance * NdotL;\n"
               << "        }\n"
               << "    } else {\n"
               << "    // Point Lights Loop\n"
               << "    for (int i = 0; i < u_NumPointLights && i < MAX_POINT_LIGHTS; ++i) {\n"
               << "        vec3 lightDir = u_PointLights[i].position - vWorldPos;\n"
               << "        float dist = length(lightDir);\n"
               << "        if (dist > u_PointLights[i].range && u_PointLights[i].range > 0.0) continue;\n"
               << "        vec3 L = normalize(lightDir);\n"
               << "        vec3 H = normalize(V + L);\n"
               << "        float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
               << "        if (u_PointLights[i].range > 0.0) attenuation *= clamp(1.0 - (dist / u_PointLights[i].range), 0.0, 1.0);\n"
               << "        vec3 radiance = u_PointLights[i].color * u_PointLights[i].intensity * attenuation;\n"
               << "        float NDF = DistributionGGX(N, H, roughness);\n"
               << "        float G = GeometrySmith(N, V, L, roughness);\n"
               << "        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
               << "        vec3 numerator = NDF * G * F;\n"
               << "        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
               << "        vec3 specular = numerator / denominator;\n"
               << "        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
               << "        float NdotL = max(dot(N, L), 0.0);\n"
               << "        Lo += (kD * albedo / PI + specular) * radiance * NdotL;\n"
               << "    }\n"
               << "    }\n\n"
               << "    if (u_HasLightmap) {\n"
               << "        vec2 lmUV = vTexCoords2 * u_LightmapScaleOffset.xy + u_LightmapScaleOffset.zw;\n"
               << "        vec3 bakedIrr = texture(u_LightmapAtlas, lmUV).rgb;\n"
               << "        Lo += bakedIrr * albedo * (1.0 - metallic);\n"
               << "    }\n\n"
               << "    // Spot Lights Loop\n"
               << "    for (int i = 0; i < u_NumSpotLights && i < MAX_SPOT_LIGHTS; ++i) {\n"
               << "        vec3 lightDir = u_SpotLights[i].position - vWorldPos;\n"
               << "        float dist = length(lightDir);\n"
               << "        if (dist > u_SpotLights[i].range && u_SpotLights[i].range > 0.0) continue;\n"
               << "        vec3 L = normalize(lightDir);\n"
               << "        float theta = dot(L, normalize(-u_SpotLights[i].direction));\n"
               << "        float innerCut = cos(radians(u_SpotLights[i].spotAngle * 0.5));\n"
               << "        float outerCut = cos(radians(u_SpotLights[i].spotAngle * 0.65));\n"
               << "        float spotFactor = clamp((theta - outerCut) / max(innerCut - outerCut, 0.0001), 0.0, 1.0);\n"
               << "        if (spotFactor <= 0.0) continue;\n"
               << "        vec3 H = normalize(V + L);\n"
               << "        float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * (dist * dist));\n"
               << "        vec3 radiance = u_SpotLights[i].color * u_SpotLights[i].intensity * attenuation * spotFactor;\n"
               << "        float NDF = DistributionGGX(N, H, roughness);\n"
               << "        float G = GeometrySmith(N, V, L, roughness);\n"
               << "        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);\n"
               << "        vec3 numerator = NDF * G * F;\n"
               << "        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;\n"
               << "        vec3 specular = numerator / denominator;\n"
               << "        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);\n"
               << "        float NdotL = max(dot(N, L), 0.0);\n"
               << "        Lo += (kD * albedo / PI + specular) * radiance * NdotL;\n"
               << "    }\n"
               << "    }\n\n"
               << "    if (u_HasLightmap) {\n"
               << "        vec2 lmUV = vTexCoords2 * u_LightmapScaleOffset.xy + u_LightmapScaleOffset.zw;\n"
               << "        vec3 bakedIrr = texture(u_LightmapAtlas, lmUV).rgb;\n"
               << "        Lo += bakedIrr * albedo * (1.0 - metallic);\n"
               << "    }\n\n"
               << "    // Indirect Ambient & DDGI\n"
               << "    float hemiAmbient = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);\n"
               << "    vec3 ambient = mix(vec3(0.08, 0.085, 0.095), vec3(0.24, 0.27, 0.32), hemiAmbient) * albedo * ao;\n"
               << "    if (u_DDGIEnabled) {\n"
               << "        vec3 irr = SampleDDGI(vWorldPos, N);\n"
               << "        ambient = vec3(1.0 - metallic) * irr * albedo * ao;\n"
               << "    }\n\n"
               << "    vec3 color = ambient + Lo + emissive;\n\n"
               << "    // Environment Reflection Cubemap\n"
               << "    if (u_HasEnvCube) {\n"
               << "        vec3 R = reflect(-V, N);\n"
               << "        vec3 envColor = textureLod(u_EnvCube, R, roughness * 6.0).rgb;\n"
               << "        vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);\n"
               << "        float roughLimit = max(u_ReflectionRoughnessCutoff, 0.05);\n"
               << "        float roughFade = smoothstep(roughLimit, roughLimit * 0.15, roughness);\n"
               << "        vec3 envSpecular = envColor * F * roughFade;\n"
               << "        color += envSpecular * max(u_EnvIntensity, 0.0);\n"
               << "    }\n\n"
               << "    FragColor = vec4(max(color, vec3(0.0)), alpha);\n"
               << "    FragGeometry = vec4(DDGIOctEncode(N), roughness, metallic);\n"
               << "}\n";
        } else {
            ss << "\n    // --- Master Unlit Surface Outputs ---\n"
               << "    vec3 color = " << masterColorExpr << ";\n"
               << "    vec3 emissive = " << masterEmissiveExpr << ";\n"
               << "    float alpha = clamp(" << masterAlphaExpr << ", 0.0, 1.0);\n"
               << "    float alphaClipThreshold = " << masterAlphaClipExpr << ";\n\n"
               << "    if (alpha < alphaClipThreshold) discard;\n\n"
               << "    vec3 finalColor = color + emissive;\n"
               << "    FragColor = vec4(finalColor, alpha);\n"
               << "}\n";
        }

        return ss.str();
    }

    GeneratedShaderCode ShaderGraphCompiler::Compile(
        const ShaderGraph& graph,
        const ShaderCompilerOptions& options
    ) {
        GeneratedShaderCode result;

        MasterNode* master = LocateMasterNode(graph);
        if (!master) {
            result.success = false;
            result.errorMessage = "ShaderGraphCompiler Error: Graph does not contain a Master Output node.";
            return result;
        }

        const_cast<ShaderGraph&>(graph).materialType = master->materialType;

        std::unordered_set<std::string> reachableNodes = CollectReachableNodes(graph, master);

        std::string cycleDiagnostic;
        if (DetectCycle(graph, reachableNodes, &cycleDiagnostic)) {
            result.success = false;
            result.errorMessage = cycleDiagnostic.empty() ? "ShaderGraphCompiler Error: Cycle detected in graph DAG topology." : cycleDiagnostic;
            return result;
        }

        bool hasCycle = false;
        std::vector<std::string> executionOrder = ComputeTopologicalOrder(graph, reachableNodes, hasCycle);
        if (hasCycle) {
            result.success = false;
            result.errorMessage = "ShaderGraphCompiler Error: Topological sort failed due to cycle.";
            return result;
        }
        result.executionOrder = executionOrder;

        result.vertexSource = EmitVertexShader(graph, options, reachableNodes, executionOrder);

        result.fragmentSource = EmitFragmentShader(
            graph, options, reachableNodes, executionOrder,
            result.textureBindings, result.exposedUniformNames
        );

        result.success = true;
        return result;
    }

    GeneratedShaderCode ShaderGraphCompiler::CompileNodePreview(
        const ShaderGraph& graph,
        const std::string& nodeId,
        const std::string& outputPortId,
        const ShaderCompilerOptions& options
    ) {
        GeneratedShaderCode result;
        result.success = false;
        result.errorMessage = "ShaderGraphCompiler Preview Error: node not found (" + nodeId + ").";

        // Clon del grafo (sin mutar el original): se omiten los Master del grafo
        // fuente para conservar un unico Master servido por el grafo clonado.
        ShaderGraph clone;
        for (const auto& node : graph.nodes) {
            if (!node) continue;
            if (dynamic_cast<MasterNode*>(node.get()) != nullptr) continue;
            clone.nodes.push_back(node->Clone());
        }
        clone.links = graph.links;
        clone.properties = graph.properties;

        ShaderNode* cloneNode = clone.FindNode(nodeId);
        if (!cloneNode) return result;
        if (cloneNode->outputs.empty()) {
            result.errorMessage = "ShaderGraphCompiler Preview Error: node has no output ports.";
            return result;
        }

        // Puerto de salida a evaluar (por defecto el primero)
        std::string outPort = outputPortId;
        if (outPort.empty()) outPort = cloneNode->outputs[0].id;

        // El Master se fuerza a Unlit y la salida del nodo se enlaza a Color
        // (el compilador aplica la conversion de tipos automaticamente).
        MasterNode* master = clone.GetMasterNode();
        if (!master) {
            result.errorMessage = "ShaderGraphCompiler Preview Error: no master node in preview clone.";
            return result;
        }
        master->SetMaterialType(ShaderGraphType::Unlit);
        clone.materialType = ShaderGraphType::Unlit;
        clone.AddLink(cloneNode->id, outPort, master->id, "Color");

        return Compile(clone, options);
    }

} // namespace SlipShaderGraph

bool SlipCompileShaderGraphFile(
    const std::string& graphPath,
    std::string& outVertexSrc,
    std::string& outFragmentSrc,
    std::string& outShaderType,
    std::vector<std::pair<std::string, float>>* outFloatProps,
    std::vector<std::pair<std::string, glm::vec4>>* outVectorProps,
    std::vector<std::pair<std::string, std::string>>* outTextureProps,
    std::string* outErrorMessage
) {
    std::string resolved = SlipShaderGraph::ShaderGraphSerializer::ResolveGraphPath(graphPath);
    SlipShaderGraph::ShaderGraph graph;
    std::string err;
    if (!SlipShaderGraph::ShaderGraphSerializer::DeserializeFromFile(graph, resolved, &err)) {
        if (outErrorMessage) *outErrorMessage = err.empty() ? ("Failed to deserialize graph: " + resolved) : err;
        return false;
    }

    SlipShaderGraph::ShaderCompilerOptions options;
    SlipShaderGraph::GeneratedShaderCode code = SlipShaderGraph::ShaderGraphCompiler::Compile(graph, options);
    if (!code.success) {
        if (outErrorMessage) *outErrorMessage = code.errorMessage;
        return false;
    }

    outVertexSrc = code.vertexSource;
    outFragmentSrc = code.fragmentSource;
    outShaderType = (graph.materialType == SlipShaderGraph::ShaderGraphType::Unlit) ? "Unlit" : "Lit";

    if (outFloatProps || outVectorProps || outTextureProps) {
        for (const auto& prop : graph.properties) {
            std::string uName = prop.GetUniformName();
            if (prop.type == SlipShaderGraph::PinType::Float || prop.type == SlipShaderGraph::PinType::Boolean) {
                if (outFloatProps) {
                    outFloatProps->push_back({prop.name, prop.defaultValue.x});
                    outFloatProps->push_back({uName, prop.defaultValue.x});
                }
            } else if (prop.type == SlipShaderGraph::PinType::Vec2 || prop.type == SlipShaderGraph::PinType::Vec3 || prop.type == SlipShaderGraph::PinType::Vec4) {
                if (outVectorProps) {
                    outVectorProps->push_back({prop.name, prop.defaultValue});
                    outVectorProps->push_back({uName, prop.defaultValue});
                }
            } else if (prop.type == SlipShaderGraph::PinType::Texture2D) {
                if (outTextureProps) {
                    outTextureProps->push_back({prop.name, prop.defaultTexturePath});
                    outTextureProps->push_back({uName, prop.defaultTexturePath});
                }
            }
        }
    }

    return true;
}

