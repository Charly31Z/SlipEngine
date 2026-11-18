#pragma once

#ifndef SLIP_SHADER_GRAPH_SERIALIZER_H
#define SLIP_SHADER_GRAPH_SERIALIZER_H

#include "SlipEngine/core/SlipExport.h"
#include <string>

namespace SlipShaderGraph {

    class ShaderGraph;

    class SLIPENGINE_API ShaderGraphSerializer {
    public:
        // Current file schema version
        static constexpr int CURRENT_SCHEMA_VERSION = 1;

        // Serialization
        static bool SerializeToString(const ShaderGraph& graph, std::string& outJson);
        static bool SerializeToFile(const ShaderGraph& graph, const std::string& filepath);
        static bool SaveToFile(const ShaderGraph& graph, const std::string& filepath) {
            return SerializeToFile(graph, filepath);
        }

        // Deserialization
        static bool DeserializeFromString(ShaderGraph& outGraph,
                                          const std::string& json,
                                          std::string* outErrorMessage = nullptr);
        static bool DeserializeFromFile(ShaderGraph& outGraph,
                                        const std::string& filepath,
                                        std::string* outErrorMessage = nullptr);
        static bool LoadFromFile(ShaderGraph& outGraph,
                                 const std::string& filepath,
                                 std::string* outErrorMessage = nullptr) {
            return DeserializeFromFile(outGraph, filepath, outErrorMessage);
        }

        // Asset Resolution
        static std::string ResolveGraphPath(const std::string& relativeOrFullPath);
    };

} // namespace SlipShaderGraph

#endif // SLIP_SHADER_GRAPH_SERIALIZER_H
