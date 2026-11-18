#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include <algorithm>
#include <queue>
#include <unordered_set>
#include <sstream>

namespace SlipShaderGraph {

    ShaderGraph::ShaderGraph() {
        InitializeDefaultGraph();
    }

    void ShaderGraph::Clear() {
        nodes.clear();
        links.clear();
        properties.clear();
    }

    void ShaderGraph::InitializeDefaultGraph() {
        Clear();
        auto master = std::make_unique<MasterNode>();
        master->id = "node_master";
        master->posX = 800.0f;
        master->posY = 300.0f;
        master->SetMaterialType(materialType);
        nodes.push_back(std::move(master));
    }

    ShaderNode* ShaderGraph::AddNode(std::unique_ptr<ShaderNode> node) {
        if (!node) return nullptr;

        if (node->id.empty()) {
            std::string prefix = node->typeName.empty() ? "node" : node->typeName;
            // Clean up prefix characters
            for (char& c : prefix) {
                if (!isalnum(static_cast<unsigned char>(c))) c = '_';
            }
            node->id = GenerateUniqueNodeId(prefix);
        } else {
            // If ID is already taken, generate a unique one
            if (FindNode(node->id) != nullptr) {
                node->id = GenerateUniqueNodeId(node->id);
            }
        }

        nodes.push_back(std::move(node));
        return nodes.back().get();
    }

    ShaderNode* ShaderGraph::AddNode(const std::string& typeName) {
        auto node = ShaderNodeRegistry::CreateNode(typeName);
        if (!node) return nullptr;
        return AddNode(std::move(node));
    }

    ShaderNode* ShaderGraph::AddNode(const std::string& typeName, float posX, float posY) {
        auto node = ShaderNodeRegistry::CreateNode(typeName);
        if (!node) return nullptr;
        node->posX = posX;
        node->posY = posY;
        return AddNode(std::move(node));
    }

    bool ShaderGraph::RemoveNode(const std::string& nodeId) {
        auto it = std::find_if(nodes.begin(), nodes.end(), [&](const std::unique_ptr<ShaderNode>& n) {
            return n && n->id == nodeId;
        });

        if (it != nodes.end()) {
            RemoveLinksConnectedTo(nodeId);
            nodes.erase(it);
            return true;
        }

        return false;
    }

    ShaderNode* ShaderGraph::FindNode(const std::string& nodeId) const {
        for (const auto& n : nodes) {
            if (n && n->id == nodeId) return n.get();
        }
        return nullptr;
    }

    MasterNode* ShaderGraph::GetMasterNode() const {
        for (const auto& n : nodes) {
            if (!n) continue;
            auto* master = dynamic_cast<MasterNode*>(n.get());
            if (master) return master;
            if (n->typeName == "MasterOutput" || n->typeName == "MasterNode") {
                return static_cast<MasterNode*>(n.get());
            }
        }
        return nullptr;
    }

    bool ShaderGraph::AddLink(const std::string& fromNodeId, const std::string& fromPortId,
                             const std::string& toNodeId, const std::string& toPortId) {
        if (fromNodeId.empty() || toNodeId.empty() || fromNodeId == toNodeId) {
            return false;
        }

        ShaderNode* fromNode = FindNode(fromNodeId);
        ShaderNode* toNode = FindNode(toNodeId);
        if (!fromNode || !toNode) return false;

        // Find output port
        const ShaderPort* fromPort = fromNode->FindOutput(fromPortId);
        if (!fromPort) {
            // Try matching by name
            for (const auto& p : fromNode->outputs) {
                if (p.name == fromPortId) { fromPort = &p; break; }
            }
        }
        if (!fromPort) return false;

        // Find input port
        ShaderPort* toPort = toNode->FindInput(toPortId);
        if (!toPort) {
            // Try matching by name
            for (auto& p : toNode->inputs) {
                if (p.name == toPortId) { toPort = &p; break; }
            }
        }
        if (!toPort) return false;

        // Check compatibility
        if (!ArePinsCompatible(fromPort->type, toPort->type)) {
            return false;
        }

        // Single-input rule: Replace existing link connected to this input
        RemoveLinksConnectedToPort(toNodeId, toPort->id);

        std::string linkId = GenerateUniqueLinkId();
        links.emplace_back(linkId, fromNodeId, fromPort->id, toNodeId, toPort->id);

        // Check for cycles
        if (HasCycles()) {
            links.pop_back();
            return false;
        }

        return true;
    }

    bool ShaderGraph::RemoveLink(const std::string& linkId) {
        auto it = std::remove_if(links.begin(), links.end(), [&](const ShaderLink& l) {
            return l.id == linkId;
        });

        if (it != links.end()) {
            links.erase(it, links.end());
            return true;
        }

        return false;
    }

    bool ShaderGraph::RemoveLinksConnectedTo(const std::string& nodeId) {
        auto it = std::remove_if(links.begin(), links.end(), [&](const ShaderLink& l) {
            return l.fromNodeId == nodeId || l.toNodeId == nodeId;
        });

        if (it != links.end()) {
            links.erase(it, links.end());
            return true;
        }

        return false;
    }

    bool ShaderGraph::RemoveLinksConnectedToPort(const std::string& nodeId, const std::string& portId) {
        auto it = std::remove_if(links.begin(), links.end(), [&](const ShaderLink& l) {
            return (l.fromNodeId == nodeId && l.fromPortId == portId) ||
                   (l.toNodeId == nodeId && l.toPortId == portId);
        });

        if (it != links.end()) {
            links.erase(it, links.end());
            return true;
        }

        return false;
    }

    const ShaderLink* ShaderGraph::FindLink(const std::string& linkId) const {
        for (const auto& l : links) {
            if (l.id == linkId) return &l;
        }
        return nullptr;
    }

    ShaderLink* ShaderGraph::FindLink(const std::string& linkId) {
        for (auto& l : links) {
            if (l.id == linkId) return &l;
        }
        return nullptr;
    }

    const ShaderLink* ShaderGraph::FindLinkToInput(const std::string& toNodeId, const std::string& toPortId) const {
        for (const auto& l : links) {
            if (l.toNodeId == toNodeId && l.toPortId == toPortId) return &l;
        }
        return nullptr;
    }

    ShaderLink* ShaderGraph::FindLinkToInput(const std::string& toNodeId, const std::string& toPortId) {
        for (auto& l : links) {
            if (l.toNodeId == toNodeId && l.toPortId == toPortId) return &l;
        }
        return nullptr;
    }

    std::vector<const ShaderLink*> ShaderGraph::FindLinksFromOutput(const std::string& fromNodeId, const std::string& fromPortId) const {
        std::vector<const ShaderLink*> result;
        for (const auto& l : links) {
            if (l.fromNodeId == fromNodeId && l.fromPortId == fromPortId) {
                result.push_back(&l);
            }
        }
        return result;
    }

    ShaderProperty* ShaderGraph::AddProperty(const std::string& name, PinType type,
                                            const glm::vec4& defaultValue,
                                            const std::string& defaultTexture) {
        std::string propId = GenerateUniquePropertyId("prop_" + name);
        properties.emplace_back(propId, name, type, defaultValue, name, defaultTexture);
        return &properties.back();
    }

    bool ShaderGraph::RemoveProperty(const std::string& propertyId) {
        auto it = std::remove_if(properties.begin(), properties.end(), [&](const ShaderProperty& p) {
            return p.id == propertyId;
        });

        if (it != properties.end()) {
            properties.erase(it, properties.end());
            return true;
        }

        return false;
    }

    const ShaderProperty* ShaderGraph::FindProperty(const std::string& propertyId) const {
        for (const auto& p : properties) {
            if (p.id == propertyId) return &p;
        }
        return nullptr;
    }

    ShaderProperty* ShaderGraph::FindProperty(const std::string& propertyId) {
        for (auto& p : properties) {
            if (p.id == propertyId) return &p;
        }
        return nullptr;
    }

    const ShaderProperty* ShaderGraph::FindPropertyByName(const std::string& name) const {
        for (const auto& p : properties) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    ShaderProperty* ShaderGraph::FindPropertyByName(const std::string& name) {
        for (auto& p : properties) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    void ShaderGraph::SetMaterialType(ShaderGraphType type) {
        materialType = type;
        MasterNode* master = GetMasterNode();
        if (master) {
            master->SetMaterialType(type);
        }
    }

    bool ShaderGraph::SaveToFile(const std::string& filepath) const {
        return ShaderGraphSerializer::SerializeToFile(*this, filepath);
    }

    bool ShaderGraph::LoadFromFile(const std::string& filepath) {
        return ShaderGraphSerializer::DeserializeFromFile(*this, filepath);
    }

    bool ShaderGraph::HasCycles() const {
        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_set<std::string> allNodes;

        for (const auto& node : nodes) {
            if (node) allNodes.insert(node->id);
        }

        for (const auto& link : links) {
            adj[link.fromNodeId].push_back(link.toNodeId);
        }

        // Color states: 0 = unvisited, 1 = visiting (in recursion stack), 2 = visited
        std::unordered_map<std::string, int> state;

        std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
            state[u] = 1;
            auto it = adj.find(u);
            if (it != adj.end()) {
                for (const auto& v : it->second) {
                    int vState = state[v];
                    if (vState == 1) return true; // Cycle detected
                    if (vState == 0 && dfs(v)) return true;
                }
            }
            state[u] = 2;
            return false;
        };

        for (const auto& nodeId : allNodes) {
            if (state[nodeId] == 0) {
                if (dfs(nodeId)) return true;
            }
        }

        return false;
    }

    std::string ShaderGraph::GenerateUniqueNodeId(const std::string& prefix) const {
        int index = 1;
        std::string candidate;
        do {
            candidate = prefix + "_" + std::to_string(index++);
        } while (FindNode(candidate) != nullptr);
        return candidate;
    }

    std::string ShaderGraph::GenerateUniqueLinkId() const {
        int index = 1;
        std::string candidate;
        auto linkExists = [&](const std::string& id) {
            for (const auto& l : links) {
                if (l.id == id) return true;
            }
            return false;
        };
        do {
            candidate = "link_" + std::to_string(index++);
        } while (linkExists(candidate));
        return candidate;
    }

    std::string ShaderGraph::GenerateUniquePropertyId(const std::string& prefix) const {
        int index = 1;
        std::string candidate = prefix;
        if (candidate.empty()) candidate = "prop";
        while (FindProperty(candidate) != nullptr) {
            candidate = prefix + "_" + std::to_string(index++);
        }
        return candidate;
    }

} // namespace SlipShaderGraph
