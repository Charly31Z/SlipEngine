#pragma once

#ifndef SLIP_SHADER_GRAPH_H
#define SLIP_SHADER_GRAPH_H

#include "ShaderGraphTypes.h"
#include "ShaderNode.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace SlipShaderGraph {

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
        ShaderGraph(ShaderGraph&&) noexcept = default;
        ShaderGraph& operator=(ShaderGraph&&) noexcept = default;

        // Initialize default graph with standard Master Output Node
        void InitializeDefaultGraph();
        void Clear();

        // Node manipulation
        ShaderNode* AddNode(std::unique_ptr<ShaderNode> node);
        ShaderNode* AddNode(const std::string& typeName);
        ShaderNode* AddNode(const std::string& typeName, float posX, float posY);
        bool RemoveNode(const std::string& nodeId);
        ShaderNode* FindNode(const std::string& nodeId) const;
        MasterNode* GetMasterNode() const;

        // Link manipulation
        bool AddLink(const std::string& fromNodeId, const std::string& fromPortId,
                     const std::string& toNodeId, const std::string& toPortId);
        bool RemoveLink(const std::string& linkId);
        bool RemoveLinksConnectedTo(const std::string& nodeId);
        bool RemoveLinksConnectedToPort(const std::string& nodeId, const std::string& portId);
        const ShaderLink* FindLink(const std::string& linkId) const;
        ShaderLink* FindLink(const std::string& linkId);
        const ShaderLink* FindLinkToInput(const std::string& toNodeId, const std::string& toPortId) const;
        ShaderLink* FindLinkToInput(const std::string& toNodeId, const std::string& toPortId);
        std::vector<const ShaderLink*> FindLinksFromOutput(const std::string& fromNodeId, const std::string& fromPortId) const;

        // Property manipulation (Blackboard)
        ShaderProperty* AddProperty(const std::string& name, PinType type,
                                    const glm::vec4& defaultValue = glm::vec4(0.0f),
                                    const std::string& defaultTexture = "");
        bool RemoveProperty(const std::string& propertyId);
        const ShaderProperty* FindProperty(const std::string& propertyId) const;
        ShaderProperty* FindProperty(const std::string& propertyId);
        const ShaderProperty* FindPropertyByName(const std::string& name) const;
        ShaderProperty* FindPropertyByName(const std::string& name);

        // Material Type
        void SetMaterialType(ShaderGraphType type);

        // File Persistence (delegates to ShaderGraphSerializer)
        bool SaveToFile(const std::string& filepath) const;
        bool LoadFromFile(const std::string& filepath);

        // Validation
        bool HasCycles() const;

        // ID generation helpers
        std::string GenerateUniqueNodeId(const std::string& prefix = "node") const;
        std::string GenerateUniqueLinkId() const;
        std::string GenerateUniquePropertyId(const std::string& prefix = "prop") const;
    };

} // namespace SlipShaderGraph

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // SLIP_SHADER_GRAPH_H
