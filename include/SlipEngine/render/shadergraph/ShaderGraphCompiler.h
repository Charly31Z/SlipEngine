#pragma once

#ifndef SLIP_SHADER_GRAPH_COMPILER_H
#define SLIP_SHADER_GRAPH_COMPILER_H

#include "SlipEngine/core/SlipExport.h"
#include "ShaderGraphTypes.h"
#include "ShaderGraph.h"
#include "ShaderNode.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace SlipShaderGraph {

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

        // Compiles a preview shader that evaluates the given node's output port
        // (used by the editor to render per-node previews). Does not modify the input graph.
        static GeneratedShaderCode CompileNodePreview(
            const ShaderGraph& graph,
            const std::string& nodeId,
            const std::string& outputPortId = "",
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
        static bool DetectCycle(const ShaderGraph& graph);
        static std::vector<std::string> ComputeTopologicalOrder(
            const ShaderGraph& graph,
            const std::unordered_set<std::string>& reachableNodes,
            bool& outHasCycle
        );
        static std::vector<std::string> TopologicalSort(
            const ShaderGraph& graph,
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

} // namespace SlipShaderGraph

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Global bridge function for SlipMaterial / legacy engine integration
SLIPENGINE_API bool SlipCompileShaderGraphFile(
    const std::string& graphPath,
    std::string& outVertexSrc,
    std::string& outFragmentSrc,
    std::string& outShaderType,
    std::vector<std::pair<std::string, float>>* outFloatProps = nullptr,
    std::vector<std::pair<std::string, glm::vec4>>* outVectorProps = nullptr,
    std::vector<std::pair<std::string, std::string>>* outTextureProps = nullptr,
    std::string* outErrorMessage = nullptr
);

#endif // SLIP_SHADER_GRAPH_COMPILER_H
