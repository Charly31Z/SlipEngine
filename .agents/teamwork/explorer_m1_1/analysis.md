# Analysis Report: Shader Graph Model & Node Library Architecture

**Author**: `explorer_m1_1`  
**Milestone**: M1 (Core Model, Node Library & Serialization)  
**Date**: 2026-11-08  
**Working Directory**: `c:\Projects\SlipEngine\.agents\teamwork\explorer_m1_1\`

---

## 1. Executive Summary

This document presents the complete architectural design and production-ready C++ specifications for SlipEngine's **Shader Graph Model & Node Library** (Milestone 1, Features F01–F07, F13).

The design introduces:
1. **`ShaderGraphTypes.h`**: Core data types (`PinType`, `ShaderGraphType`, `ShaderProperty`, `ShaderPort`, `ShaderLink`) with type compatibility, scalar promotion rules, string conversions, and GLSL formatting helpers.
2. **`ShaderNode.h` / `ShaderNode.cpp`**: An extensible, polymorphic `ShaderNode` hierarchy supporting 35+ production nodes across 6 categories:
   - **Math**: Add, Subtract, Multiply, Divide, Sin, Cos, Tan, Dot, Cross, Normalize, Length, Distance.
   - **Procedural**: Min, Max, Clamp, Lerp, Abs, Step, Smoothstep, Remap, Procedural Noise (gradient), Procedural Voronoi (cellular).
   - **Time**: Time (with `u_Time`, `sin(Time)`, `cos(Time)`), DeltaTime (`u_DeltaTime`).
   - **Camera / Transform**: CameraPosition, ViewMatrix, ProjectionMatrix, WorldPosition, WorldNormal.
   - **Texture**: Texture2D Sampler, UV Coordinates, Tiling & Offset.
   - **Inputs / Parameters**: PropertyNode (referencing Blackboard public variables), Constants (Float, Vec2, Vec3, Vec4), Vector Split, Vector Combine.
   - **Master Output**: `MasterNode` supporting dynamic toggling between **Lit** (PBR inputs: Albedo, Normal, Roughness, Metallic, Emissive, AO, Alpha, Clip Threshold) and **Unlit** (Direct Color, Emissive, Alpha, Clip Threshold).
3. **`ShaderNodeRegistry`**: A thread-safe node factory and descriptor registry allowing dynamic instantiation by `typeName` (crucial for serializer deserialization) and catalog queries for the editor menu palette (Milestone 3).
4. **`ShaderGraph.h` / `ShaderGraph.cpp`**: The central graph container managing nodes, links, blackboard properties, cycle detection, and high-level graph manipulation.

---

## 2. Core Architectural Design

### 2.1 Type System & Pin Compatibility

In a node-based shader graph, pins represent data flow between shader operations. SlipEngine defines five fundamental data types plus matrix and boolean helpers:

```cpp
namespace SlipEngine {
    enum class PinType {
        Float,      // 32-bit floating point scalar (GLSL: float)
        Vec2,       // 2D vector (GLSL: vec2)
        Vec3,       // 3D vector (GLSL: vec3)
        Vec4,       // 4D vector / RGBA color (GLSL: vec4)
        Texture2D,  // 2D texture sampler handle (GLSL: sampler2D)
        Matrix4,    // 4x4 matrix (GLSL: mat4)
        Boolean     // boolean flag (GLSL: bool)
    };
}
```

#### Pin Compatibility & Swizzle/Promotion Matrix

To match user expectations from modern editors (Unity Shader Graph, Unreal Engine Material Editor), pins are compatible according to these rules:

| Output Type (Source) | Input Type (Target) | Compatible? | GLSL Auto-Conversion Rule |
|:---|:---|:---:|:---|
| **Float** | Float | Yes | Direct assignment: `val` |
| **Float** | Vec2 | Yes | Scalar promotion: `vec2(val)` |
| **Float** | Vec3 | Yes | Scalar promotion: `vec3(val)` |
| **Float** | Vec4 | Yes | Scalar promotion: `vec4(val)` |
| **Vec2** | Float | Yes | Component truncation: `val.x` |
| **Vec2** | Vec2 | Yes | Direct assignment: `val` |
| **Vec2** | Vec3 | Yes | Zero extension: `vec3(val, 0.0)` |
| **Vec2** | Vec4 | Yes | Extension: `vec4(val, 0.0, 1.0)` |
| **Vec3** | Float | Yes | Component truncation: `val.x` |
| **Vec3** | Vec2 | Yes | Truncation: `val.xy` |
| **Vec3** | Vec3 | Yes | Direct assignment: `val` |
| **Vec3** | Vec4 | Yes | Extension: `vec4(val, 1.0)` |
| **Vec4** | Float | Yes | Component truncation: `val.x` |
| **Vec4** | Vec2 | Yes | Truncation: `val.xy` |
| **Vec4** | Vec3 | Yes | Truncation: `val.xyz` |
| **Vec4** | Vec4 | Yes | Direct assignment: `val` |
| **Texture2D** | Texture2D | Yes | Direct sampler handle pass-through |
| **Matrix4** | Matrix4 | Yes | Direct matrix pass-through |
| *Others* | *Others* | No | Incompatible (e.g. Texture2D cannot connect to Float) |

This auto-conversion logic is encapsulated in `ArePinsCompatible(PinType from, PinType to)` and `ConvertGLSLExpression(PinType from, PinType to, const std::string& expr)`.

---

### 2.2 Master Output Node Design (Lit vs. Unlit)

The Master Output Node acts as the root terminal of the Shader Graph.
When the graph's `materialType` changes between `ShaderGraphType::Lit` and `ShaderGraphType::Unlit`, the `MasterNode` dynamically updates its input ports:

#### Lit Configuration (PBR GGX Pipeline):
- `Albedo` (Vec3, Default: `vec3(1.0, 1.0, 1.0)`)
- `Normal` (Vec3, Default: `vec3(0.0, 0.0, 1.0)` / Tangent space normal)
- `Roughness` (Float, Default: `0.5`)
- `Metallic` (Float, Default: `0.0`)
- `Emissive` (Vec3, Default: `vec3(0.0, 0.0, 0.0)`)
- `AO` (Float, Default: `1.0`)
- `Alpha` (Float, Default: `1.0`)
- `AlphaClipThreshold` (Float, Default: `0.5`)

#### Unlit Configuration:
- `Color` (Vec3, Default: `vec3(1.0, 1.0, 1.0)`)
- `Emissive` (Vec3, Default: `vec3(0.0, 0.0, 0.0)`)
- `Alpha` (Float, Default: `1.0`)
- `AlphaClipThreshold` (Float, Default: `0.5`)

When switching modes, existing connections to common ports (e.g. `Emissive`, `Alpha`, `AlphaClipThreshold`) are preserved; connections to removed ports are cleanly detached without corrupting the graph.

---

### 2.3 Blackboard Public Properties (`ShaderProperty`)

Exposed parameters allow materials using a `.shadergraph` to customize values (roughness, tint colors, speed, textures) per material instance in the Inspector:
- `id`: Unique GUID string (e.g., `"prop_albedo_tint"`).
- `name`: Clean identifier used for the uniform in GLSL (e.g., `"AlbedoTint"` -> `u_AlbedoTint`).
- `displayName`: UI label shown in the editor Inspector.
- `type`: `PinType` (`Float`, `Vec2`, `Vec3`, `Vec4`, `Texture2D`).
- `defaultValue`: `glm::vec4` storing default numeric values.
- `defaultTexturePath`: Path string if type is `Texture2D`.

Inside the graph canvas, users place a `PropertyNode` referencing `propertyId`. The `PropertyNode` outputs the current value of the uniform `u_<name>`.

---

## 3. Concrete Code Skeletons

### 3.1 `include/SlipEngine/render/shadergraph/ShaderGraphTypes.h`

```cpp
#pragma once

#ifndef SLIP_SHADER_GRAPH_TYPES_H
#define SLIP_SHADER_GRAPH_TYPES_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace SlipEngine {

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

        ShaderPort() = default;
        ShaderPort(const std::string& inId, const std::string& inName, PinType inType, bool inIsOutput = false, const glm::vec4& inDefault = glm::vec4(0.0f))
            : id(inId), name(inName), type(inType), isOutput(inIsOutput), defaultValue(inDefault) {}
    };

    struct SLIPENGINE_API ShaderLink {
        std::string id;
        std::string fromNodeId;
        std::string fromPortId;
        std::string toNodeId;
        std::string toPortId;

        ShaderLink() = default;
        ShaderLink(const std::string& inId, const std::string& fromN, const std::string& fromP, const std::string& toN, const std::string& toP)
            : id(inId), fromNodeId(fromN), fromPortId(fromP), toNodeId(toN), toPortId(toP) {}
    };

} // namespace SlipEngine

#endif // SLIP_SHADER_GRAPH_TYPES_H
```

---

### 3.2 `include/SlipEngine/render/shadergraph/ShaderNode.h`

```cpp
#pragma once

#ifndef SLIP_SHADER_NODE_H
#define SLIP_SHADER_NODE_H

#include "ShaderGraphTypes.h"
#include <memory>
#include <functional>

namespace SlipEngine {

    class SLIPENGINE_API ShaderNode {
    public:
        std::string id;
        std::string typeName;
        std::string title;
        std::string category;
        float posX = 0.0f;
        float posY = 0.0f;

        std::vector<ShaderPort> inputs;
        std::vector<ShaderPort> outputs;
        std::unordered_map<std::string, std::string> customProperties;

        virtual ~ShaderNode() = default;

        // Port finders
        const ShaderPort* FindInput(const std::string& portId) const;
        ShaderPort* FindInput(const std::string& portId);
        const ShaderPort* FindOutput(const std::string& portId) const;
        ShaderPort* FindOutput(const std::string& portId);

        // Port helper additions
        ShaderPort& AddInput(const std::string& portId, const std::string& name, PinType type, const glm::vec4& defaultVal = glm::vec4(0.0f));
        ShaderPort& AddOutput(const std::string& portId, const std::string& name, PinType type);

        // Virtual lifecycle and dynamic reconfiguration
        virtual void UpdatePorts() {}
        virtual void OnCustomPropertiesUpdated() {}
        virtual std::unique_ptr<ShaderNode> Clone() const = 0;

        // Code Generation Interface for M2
        // inputExpressions: maps inputPortId -> valid GLSL expression string
        // outputExpressions: maps outputPortId -> generated GLSL variable name
        // statements: code block lines to be prepended in shader body
        virtual void GenerateGLSL(
            const std::unordered_map<std::string, std::string>& inputExpressions,
            std::unordered_map<std::string, std::string>& outputExpressions,
            std::string& statements
        ) const;
    };

    // Node registry descriptor for UI menus and deserialization
    struct NodeDescriptor {
        std::string typeName;
        std::string title;
        std::string category;
        std::string description;
        std::function<std::unique_ptr<ShaderNode>()> factory;
    };

    class SLIPENGINE_API ShaderNodeRegistry {
    public:
        static ShaderNodeRegistry& Get();

        void Register(const NodeDescriptor& desc);

        template<typename T>
        void RegisterNodeType(const std::string& typeName, const std::string& title, const std::string& category, const std::string& desc = "") {
            Register({
                typeName,
                title,
                category,
                desc,
                [typeName, title, category]() -> std::unique_ptr<ShaderNode> {
                    auto node = std::make_unique<T>();
                    node->typeName = typeName;
                    node->title = title;
                    node->category = category;
                    return node;
                }
            });
        }

        std::unique_ptr<ShaderNode> CreateNode(const std::string& typeName) const;
        const std::vector<NodeDescriptor>& GetRegisteredNodes() const;
        const NodeDescriptor* FindDescriptor(const std::string& typeName) const;

        void InitializeDefaultNodes();

    private:
        ShaderNodeRegistry();
        ~ShaderNodeRegistry() = default;
        ShaderNodeRegistry(const ShaderNodeRegistry&) = delete;
        ShaderNodeRegistry& operator=(const ShaderNodeRegistry&) = delete;

        std::vector<NodeDescriptor> m_Descriptors;
        std::unordered_map<std::string, size_t> m_NameToIndex;
        bool m_Initialized = false;
    };

    // --- Concrete Math Nodes ---
    class SLIPENGINE_API MathAddNode : public ShaderNode {
    public:
        MathAddNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathAddNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathSubtractNode : public ShaderNode {
    public:
        MathSubtractNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathSubtractNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathMultiplyNode : public ShaderNode {
    public:
        MathMultiplyNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathMultiplyNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathDivideNode : public ShaderNode {
    public:
        MathDivideNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathDivideNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathSinNode : public ShaderNode {
    public:
        MathSinNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathSinNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathCosNode : public ShaderNode {
    public:
        MathCosNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathCosNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathTanNode : public ShaderNode {
    public:
        MathTanNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathTanNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathDotNode : public ShaderNode {
    public:
        MathDotNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathDotNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathCrossNode : public ShaderNode {
    public:
        MathCrossNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathCrossNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathNormalizeNode : public ShaderNode {
    public:
        MathNormalizeNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathNormalizeNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathLengthNode : public ShaderNode {
    public:
        MathLengthNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathLengthNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathDistanceNode : public ShaderNode {
    public:
        MathDistanceNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MathDistanceNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Procedural Nodes ---
    class SLIPENGINE_API ProceduralMinNode : public ShaderNode {
    public:
        ProceduralMinNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralMinNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralMaxNode : public ShaderNode {
    public:
        ProceduralMaxNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralMaxNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralClampNode : public ShaderNode {
    public:
        ProceduralClampNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralClampNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralLerpNode : public ShaderNode {
    public:
        ProceduralLerpNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralLerpNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralAbsNode : public ShaderNode {
    public:
        ProceduralAbsNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralAbsNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralStepNode : public ShaderNode {
    public:
        ProceduralStepNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralStepNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralSmoothstepNode : public ShaderNode {
    public:
        ProceduralSmoothstepNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralSmoothstepNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralRemapNode : public ShaderNode {
    public:
        ProceduralRemapNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralRemapNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralNoiseNode : public ShaderNode {
    public:
        ProceduralNoiseNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralNoiseNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralVoronoiNode : public ShaderNode {
    public:
        ProceduralVoronoiNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProceduralVoronoiNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Time Nodes ---
    class SLIPENGINE_API TimeNode : public ShaderNode {
    public:
        TimeNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<TimeNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API DeltaTimeNode : public ShaderNode {
    public:
        DeltaTimeNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<DeltaTimeNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Camera / Transform Nodes ---
    class SLIPENGINE_API CameraPositionNode : public ShaderNode {
    public:
        CameraPositionNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<CameraPositionNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ViewMatrixNode : public ShaderNode {
    public:
        ViewMatrixNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ViewMatrixNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProjectionMatrixNode : public ShaderNode {
    public:
        ProjectionMatrixNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<ProjectionMatrixNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API WorldPositionNode : public ShaderNode {
    public:
        WorldPositionNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<WorldPositionNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API WorldNormalNode : public ShaderNode {
    public:
        WorldNormalNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<WorldNormalNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Texture Nodes ---
    class SLIPENGINE_API TextureSamplerNode : public ShaderNode {
    public:
        TextureSamplerNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<TextureSamplerNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API UVNode : public ShaderNode {
    public:
        UVNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<UVNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API TilingAndOffsetNode : public ShaderNode {
    public:
        TilingAndOffsetNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<TilingAndOffsetNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Property / Constant / Vector Nodes ---
    class SLIPENGINE_API PropertyNode : public ShaderNode {
    public:
        PropertyNode();
        std::string propertyId;
        std::string propertyName;
        PinType propertyType = PinType::Float;

        void SetProperty(const ShaderProperty& prop);
        void UpdatePorts() override;
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<PropertyNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API FloatConstantNode : public ShaderNode {
    public:
        FloatConstantNode();
        float value = 0.0f;
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<FloatConstantNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API Vec2ConstantNode : public ShaderNode {
    public:
        Vec2ConstantNode();
        glm::vec2 value = glm::vec2(0.0f);
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<Vec2ConstantNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API Vec3ConstantNode : public ShaderNode {
    public:
        Vec3ConstantNode();
        glm::vec3 value = glm::vec3(0.0f);
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<Vec3ConstantNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API Vec4ConstantNode : public ShaderNode {
    public:
        Vec4ConstantNode();
        glm::vec4 value = glm::vec4(0.0f);
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<Vec4ConstantNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API SplitVectorNode : public ShaderNode {
    public:
        SplitVectorNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<SplitVectorNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API CombineVectorNode : public ShaderNode {
    public:
        CombineVectorNode();
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<CombineVectorNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Master Output Node ---
    class SLIPENGINE_API MasterNode : public ShaderNode {
    public:
        MasterNode();
        ShaderGraphType materialType = ShaderGraphType::Lit;

        void SetMaterialType(ShaderGraphType type);
        void UpdatePorts() override;
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override { return std::make_unique<MasterNode>(*this); }
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

} // namespace SlipEngine

#endif // SLIP_SHADER_NODE_H
```

---

### 3.3 `src/render/shadergraph/ShaderNode.cpp` Implementation Details

Here is the implementation skeleton for core base class functions, registry initialization, and representative nodes:

```cpp
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include <sstream>
#include <iomanip>

namespace SlipEngine {

    // --- Helper Implementations for ShaderGraphTypes ---
    const char* PinTypeToString(PinType type) {
        switch (type) {
            case PinType::Float: return "Float";
            case PinType::Vec2: return "Vec2";
            case PinType::Vec3: return "Vec3";
            case PinType::Vec4: return "Vec4";
            case PinType::Texture2D: return "Texture2D";
            case PinType::Matrix4: return "Matrix4";
            case PinType::Boolean: return "Boolean";
            default: return "Float";
        }
    }

    PinType StringToPinType(const std::string& str) {
        if (str == "Float") return PinType::Float;
        if (str == "Vec2") return PinType::Vec2;
        if (str == "Vec3") return PinType::Vec3;
        if (str == "Vec4") return PinType::Vec4;
        if (str == "Texture2D") return PinType::Texture2D;
        if (str == "Matrix4") return PinType::Matrix4;
        if (str == "Boolean") return PinType::Boolean;
        return PinType::Float;
    }

    const char* GetPinTypeGLSLName(PinType type) {
        switch (type) {
            case PinType::Float: return "float";
            case PinType::Vec2: return "vec2";
            case PinType::Vec3: return "vec3";
            case PinType::Vec4: return "vec4";
            case PinType::Texture2D: return "sampler2D";
            case PinType::Matrix4: return "mat4";
            case PinType::Boolean: return "bool";
            default: return "float";
        }
    }

    int GetPinTypeDimension(PinType type) {
        switch (type) {
            case PinType::Float: return 1;
            case PinType::Vec2: return 2;
            case PinType::Vec3: return 3;
            case PinType::Vec4: return 4;
            default: return 0;
        }
    }

    bool ArePinsCompatible(PinType fromType, PinType toType) {
        if (fromType == toType) return true;
        if (fromType == PinType::Texture2D || toType == PinType::Texture2D) return false;
        if (fromType == PinType::Matrix4 || toType == PinType::Matrix4) return false;
        int d1 = GetPinTypeDimension(fromType);
        int d2 = GetPinTypeDimension(toType);
        return (d1 >= 1 && d1 <= 4 && d2 >= 1 && d2 <= 4);
    }

    std::string FormatGLSLValue(PinType type, const glm::vec4& val) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        switch (type) {
            case PinType::Float:
                ss << val.x;
                return ss.str();
            case PinType::Vec2:
                ss << "vec2(" << val.x << ", " << val.y << ")";
                return ss.str();
            case PinType::Vec3:
                ss << "vec3(" << val.x << ", " << val.y << ", " << val.z << ")";
                return ss.str();
            case PinType::Vec4:
                ss << "vec4(" << val.x << ", " << val.y << ", " << val.z << ", " << val.w << ")";
                return ss.str();
            default:
                return "0.0";
        }
    }

    std::string ConvertGLSLExpression(PinType fromType, PinType toType, const std::string& expr) {
        if (fromType == toType || expr.empty()) return expr;
        int dFrom = GetPinTypeDimension(fromType);
        int dTo = GetPinTypeDimension(toType);
        if (dFrom == 0 || dTo == 0) return expr;

        if (dFrom == 1) {
            // Scalar promotion: float -> vecN
            if (dTo == 2) return "vec2(" + expr + ")";
            if (dTo == 3) return "vec3(" + expr + ")";
            if (dTo == 4) return "vec4(" + expr + ")";
        } else if (dFrom == 2) {
            if (dTo == 1) return "(" + expr + ").x";
            if (dTo == 3) return "vec3(" + expr + ", 0.0)";
            if (dTo == 4) return "vec4(" + expr + ", 0.0, 1.0)";
        } else if (dFrom == 3) {
            if (dTo == 1) return "(" + expr + ").x";
            if (dTo == 2) return "(" + expr + ").xy";
            if (dTo == 4) return "vec4(" + expr + ", 1.0)";
        } else if (dFrom == 4) {
            if (dTo == 1) return "(" + expr + ").x";
            if (dTo == 2) return "(" + expr + ").xy";
            if (dTo == 3) return "(" + expr + ").xyz";
        }
        return expr;
    }

    const char* ShaderGraphTypeToString(ShaderGraphType type) {
        return (type == ShaderGraphType::Lit) ? "Lit" : "Unlit";
    }

    ShaderGraphType StringToShaderGraphType(const std::string& str) {
        return (str == "Unlit") ? ShaderGraphType::Unlit : ShaderGraphType::Lit;
    }

    // --- ShaderNode Base Methods ---
    const ShaderPort* ShaderNode::FindInput(const std::string& portId) const {
        for (const auto& port : inputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    ShaderPort* ShaderNode::FindInput(const std::string& portId) {
        for (auto& port : inputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    const ShaderPort* ShaderNode::FindOutput(const std::string& portId) const {
        for (const auto& port : outputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    ShaderPort* ShaderNode::FindOutput(const std::string& portId) {
        for (auto& port : outputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    ShaderPort& ShaderNode::AddInput(const std::string& portId, const std::string& name, PinType type, const glm::vec4& defaultVal) {
        inputs.emplace_back(portId, name, type, false, defaultVal);
        return inputs.back();
    }

    ShaderPort& ShaderNode::AddOutput(const std::string& portId, const std::string& name, PinType type) {
        outputs.emplace_back(portId, name, type, true, glm::vec4(0.0f));
        return outputs.back();
    }

    void ShaderNode::GenerateGLSL(
        const std::unordered_map<std::string, std::string>& inExpr,
        std::unordered_map<std::string, std::string>& outExpr,
        std::string& stmts
    ) const {
        // Base does nothing
    }

    // --- ShaderNodeRegistry Singleton ---
    ShaderNodeRegistry::ShaderNodeRegistry() {
        InitializeDefaultNodes();
    }

    ShaderNodeRegistry& ShaderNodeRegistry::Get() {
        static ShaderNodeRegistry instance;
        return instance;
    }

    void ShaderNodeRegistry::Register(const NodeDescriptor& desc) {
        auto it = m_NameToIndex.find(desc.typeName);
        if (it != m_NameToIndex.end()) {
            m_Descriptors[it->second] = desc;
        } else {
            m_NameToIndex[desc.typeName] = m_Descriptors.size();
            m_Descriptors.push_back(desc);
        }
    }

    std::unique_ptr<ShaderNode> ShaderNodeRegistry::CreateNode(const std::string& typeName) const {
        auto it = m_NameToIndex.find(typeName);
        if (it != m_NameToIndex.end()) {
            return m_Descriptors[it->second].factory();
        }
        return nullptr;
    }

    const std::vector<NodeDescriptor>& ShaderNodeRegistry::GetRegisteredNodes() const {
        return m_Descriptors;
    }

    const NodeDescriptor* ShaderNodeRegistry::FindDescriptor(const std::string& typeName) const {
        auto it = m_NameToIndex.find(typeName);
        if (it != m_NameToIndex.end()) {
            return &m_Descriptors[it->second];
        }
        return nullptr;
    }

    void ShaderNodeRegistry::InitializeDefaultNodes() {
        if (m_Initialized) return;
        m_Initialized = true;

        // Math
        RegisterNodeType<MathAddNode>("Math_Add", "Add", "Math", "Sum of two values (A + B)");
        RegisterNodeType<MathSubtractNode>("Math_Subtract", "Subtract", "Math", "Difference between two values (A - B)");
        RegisterNodeType<MathMultiplyNode>("Math_Multiply", "Multiply", "Math", "Product of two values (A * B)");
        RegisterNodeType<MathDivideNode>("Math_Divide", "Divide", "Math", "Division of two values (A / B)");
        RegisterNodeType<MathSinNode>("Math_Sin", "Sin", "Math/Trigonometry", "Sine of input angle in radians");
        RegisterNodeType<MathCosNode>("Math_Cos", "Cos", "Math/Trigonometry", "Cosine of input angle in radians");
        RegisterNodeType<MathTanNode>("Math_Tan", "Tan", "Math/Trigonometry", "Tangent of input angle in radians");
        RegisterNodeType<MathDotNode>("Math_Dot", "Dot Product", "Math/Vector", "Dot product of two 3D vectors");
        RegisterNodeType<MathCrossNode>("Math_Cross", "Cross Product", "Math/Vector", "Cross product of two 3D vectors");
        RegisterNodeType<MathNormalizeNode>("Math_Normalize", "Normalize", "Math/Vector", "Vector normalized to unit length");
        RegisterNodeType<MathLengthNode>("Math_Length", "Length", "Math/Vector", "Euclidean length of a vector");
        RegisterNodeType<MathDistanceNode>("Math_Distance", "Distance", "Math/Vector", "Euclidean distance between two points");

        // Procedural
        RegisterNodeType<ProceduralMinNode>("Procedural_Min", "Min", "Procedural", "Minimum of two values");
        RegisterNodeType<ProceduralMaxNode>("Procedural_Max", "Max", "Procedural", "Maximum of two values");
        RegisterNodeType<ProceduralClampNode>("Procedural_Clamp", "Clamp", "Procedural", "Clamp input between min and max");
        RegisterNodeType<ProceduralLerpNode>("Procedural_Lerp", "Lerp", "Procedural", "Linear interpolation between A and B by T");
        RegisterNodeType<ProceduralAbsNode>("Procedural_Abs", "Abs", "Procedural", "Absolute value");
        RegisterNodeType<ProceduralStepNode>("Procedural_Step", "Step", "Procedural", "Step function (0 if In < Edge else 1)");
        RegisterNodeType<ProceduralSmoothstepNode>("Procedural_Smoothstep", "Smoothstep", "Procedural", "Hermite smooth interpolation");
        RegisterNodeType<ProceduralRemapNode>("Procedural_Remap", "Remap", "Procedural", "Remap value from in-range to out-range");
        RegisterNodeType<ProceduralNoiseNode>("Procedural_Noise", "Noise 2D", "Procedural", "Procedural 2D gradient noise");
        RegisterNodeType<ProceduralVoronoiNode>("Procedural_Voronoi", "Voronoi", "Procedural", "Procedural cellular Voronoi diagram");

        // Time
        RegisterNodeType<TimeNode>("Time_Time", "Time", "Time", "Global time and delta time uniforms");
        RegisterNodeType<DeltaTimeNode>("Time_DeltaTime", "Delta Time", "Time", "Frame delta time uniform");

        // Camera / Transform
        RegisterNodeType<CameraPositionNode>("Transform_CameraPosition", "Camera Position", "Transform", "World position of camera");
        RegisterNodeType<ViewMatrixNode>("Transform_ViewMatrix", "View Matrix", "Transform", "View matrix");
        RegisterNodeType<ProjectionMatrixNode>("Transform_ProjectionMatrix", "Projection Matrix", "Transform", "Projection matrix");
        RegisterNodeType<WorldPositionNode>("Transform_WorldPosition", "World Position", "Transform", "Interpolated world position");
        RegisterNodeType<WorldNormalNode>("Transform_WorldNormal", "World Normal", "Transform", "Interpolated world normal");

        // Texture
        RegisterNodeType<TextureSamplerNode>("Texture_Sampler", "Sample Texture 2D", "Texture", "Sample 2D texture with UV");
        RegisterNodeType<UVNode>("Texture_UV", "UV Coordinates", "Texture", "Mesh vertex UV coordinates");
        RegisterNodeType<TilingAndOffsetNode>("Texture_TilingAndOffset", "Tiling And Offset", "Texture", "UV tiling and offset");

        // Inputs / Constants / Vectors
        RegisterNodeType<PropertyNode>("Input_Property", "Property", "Input", "Blackboard material parameter");
        RegisterNodeType<FloatConstantNode>("Input_Float", "Float", "Input", "Constant float value");
        RegisterNodeType<Vec2ConstantNode>("Input_Vec2", "Vector2", "Input", "Constant 2D vector");
        RegisterNodeType<Vec3ConstantNode>("Input_Vec3", "Vector3", "Input", "Constant 3D vector");
        RegisterNodeType<Vec4ConstantNode>("Input_Vec4", "Vector4", "Input", "Constant 4D vector / Color");
        RegisterNodeType<SplitVectorNode>("Vector_Split", "Split", "Vector", "Split vector into components");
        RegisterNodeType<CombineVectorNode>("Vector_Combine", "Combine", "Vector", "Combine components into vectors");

        // Master Output
        RegisterNodeType<MasterNode>("MasterOutput", "Master Output", "Master", "Final material output surface terminal");
    }

    // --- MasterNode Implementation ---
    MasterNode::MasterNode() {
        typeName = "MasterOutput";
        title = "Master (Lit)";
        category = "Master";
        SetMaterialType(ShaderGraphType::Lit);
    }

    void MasterNode::SetMaterialType(ShaderGraphType type) {
        materialType = type;
        title = (materialType == ShaderGraphType::Lit) ? "Master (Lit)" : "Master (Unlit)";
        customProperties["materialType"] = ShaderGraphTypeToString(materialType);
        UpdatePorts();
    }

    void MasterNode::OnCustomPropertiesUpdated() {
        auto it = customProperties.find("materialType");
        if (it != customProperties.end()) {
            materialType = StringToShaderGraphType(it->second);
            title = (materialType == ShaderGraphType::Lit) ? "Master (Lit)" : "Master (Unlit)";
            UpdatePorts();
        }
    }

    void MasterNode::UpdatePorts() {
        inputs.clear();
        outputs.clear();

        if (materialType == ShaderGraphType::Lit) {
            AddInput("Albedo", "Albedo", PinType::Vec3, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            AddInput("Normal", "Normal", PinType::Vec3, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
            AddInput("Roughness", "Roughness", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
            AddInput("Metallic", "Metallic", PinType::Float, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            AddInput("Emissive", "Emissive", PinType::Vec3, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            AddInput("AO", "AO", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            AddInput("Alpha", "Alpha", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            AddInput("AlphaClipThreshold", "Alpha Clip", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
        } else {
            AddInput("Color", "Color", PinType::Vec3, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            AddInput("Emissive", "Emissive", PinType::Vec3, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            AddInput("Alpha", "Alpha", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            AddInput("AlphaClipThreshold", "Alpha Clip", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
        }
    }

    void MasterNode::GenerateGLSL(
        const std::unordered_map<std::string, std::string>& inExpr,
        std::unordered_map<std::string, std::string>& outExpr,
        std::string& stmts
    ) const {
        // Output expressions for compiler
        if (materialType == ShaderGraphType::Lit) {
            outExpr["Albedo"] = inExpr.count("Albedo") ? inExpr.at("Albedo") : "vec3(1.0)";
            outExpr["Normal"] = inExpr.count("Normal") ? inExpr.at("Normal") : "vNormal";
            outExpr["Roughness"] = inExpr.count("Roughness") ? inExpr.at("Roughness") : "0.5";
            outExpr["Metallic"] = inExpr.count("Metallic") ? inExpr.at("Metallic") : "0.0";
            outExpr["Emissive"] = inExpr.count("Emissive") ? inExpr.at("Emissive") : "vec3(0.0)";
            outExpr["AO"] = inExpr.count("AO") ? inExpr.at("AO") : "1.0";
            outExpr["Alpha"] = inExpr.count("Alpha") ? inExpr.at("Alpha") : "1.0";
            outExpr["AlphaClipThreshold"] = inExpr.count("AlphaClipThreshold") ? inExpr.at("AlphaClipThreshold") : "0.5";
        } else {
            outExpr["Color"] = inExpr.count("Color") ? inExpr.at("Color") : "vec3(1.0)";
            outExpr["Emissive"] = inExpr.count("Emissive") ? inExpr.at("Emissive") : "vec3(0.0)";
            outExpr["Alpha"] = inExpr.count("Alpha") ? inExpr.at("Alpha") : "1.0";
            outExpr["AlphaClipThreshold"] = inExpr.count("AlphaClipThreshold") ? inExpr.at("AlphaClipThreshold") : "0.5";
        }
    }

    // --- Math Nodes Implementation ---
    MathAddNode::MathAddNode() {
        typeName = "Math_Add";
        title = "Add";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathAddNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "(" + a + " + " + b + ")";
    }

    MathSubtractNode::MathSubtractNode() {
        typeName = "Math_Subtract";
        title = "Subtract";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathSubtractNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "(" + a + " - " + b + ")";
    }

    MathMultiplyNode::MathMultiplyNode() {
        typeName = "Math_Multiply";
        title = "Multiply";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(1.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathMultiplyNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "1.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "1.0";
        outExpr["Out"] = "(" + a + " * " + b + ")";
    }

    MathDivideNode::MathDivideNode() {
        typeName = "Math_Divide";
        title = "Divide";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(1.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathDivideNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "1.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "1.0";
        outExpr["Out"] = "(" + a + " / (" + b + " != 0.0 ? " + b + " : 0.00001))";
    }

    MathSinNode::MathSinNode() {
        typeName = "Math_Sin";
        title = "Sin";
        category = "Math/Trigonometry";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathSinNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "sin(" + inVal + ")";
    }

    MathCosNode::MathCosNode() {
        typeName = "Math_Cos";
        title = "Cos";
        category = "Math/Trigonometry";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathCosNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "cos(" + inVal + ")";
    }

    MathTanNode::MathTanNode() {
        typeName = "Math_Tan";
        title = "Tan";
        category = "Math/Trigonometry";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathTanNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "tan(" + inVal + ")";
    }

    MathDotNode::MathDotNode() {
        typeName = "Math_Dot";
        title = "Dot Product";
        category = "Math/Vector";
        AddInput("A", "A", PinType::Vec3, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Vec3, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathDotNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "vec3(0.0)";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "vec3(0.0)";
        outExpr["Out"] = "dot(" + a + ", " + b + ")";
    }

    MathCrossNode::MathCrossNode() {
        typeName = "Math_Cross";
        title = "Cross Product";
        category = "Math/Vector";
        AddInput("A", "A", PinType::Vec3, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        AddInput("B", "B", PinType::Vec3, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Vec3);
    }
    void MathCrossNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "vec3(0.0, 0.0, 1.0)";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "vec3(1.0, 0.0, 0.0)";
        outExpr["Out"] = "cross(" + a + ", " + b + ")";
    }

    MathNormalizeNode::MathNormalizeNode() {
        typeName = "Math_Normalize";
        title = "Normalize";
        category = "Math/Vector";
        AddInput("In", "In", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Vec3);
    }
    void MathNormalizeNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "vec3(0.0, 1.0, 0.0)";
        outExpr["Out"] = "normalize(" + inVal + ")";
    }

    MathLengthNode::MathLengthNode() {
        typeName = "Math_Length";
        title = "Length";
        category = "Math/Vector";
        AddInput("In", "In", PinType::Vec3, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathLengthNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "vec3(0.0)";
        outExpr["Out"] = "length(" + inVal + ")";
    }

    MathDistanceNode::MathDistanceNode() {
        typeName = "Math_Distance";
        title = "Distance";
        category = "Math/Vector";
        AddInput("A", "A", PinType::Vec3, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Vec3, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void MathDistanceNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "vec3(0.0)";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "vec3(0.0)";
        outExpr["Out"] = "distance(" + a + ", " + b + ")";
    }

    // --- Procedural Nodes Implementation ---
    ProceduralMinNode::ProceduralMinNode() {
        typeName = "Procedural_Min";
        title = "Min";
        category = "Procedural";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralMinNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "min(" + a + ", " + b + ")";
    }

    ProceduralMaxNode::ProceduralMaxNode() {
        typeName = "Procedural_Max";
        title = "Max";
        category = "Procedural";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralMaxNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "max(" + a + ", " + b + ")";
    }

    ProceduralClampNode::ProceduralClampNode() {
        typeName = "Procedural_Clamp";
        title = "Clamp";
        category = "Procedural";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddInput("Min", "Min", PinType::Float, glm::vec4(0.0f));
        AddInput("Max", "Max", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralClampNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        std::string minVal = inExpr.count("Min") ? inExpr.at("Min") : "0.0";
        std::string maxVal = inExpr.count("Max") ? inExpr.at("Max") : "1.0";
        outExpr["Out"] = "clamp(" + inVal + ", " + minVal + ", " + maxVal + ")";
    }

    ProceduralLerpNode::ProceduralLerpNode() {
        typeName = "Procedural_Lerp";
        title = "Lerp";
        category = "Procedural";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(1.0f));
        AddInput("T", "T", PinType::Float, glm::vec4(0.5f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralLerpNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "1.0";
        std::string t = inExpr.count("T") ? inExpr.at("T") : "0.5";
        outExpr["Out"] = "mix(" + a + ", " + b + ", " + t + ")";
    }

    ProceduralAbsNode::ProceduralAbsNode() {
        typeName = "Procedural_Abs";
        title = "Abs";
        category = "Procedural";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralAbsNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "abs(" + inVal + ")";
    }

    ProceduralStepNode::ProceduralStepNode() {
        typeName = "Procedural_Step";
        title = "Step";
        category = "Procedural";
        AddInput("Edge", "Edge", PinType::Float, glm::vec4(0.5f));
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralStepNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string edge = inExpr.count("Edge") ? inExpr.at("Edge") : "0.5";
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "step(" + edge + ", " + inVal + ")";
    }

    ProceduralSmoothstepNode::ProceduralSmoothstepNode() {
        typeName = "Procedural_Smoothstep";
        title = "Smoothstep";
        category = "Procedural";
        AddInput("Edge1", "Edge1", PinType::Float, glm::vec4(0.0f));
        AddInput("Edge2", "Edge2", PinType::Float, glm::vec4(1.0f));
        AddInput("In", "In", PinType::Float, glm::vec4(0.5f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralSmoothstepNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string e1 = inExpr.count("Edge1") ? inExpr.at("Edge1") : "0.0";
        std::string e2 = inExpr.count("Edge2") ? inExpr.at("Edge2") : "1.0";
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.5";
        outExpr["Out"] = "smoothstep(" + e1 + ", " + e2 + ", " + inVal + ")";
    }

    ProceduralRemapNode::ProceduralRemapNode() {
        typeName = "Procedural_Remap";
        title = "Remap";
        category = "Procedural";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddInput("InMin", "In Min", PinType::Float, glm::vec4(0.0f));
        AddInput("InMax", "In Max", PinType::Float, glm::vec4(1.0f));
        AddInput("OutMin", "Out Min", PinType::Float, glm::vec4(0.0f));
        AddInput("OutMax", "Out Max", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralRemapNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string val = inExpr.count("In") ? inExpr.at("In") : "0.0";
        std::string inMin = inExpr.count("InMin") ? inExpr.at("InMin") : "0.0";
        std::string inMax = inExpr.count("InMax") ? inExpr.at("InMax") : "1.0";
        std::string outMin = inExpr.count("OutMin") ? inExpr.at("OutMin") : "0.0";
        std::string outMax = inExpr.count("OutMax") ? inExpr.at("OutMax") : "1.0";
        outExpr["Out"] = "(" + outMin + " + ((" + val + " - " + inMin + ") / max(" + inMax + " - " + inMin + ", 0.00001)) * (" + outMax + " - " + outMin + "))";
    }

    ProceduralNoiseNode::ProceduralNoiseNode() {
        typeName = "Procedural_Noise";
        title = "Noise 2D";
        category = "Procedural";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f));
        AddInput("Scale", "Scale", PinType::Float, glm::vec4(5.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    void ProceduralNoiseNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string scale = inExpr.count("Scale") ? inExpr.at("Scale") : "5.0";
        outExpr["Out"] = "slip_gradient_noise(" + uv + " * " + scale + ")";
    }

    ProceduralVoronoiNode::ProceduralVoronoiNode() {
        typeName = "Procedural_Voronoi";
        title = "Voronoi";
        category = "Procedural";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f));
        AddInput("Scale", "Scale", PinType::Float, glm::vec4(5.0f, 0.0f, 0.0f, 0.0f));
        AddInput("AngleOffset", "Angle Offset", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
        AddOutput("Cells", "Cells", PinType::Float);
    }
    void ProceduralVoronoiNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string scale = inExpr.count("Scale") ? inExpr.at("Scale") : "5.0";
        std::string angle = inExpr.count("AngleOffset") ? inExpr.at("AngleOffset") : "0.0";
        std::string varName = "voronoi_" + id;
        stmts += "    vec2 " + varName + " = slip_voronoi(" + uv + ", " + angle + ", " + scale + ");\n";
        outExpr["Out"] = varName + ".x";
        outExpr["Cells"] = varName + ".y";
    }

    // --- Time Nodes Implementation ---
    TimeNode::TimeNode() {
        typeName = "Time_Time";
        title = "Time";
        category = "Time";
        AddOutput("Time", "Time", PinType::Float);
        AddOutput("SinTime", "Sin Time", PinType::Float);
        AddOutput("CosTime", "Cos Time", PinType::Float);
        AddOutput("DeltaTime", "Delta Time", PinType::Float);
    }
    void TimeNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Time"] = "u_Time";
        outExpr["SinTime"] = "sin(u_Time)";
        outExpr["CosTime"] = "cos(u_Time)";
        outExpr["DeltaTime"] = "u_DeltaTime";
    }

    DeltaTimeNode::DeltaTimeNode() {
        typeName = "Time_DeltaTime";
        title = "Delta Time";
        category = "Time";
        AddOutput("DeltaTime", "Delta Time", PinType::Float);
    }
    void DeltaTimeNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["DeltaTime"] = "u_DeltaTime";
    }

    // --- Camera / Transform Nodes Implementation ---
    CameraPositionNode::CameraPositionNode() {
        typeName = "Transform_CameraPosition";
        title = "Camera Position";
        category = "Transform";
        AddOutput("Position", "Position", PinType::Vec3);
    }
    void CameraPositionNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Position"] = "u_CameraPos";
    }

    ViewMatrixNode::ViewMatrixNode() {
        typeName = "Transform_ViewMatrix";
        title = "View Matrix";
        category = "Transform";
        AddOutput("View", "View", PinType::Matrix4);
    }
    void ViewMatrixNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["View"] = "u_View";
    }

    ProjectionMatrixNode::ProjectionMatrixNode() {
        typeName = "Transform_ProjectionMatrix";
        title = "Projection Matrix";
        category = "Transform";
        AddOutput("Projection", "Projection", PinType::Matrix4);
    }
    void ProjectionMatrixNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Projection"] = "u_Projection";
    }

    WorldPositionNode::WorldPositionNode() {
        typeName = "Transform_WorldPosition";
        title = "World Position";
        category = "Transform";
        AddOutput("Position", "Position", PinType::Vec3);
    }
    void WorldPositionNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Position"] = "vWorldPos";
    }

    WorldNormalNode::WorldNormalNode() {
        typeName = "Transform_WorldNormal";
        title = "World Normal";
        category = "Transform";
        AddOutput("Normal", "Normal", PinType::Vec3);
    }
    void WorldNormalNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Normal"] = "vNormal";
    }

    // --- Texture Nodes Implementation ---
    TextureSamplerNode::TextureSamplerNode() {
        typeName = "Texture_Sampler";
        title = "Sample Texture 2D";
        category = "Texture";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f));
        AddInput("Sampler", "Sampler", PinType::Texture2D);
        AddOutput("RGBA", "RGBA", PinType::Vec4);
        AddOutput("RGB", "RGB", PinType::Vec3);
        AddOutput("R", "R", PinType::Float);
        AddOutput("G", "G", PinType::Float);
        AddOutput("B", "B", PinType::Float);
        AddOutput("A", "A", PinType::Float);
    }
    void TextureSamplerNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string sampler = inExpr.count("Sampler") ? inExpr.at("Sampler") : "u_MainTexture";
        std::string sampleVar = "texSample_" + id;
        stmts += "    vec4 " + sampleVar + " = texture(" + sampler + ", " + uv + ");\n";
        outExpr["RGBA"] = sampleVar;
        outExpr["RGB"] = sampleVar + ".rgb";
        outExpr["R"] = sampleVar + ".r";
        outExpr["G"] = sampleVar + ".g";
        outExpr["B"] = sampleVar + ".b";
        outExpr["A"] = sampleVar + ".a";
    }

    UVNode::UVNode() {
        typeName = "Texture_UV";
        title = "UV Coordinates";
        category = "Texture";
        AddOutput("UV", "UV", PinType::Vec2);
    }
    void UVNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["UV"] = "vTexCoords";
    }

    TilingAndOffsetNode::TilingAndOffsetNode() {
        typeName = "Texture_TilingAndOffset";
        title = "Tiling And Offset";
        category = "Texture";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f));
        AddInput("Tiling", "Tiling", PinType::Vec2, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        AddInput("Offset", "Offset", PinType::Vec2, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Vec2);
    }
    void TilingAndOffsetNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string tiling = inExpr.count("Tiling") ? inExpr.at("Tiling") : "vec2(1.0, 1.0)";
        std::string offset = inExpr.count("Offset") ? inExpr.at("Offset") : "vec2(0.0, 0.0)";
        outExpr["Out"] = "(" + uv + " * " + tiling + " + " + offset + ")";
    }

    // --- Property & Constant Nodes Implementation ---
    PropertyNode::PropertyNode() {
        typeName = "Input_Property";
        title = "Property";
        category = "Input";
        UpdatePorts();
    }
    void PropertyNode::SetProperty(const ShaderProperty& prop) {
        propertyId = prop.id;
        propertyName = prop.name;
        propertyType = prop.type;
        title = prop.displayName.empty() ? prop.name : prop.displayName;
        customProperties["propertyId"] = propertyId;
        customProperties["propertyName"] = propertyName;
        customProperties["propertyType"] = PinTypeToString(propertyType);
        UpdatePorts();
    }
    void PropertyNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("propertyId")) propertyId = customProperties["propertyId"];
        if (customProperties.count("propertyName")) propertyName = customProperties["propertyName"];
        if (customProperties.count("propertyType")) propertyType = StringToPinType(customProperties["propertyType"]);
        title = propertyName.empty() ? "Property" : propertyName;
        UpdatePorts();
    }
    void PropertyNode::UpdatePorts() {
        outputs.clear();
        AddOutput("Out", "Out", propertyType);
    }
    void PropertyNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Out"] = "u_" + propertyName;
    }

    FloatConstantNode::FloatConstantNode() {
        typeName = "Input_Float";
        title = "Float";
        category = "Input";
        AddOutput("Out", "Out", PinType::Float);
    }
    void FloatConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("value")) {
            value = std::stof(customProperties["value"]);
        }
    }
    void FloatConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << value;
        outExpr["Out"] = ss.str();
    }

    Vec2ConstantNode::Vec2ConstantNode() {
        typeName = "Input_Vec2";
        title = "Vector2";
        category = "Input";
        AddOutput("Out", "Out", PinType::Vec2);
    }
    void Vec2ConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("x")) value.x = std::stof(customProperties["x"]);
        if (customProperties.count("y")) value.y = std::stof(customProperties["y"]);
    }
    void Vec2ConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Out"] = FormatGLSLValue(PinType::Vec2, glm::vec4(value, 0.0f, 0.0f));
    }

    Vec3ConstantNode::Vec3ConstantNode() {
        typeName = "Input_Vec3";
        title = "Vector3";
        category = "Input";
        AddOutput("Out", "Out", PinType::Vec3);
    }
    void Vec3ConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("x")) value.x = std::stof(customProperties["x"]);
        if (customProperties.count("y")) value.y = std::stof(customProperties["y"]);
        if (customProperties.count("z")) value.z = std::stof(customProperties["z"]);
    }
    void Vec3ConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Out"] = FormatGLSLValue(PinType::Vec3, glm::vec4(value, 0.0f));
    }

    Vec4ConstantNode::Vec4ConstantNode() {
        typeName = "Input_Vec4";
        title = "Vector4";
        category = "Input";
        AddOutput("Out", "Out", PinType::Vec4);
    }
    void Vec4ConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("x")) value.x = std::stof(customProperties["x"]);
        if (customProperties.count("y")) value.y = std::stof(customProperties["y"]);
        if (customProperties.count("z")) value.z = std::stof(customProperties["z"]);
        if (customProperties.count("w")) value.w = std::stof(customProperties["w"]);
    }
    void Vec4ConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        outExpr["Out"] = FormatGLSLValue(PinType::Vec4, value);
    }

    SplitVectorNode::SplitVectorNode() {
        typeName = "Vector_Split";
        title = "Split";
        category = "Vector";
        AddInput("In", "In", PinType::Vec4, glm::vec4(0.0f));
        AddOutput("X", "X", PinType::Float);
        AddOutput("Y", "Y", PinType::Float);
        AddOutput("Z", "Z", PinType::Float);
        AddOutput("W", "W", PinType::Float);
    }
    void SplitVectorNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "vec4(0.0)";
        outExpr["X"] = "(" + inVal + ").x";
        outExpr["Y"] = "(" + inVal + ").y";
        outExpr["Z"] = "(" + inVal + ").z";
        outExpr["W"] = "(" + inVal + ").w";
    }

    CombineVectorNode::CombineVectorNode() {
        typeName = "Vector_Combine";
        title = "Combine";
        category = "Vector";
        AddInput("X", "X", PinType::Float, glm::vec4(0.0f));
        AddInput("Y", "Y", PinType::Float, glm::vec4(0.0f));
        AddInput("Z", "Z", PinType::Float, glm::vec4(0.0f));
        AddInput("W", "W", PinType::Float, glm::vec4(1.0f));
        AddOutput("Vec4", "Vec4", PinType::Vec4);
        AddOutput("Vec3", "Vec3", PinType::Vec3);
        AddOutput("Vec2", "Vec2", PinType::Vec2);
    }
    void CombineVectorNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string x = inExpr.count("X") ? inExpr.at("X") : "0.0";
        std::string y = inExpr.count("Y") ? inExpr.at("Y") : "0.0";
        std::string z = inExpr.count("Z") ? inExpr.at("Z") : "0.0";
        std::string w = inExpr.count("W") ? inExpr.at("W") : "1.0";
        outExpr["Vec4"] = "vec4(" + x + ", " + y + ", " + z + ", " + w + ")";
        outExpr["Vec3"] = "vec3(" + x + ", " + y + ", " + z + ")";
        outExpr["Vec2"] = "vec2(" + x + ", " + y + ")";
    }

} // namespace SlipEngine
```

---

### 3.4 `include/SlipEngine/render/shadergraph/ShaderGraph.h`

```cpp
#pragma once

#ifndef SLIP_SHADER_GRAPH_H
#define SLIP_SHADER_GRAPH_H

#include "ShaderGraphTypes.h"
#include "ShaderNode.h"
#include <vector>
#include <memory>
#include <string>

namespace SlipEngine {

    class SLIPENGINE_API ShaderGraph {
    public:
        ShaderGraphType materialType = ShaderGraphType::Lit;
        std::vector<std::unique_ptr<ShaderNode>> nodes;
        std::vector<ShaderLink> links;
        std::vector<ShaderProperty> properties;

        ShaderGraph();
        ~ShaderGraph() = default;

        // Non-copyable, movable
        ShaderGraph(const ShaderGraph&) = delete;
        ShaderGraph& operator=(const ShaderGraph&) = delete;
        ShaderGraph(ShaderGraph&&) = default;
        ShaderGraph& operator=(ShaderGraph&&) = default;

        // Initialize empty graph with standard Master Output Node
        void InitializeDefaultGraph();

        // Node manipulation
        ShaderNode* AddNode(std::unique_ptr<ShaderNode> node);
        bool RemoveNode(const std::string& nodeId);
        ShaderNode* FindNode(const std::string& nodeId) const;
        MasterNode* GetMasterNode() const;

        // Link manipulation
        bool AddLink(const std::string& fromNodeId, const std::string& fromPortId, const std::string& toNodeId, const std::string& toPortId);
        bool RemoveLink(const std::string& linkId);
        bool RemoveLinksConnectedTo(const std::string& nodeId);
        bool RemoveLinksConnectedToPort(const std::string& nodeId, const std::string& portId);
        const ShaderLink* FindLink(const std::string& linkId) const;
        const ShaderLink* FindLinkToInput(const std::string& toNodeId, const std::string& toPortId) const;

        // Property manipulation (Blackboard)
        ShaderProperty* AddProperty(const std::string& name, PinType type, const glm::vec4& defaultValue = glm::vec4(0.0f));
        bool RemoveProperty(const std::string& propertyId);
        const ShaderProperty* FindProperty(const std::string& propertyId) const;
        ShaderProperty* FindProperty(const std::string& propertyId);

        // Material Type
        void SetMaterialType(ShaderGraphType type);

        // File Persistence (delegates to ShaderGraphSerializer)
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

        // Validation
        bool HasCycles() const;
    };

} // namespace SlipEngine

#endif // SLIP_SHADER_GRAPH_H
```

---

## 4. Integration Contracts with Peers & Milestones

### 4.1 Interface Contract with `explorer_m1_2` (`ShaderGraphSerializer`)
The serializer iterates through:
- `graph.materialType` -> serialized as `"Lit"` or `"Unlit"`.
- `graph.properties` -> array of `{ id, name, displayName, type, defaultValue, defaultTexturePath }`.
- `graph.nodes` -> array of `{ id, typeName, title, posX, posY, customProperties, inputs: [{ id, defaultValue, ... }] }`.
- `graph.links` -> array of `{ id, fromNodeId, fromPortId, toNodeId, toPortId }`.

Upon deserialization:
- The serializer creates each node using `ShaderNodeRegistry::Get().CreateNode(typeName)`.
- Restores `id`, `posX`, `posY`, `customProperties`.
- Calls `node->OnCustomPropertiesUpdated()` so dynamic nodes (like `PropertyNode` or `MasterNode`) restore their specific port states.
- Reconnects links and sets property overrides.

### 4.2 Interface Contract with `explorer_m1_3` (CMake & Build/Verification)
- All new files live strictly in `include/SlipEngine/render/shadergraph/*.h` and `src/render/shadergraph/*.cpp`.
- Root `CMakeLists.txt` automatically includes `src/render/*.cpp` recursively into `SlipEngine.dll`.
- M1 unit test can instantiate `ShaderGraph`, create multiple nodes (e.g. `TimeNode`, `MathMultiplyNode`, `MasterNode`), connect them, add a property, test `SaveToFile`, test `LoadFromFile`, and verify node counts and link connections.

### 4.3 Interface Contract with Milestone 2 (`ShaderGraphCompiler`)
- The compiler performs a topological sort on `ShaderGraph` starting from `MasterNode` backwards through `links`.
- For each node in topological order, it resolves the input expressions (either from upstream connected node's output, or formatted default value via `FormatGLSLValue`), and invokes `node->GenerateGLSL(...)`.
- `MasterNode` receives the final resolved inputs and feeds them into the GGX PBR lighting model (Lit) or direct color output (Unlit) matching SlipEngine's existing `SlipShaderLibrary` shaders.

---

## 5. Summary of Recommended Next Steps for Worker Implementer

1. Create directory `include/SlipEngine/render/shadergraph/`.
2. Write `ShaderGraphTypes.h` matching the above specification.
3. Write `ShaderNode.h` with the base class, node registry, and node subclasses.
4. Create directory `src/render/shadergraph/`.
5. Write `ShaderNode.cpp` with the registry initialization and node constructors/GLSL templates.
6. Write `ShaderGraph.h` and `ShaderGraph.cpp`.
7. Coordinate with `explorer_m1_2`'s serializer to complete M1 data persistence.
