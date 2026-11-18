#pragma once

#ifndef SLIP_SHADER_GRAPH_TYPES_H
#define SLIP_SHADER_GRAPH_TYPES_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace SlipShaderGraph {

    enum class ShaderGraphType {
        Lit,
        Unlit
    };

    enum class PinType {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Texture2D,
        Matrix4,
        Boolean
    };

    // Helper functions for PinType conversions
    SLIPENGINE_API const char* PinTypeToString(PinType type);
    SLIPENGINE_API PinType StringToPinType(const std::string& str);
    SLIPENGINE_API const char* GetPinTypeGLSLName(PinType type);
    SLIPENGINE_API int GetPinTypeDimension(PinType type);
    SLIPENGINE_API bool ArePinsCompatible(PinType fromType, PinType toType);
    SLIPENGINE_API std::string FormatGLSLValue(PinType type, const glm::vec4& val);
    SLIPENGINE_API std::string ConvertGLSLExpression(PinType fromType, PinType toType, const std::string& expr);

    // Helper functions for ShaderGraphType
    SLIPENGINE_API const char* ShaderGraphTypeToString(ShaderGraphType type);
    SLIPENGINE_API ShaderGraphType StringToShaderGraphType(const std::string& str);

    struct SLIPENGINE_API ShaderProperty {
        std::string id;
        std::string name;
        std::string displayName;
        PinType type = PinType::Float;
        glm::vec4 defaultValue = glm::vec4(0.0f);
        std::string defaultTexturePath;

        ShaderProperty() = default;
        ShaderProperty(const std::string& inId, const std::string& inName, PinType inType,
                       const glm::vec4& inDefault = glm::vec4(0.0f),
                       const std::string& inDisplay = "", const std::string& inTexPath = "")
            : id(inId), name(inName),
              displayName(inDisplay.empty() ? inName : inDisplay),
              type(inType), defaultValue(inDefault),
              defaultTexturePath(inTexPath) {}

        std::string GetUniformName() const {
            return "u_" + name;
        }
    };

    struct SLIPENGINE_API ShaderPort {
        std::string id;
        std::string name;
        PinType type = PinType::Float;
        bool isOutput = false;
        glm::vec4 defaultValue = glm::vec4(0.0f);
        std::string defaultTexturePath;
        // Si true, un input sin conectar se evalua con vTexCoords (UV del mesh)
        // en lugar de defaultValue. Usado por puertos UV estilo Unreal/Unity.
        bool defaultToMeshUV = false;

        ShaderPort() = default;
        ShaderPort(const std::string& inId, const std::string& inName, PinType inType,
                   bool inIsOutput = false, const glm::vec4& inDefault = glm::vec4(0.0f),
                   const std::string& inTexPath = "")
            : id(inId), name(inName), type(inType), isOutput(inIsOutput),
              defaultValue(inDefault), defaultTexturePath(inTexPath) {}
    };

    struct SLIPENGINE_API ShaderLink {
        std::string id;
        std::string fromNodeId;
        std::string fromPortId;
        std::string toNodeId;
        std::string toPortId;

        ShaderLink() = default;
        ShaderLink(const std::string& inId, const std::string& fromN, const std::string& fromP,
                   const std::string& toN, const std::string& toP)
            : id(inId), fromNodeId(fromN), fromPortId(fromP), toNodeId(toN), toPortId(toP) {}
    };

} // namespace SlipShaderGraph

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // SLIP_SHADER_GRAPH_TYPES_H
