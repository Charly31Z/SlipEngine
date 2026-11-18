#pragma once

#ifndef SLIP_SHADER_NODE_H
#define SLIP_SHADER_NODE_H

#include "ShaderGraphTypes.h"
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace SlipShaderGraph {

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

        // Code Generation Interface
        virtual void GenerateGLSL(
            const std::unordered_map<std::string, std::string>& inputExpressions,
            std::unordered_map<std::string, std::string>& outputExpressions,
            std::string& statements
        ) const;
    };

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
        static std::unique_ptr<ShaderNode> CreateNode(const std::string& typeName);

        void Register(const NodeDescriptor& desc);
        void RegisterAlias(const std::string& alias, const std::string& canonicalTypeName);

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

        std::unique_ptr<ShaderNode> Instantiate(const std::string& typeName) const;
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
        std::unordered_map<std::string, std::string> m_Aliases;
        bool m_Initialized = false;
    };

    // --- Concrete Math Nodes ---
    class SLIPENGINE_API MathAddNode : public ShaderNode {
    public:
        MathAddNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathSubtractNode : public ShaderNode {
    public:
        MathSubtractNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathMultiplyNode : public ShaderNode {
    public:
        MathMultiplyNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathDivideNode : public ShaderNode {
    public:
        MathDivideNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathSinNode : public ShaderNode {
    public:
        MathSinNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathCosNode : public ShaderNode {
    public:
        MathCosNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathTanNode : public ShaderNode {
    public:
        MathTanNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathDotNode : public ShaderNode {
    public:
        MathDotNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathCrossNode : public ShaderNode {
    public:
        MathCrossNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathNormalizeNode : public ShaderNode {
    public:
        MathNormalizeNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathLengthNode : public ShaderNode {
    public:
        MathLengthNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API MathDistanceNode : public ShaderNode {
    public:
        MathDistanceNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Procedural Nodes ---
    class SLIPENGINE_API ProceduralMinNode : public ShaderNode {
    public:
        ProceduralMinNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralMaxNode : public ShaderNode {
    public:
        ProceduralMaxNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralClampNode : public ShaderNode {
    public:
        ProceduralClampNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralLerpNode : public ShaderNode {
    public:
        ProceduralLerpNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralAbsNode : public ShaderNode {
    public:
        ProceduralAbsNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralStepNode : public ShaderNode {
    public:
        ProceduralStepNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralSmoothstepNode : public ShaderNode {
    public:
        ProceduralSmoothstepNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralRemapNode : public ShaderNode {
    public:
        ProceduralRemapNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralNoiseNode : public ShaderNode {
    public:
        ProceduralNoiseNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProceduralVoronoiNode : public ShaderNode {
    public:
        ProceduralVoronoiNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Time Nodes ---
    class SLIPENGINE_API TimeNode : public ShaderNode {
    public:
        TimeNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API DeltaTimeNode : public ShaderNode {
    public:
        DeltaTimeNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Camera / Transform Nodes ---
    class SLIPENGINE_API CameraPositionNode : public ShaderNode {
    public:
        CameraPositionNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ViewMatrixNode : public ShaderNode {
    public:
        ViewMatrixNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API ProjectionMatrixNode : public ShaderNode {
    public:
        ProjectionMatrixNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API WorldPositionNode : public ShaderNode {
    public:
        WorldPositionNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API WorldNormalNode : public ShaderNode {
    public:
        WorldNormalNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Texture Nodes ---
    class SLIPENGINE_API TextureSamplerNode : public ShaderNode {
    public:
        TextureSamplerNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API UVNode : public ShaderNode {
    public:
        UVNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API TilingAndOffsetNode : public ShaderNode {
    public:
        TilingAndOffsetNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Concrete Property / Constant / Vector Nodes ---
    class SLIPENGINE_API PropertyNode : public ShaderNode {
    public:
        std::string propertyId;
        std::string propertyName;
        PinType propertyType = PinType::Float;

        PropertyNode();
        void SetProperty(const ShaderProperty& prop);
        void UpdatePorts() override;
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API FloatConstantNode : public ShaderNode {
    public:
        float value = 0.0f;

        FloatConstantNode();
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API Vec2ConstantNode : public ShaderNode {
    public:
        glm::vec2 value = glm::vec2(0.0f);

        Vec2ConstantNode();
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API Vec3ConstantNode : public ShaderNode {
    public:
        glm::vec3 value = glm::vec3(0.0f);

        Vec3ConstantNode();
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API Vec4ConstantNode : public ShaderNode {
    public:
        glm::vec4 value = glm::vec4(0.0f);

        Vec4ConstantNode();
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API SplitVectorNode : public ShaderNode {
    public:
        SplitVectorNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    class SLIPENGINE_API CombineVectorNode : public ShaderNode {
    public:
        CombineVectorNode();
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

    // --- Master Output Node ---
    class SLIPENGINE_API MasterNode : public ShaderNode {
    public:
        ShaderGraphType materialType = ShaderGraphType::Lit;

        MasterNode();
        void SetMaterialType(ShaderGraphType type);
        void UpdatePorts() override;
        void OnCustomPropertiesUpdated() override;
        std::unique_ptr<ShaderNode> Clone() const override;
        void GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const override;
    };

} // namespace SlipShaderGraph

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // SLIP_SHADER_NODE_H
