#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/error/en.h>

#include <sstream>
#include <vector>
#include <unordered_set>
#include <algorithm>

namespace SlipShaderGraph {

    namespace {
        class UnknownShaderNode : public ShaderNode {
        public:
            UnknownShaderNode() {
                category = "Unknown";
            }
            std::unique_ptr<ShaderNode> Clone() const override {
                return std::make_unique<UnknownShaderNode>(*this);
            }
        };
    }

    bool ShaderGraphSerializer::SerializeToString(const ShaderGraph& graph, std::string& outJson) {
        rapidjson::StringBuffer sb;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
        writer.SetIndent(' ', 2);
        writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);

        writer.StartObject();

        // 1. Versioning
        writer.Key("version");
        writer.Int(CURRENT_SCHEMA_VERSION);

        // 2. Material Type
        writer.Key("materialType");
        writer.String(ShaderGraphTypeToString(graph.materialType));

        // 3. Properties (Blackboard)
        writer.Key("properties");
        writer.StartArray();
        for (const auto& prop : graph.properties) {
            writer.StartObject();
            writer.Key("id"); writer.String(prop.id.c_str());
            writer.Key("name"); writer.String(prop.name.c_str());
            writer.Key("displayName"); writer.String(prop.displayName.c_str());
            writer.Key("type"); writer.String(PinTypeToString(prop.type));
            writer.Key("defaultValue");
            writer.StartArray();
            writer.Double(static_cast<double>(prop.defaultValue.x));
            writer.Double(static_cast<double>(prop.defaultValue.y));
            writer.Double(static_cast<double>(prop.defaultValue.z));
            writer.Double(static_cast<double>(prop.defaultValue.w));
            writer.EndArray();
            writer.Key("defaultTexturePath"); writer.String(prop.defaultTexturePath.c_str());
            writer.EndObject();
        }
        writer.EndArray();

        // 4. Nodes
        writer.Key("nodes");
        writer.StartArray();
        for (const auto& node : graph.nodes) {
            if (!node) continue;
            writer.StartObject();
            writer.Key("id"); writer.String(node->id.c_str());
            writer.Key("type"); writer.String(node->typeName.c_str());
            writer.Key("title"); writer.String(node->title.c_str());
            writer.Key("position");
            writer.StartArray();
            writer.Double(static_cast<double>(node->posX));
            writer.Double(static_cast<double>(node->posY));
            writer.EndArray();

            // Node Custom Properties
            writer.Key("customProperties");
            writer.StartObject();
            for (const auto& kv : node->customProperties) {
                writer.Key(kv.first.c_str());
                writer.String(kv.second.c_str());
            }
            writer.EndObject();

            // Node Inputs
            writer.Key("inputs");
            writer.StartArray();
            for (const auto& port : node->inputs) {
                writer.StartObject();
                writer.Key("id"); writer.String(port.id.c_str());
                writer.Key("name"); writer.String(port.name.c_str());
                writer.Key("type"); writer.String(PinTypeToString(port.type));
                writer.Key("defaultValue");
                writer.StartArray();
                writer.Double(static_cast<double>(port.defaultValue.x));
                writer.Double(static_cast<double>(port.defaultValue.y));
                writer.Double(static_cast<double>(port.defaultValue.z));
                writer.Double(static_cast<double>(port.defaultValue.w));
                writer.EndArray();
                writer.EndObject();
            }
            writer.EndArray();

            // Node Outputs
            writer.Key("outputs");
            writer.StartArray();
            for (const auto& port : node->outputs) {
                writer.StartObject();
                writer.Key("id"); writer.String(port.id.c_str());
                writer.Key("name"); writer.String(port.name.c_str());
                writer.Key("type"); writer.String(PinTypeToString(port.type));
                writer.Key("defaultValue");
                writer.StartArray();
                writer.Double(static_cast<double>(port.defaultValue.x));
                writer.Double(static_cast<double>(port.defaultValue.y));
                writer.Double(static_cast<double>(port.defaultValue.z));
                writer.Double(static_cast<double>(port.defaultValue.w));
                writer.EndArray();
                writer.EndObject();
            }
            writer.EndArray();

            writer.EndObject();
        }
        writer.EndArray();

        // 5. Links
        writer.Key("links");
        writer.StartArray();
        for (const auto& link : graph.links) {
            writer.StartObject();
            writer.Key("id"); writer.String(link.id.c_str());
            writer.Key("fromNode"); writer.String(link.fromNodeId.c_str());
            writer.Key("fromPort"); writer.String(link.fromPortId.c_str());
            writer.Key("toNode"); writer.String(link.toNodeId.c_str());
            writer.Key("toPort"); writer.String(link.toPortId.c_str());
            writer.EndObject();
        }
        writer.EndArray();

        writer.EndObject();

        outJson = sb.GetString();
        return true;
    }

    bool ShaderGraphSerializer::SerializeToFile(const ShaderGraph& graph, const std::string& filepath) {
        if (filepath.empty()) return false;

        std::string json;
        if (!SerializeToString(graph, json)) {
            return false;
        }

        std::string parentDir = SlipFileSystem::GetParentPath(filepath);
        if (!parentDir.empty()) {
            SlipFileSystem::CreateDirectoryPath(parentDir);
        }

        return SlipFileSystem::WriteAllText(filepath, json);
    }

    bool ShaderGraphSerializer::DeserializeFromString(ShaderGraph& outGraph,
                                                      const std::string& json,
                                                      std::string* outErrorMessage) {
        if (json.empty()) {
            if (outErrorMessage) *outErrorMessage = "Cannot deserialize empty JSON string";
            return false;
        }

        rapidjson::Document doc;
        rapidjson::ParseResult parseResult = doc.Parse(json.c_str());
        if (!parseResult) {
            if (outErrorMessage) {
                std::ostringstream ss;
                ss << "JSON parse error: " << rapidjson::GetParseError_En(parseResult.Code())
                   << " at offset " << parseResult.Offset();
                *outErrorMessage = ss.str();
            }
            return false;
        }

        if (!doc.IsObject()) {
            if (outErrorMessage) *outErrorMessage = "Root JSON value is not an object";
            return false;
        }

        // Clear existing graph elements
        outGraph.nodes.clear();
        outGraph.links.clear();
        outGraph.properties.clear();

        // 1. Version Check
        int version = 1;
        if (doc.HasMember("version") && doc["version"].IsInt()) {
            version = doc["version"].GetInt();
        }
        (void)version;

        // 2. Material Type
        if (doc.HasMember("materialType") && doc["materialType"].IsString()) {
            outGraph.materialType = StringToShaderGraphType(doc["materialType"].GetString());
        } else {
            outGraph.materialType = ShaderGraphType::Lit;
        }

        // 3. Properties (Blackboard)
        if (doc.HasMember("properties") && doc["properties"].IsArray()) {
            const auto& propArr = doc["properties"].GetArray();
            for (rapidjson::SizeType i = 0; i < propArr.Size(); ++i) {
                const auto& pVal = propArr[i];
                if (!pVal.IsObject()) continue;

                ShaderProperty prop;
                if (pVal.HasMember("id") && pVal["id"].IsString()) {
                    prop.id = pVal["id"].GetString();
                }
                if (pVal.HasMember("name") && pVal["name"].IsString()) {
                    prop.name = pVal["name"].GetString();
                }
                if (pVal.HasMember("displayName") && pVal["displayName"].IsString()) {
                    prop.displayName = pVal["displayName"].GetString();
                } else {
                    prop.displayName = prop.name;
                }
                if (pVal.HasMember("type") && pVal["type"].IsString()) {
                    prop.type = StringToPinType(pVal["type"].GetString());
                }
                if (pVal.HasMember("defaultValue") && pVal["defaultValue"].IsArray()) {
                    const auto& vecArr = pVal["defaultValue"].GetArray();
                    for (rapidjson::SizeType c = 0; c < vecArr.Size() && c < 4; ++c) {
                        if (vecArr[c].IsNumber()) {
                            prop.defaultValue[c] = static_cast<float>(vecArr[c].GetDouble());
                        }
                    }
                }
                if (pVal.HasMember("defaultTexturePath") && pVal["defaultTexturePath"].IsString()) {
                    prop.defaultTexturePath = pVal["defaultTexturePath"].GetString();
                }

                outGraph.properties.push_back(std::move(prop));
            }
        }

        // 4. Nodes
        std::unordered_set<std::string> validNodeIds;
        if (doc.HasMember("nodes") && doc["nodes"].IsArray()) {
            const auto& nodeArr = doc["nodes"].GetArray();
            for (rapidjson::SizeType i = 0; i < nodeArr.Size(); ++i) {
                const auto& nVal = nodeArr[i];
                if (!nVal.IsObject()) continue;

                std::string typeName = "";
                if (nVal.HasMember("type") && nVal["type"].IsString()) {
                    typeName = nVal["type"].GetString();
                }

                // Create node via Registry factory
                std::unique_ptr<ShaderNode> node = ShaderNodeRegistry::CreateNode(typeName);
                if (!node) {
                    // Unknown node fallback
                    auto unknownNode = std::make_unique<UnknownShaderNode>();
                    unknownNode->typeName = typeName;
                    unknownNode->title = typeName.empty() ? "Unknown Node" : typeName;
                    node = std::move(unknownNode);
                }

                if (nVal.HasMember("id") && nVal["id"].IsString()) {
                    node->id = nVal["id"].GetString();
                }
                if (nVal.HasMember("title") && nVal["title"].IsString()) {
                    node->title = nVal["title"].GetString();
                }
                if (nVal.HasMember("position") && nVal["position"].IsArray()) {
                    const auto& posArr = nVal["position"].GetArray();
                    if (posArr.Size() >= 2 && posArr[0].IsNumber() && posArr[1].IsNumber()) {
                        node->posX = static_cast<float>(posArr[0].GetDouble());
                        node->posY = static_cast<float>(posArr[1].GetDouble());
                    }
                }

                // Restore customProperties
                if (nVal.HasMember("customProperties") && nVal["customProperties"].IsObject()) {
                    for (auto it = nVal["customProperties"].MemberBegin(); it != nVal["customProperties"].MemberEnd(); ++it) {
                        if (it->name.IsString() && it->value.IsString()) {
                            node->customProperties[it->name.GetString()] = it->value.GetString();
                        }
                    }
                    node->OnCustomPropertiesUpdated();
                }

                // Restore input port values
                if (nVal.HasMember("inputs") && nVal["inputs"].IsArray()) {
                    const auto& inArr = nVal["inputs"].GetArray();
                    for (rapidjson::SizeType pIdx = 0; pIdx < inArr.Size(); ++pIdx) {
                        const auto& pVal = inArr[pIdx];
                        if (!pVal.IsObject()) continue;

                        std::string portId = "";
                        if (pVal.HasMember("id") && pVal["id"].IsString()) portId = pVal["id"].GetString();
                        std::string portName = "";
                        if (pVal.HasMember("name") && pVal["name"].IsString()) portName = pVal["name"].GetString();

                        // Find matching port in node inputs
                        ShaderPort* targetPort = nullptr;
                        for (auto& inp : node->inputs) {
                            if (!portId.empty() && inp.id == portId) { targetPort = &inp; break; }
                            if (!portName.empty() && inp.name == portName) { targetPort = &inp; break; }
                        }

                        if (!targetPort) {
                            ShaderPort extraPort;
                            extraPort.id = portId.empty() ? ("in_" + std::to_string(node->inputs.size())) : portId;
                            extraPort.name = portName.empty() ? extraPort.id : portName;
                            extraPort.isOutput = false;
                            node->inputs.push_back(extraPort);
                            targetPort = &node->inputs.back();
                        }

                        if (pVal.HasMember("type") && pVal["type"].IsString()) {
                            targetPort->type = StringToPinType(pVal["type"].GetString());
                        }
                        if (pVal.HasMember("defaultValue") && pVal["defaultValue"].IsArray()) {
                            const auto& vecArr = pVal["defaultValue"].GetArray();
                            for (rapidjson::SizeType c = 0; c < vecArr.Size() && c < 4; ++c) {
                                if (vecArr[c].IsNumber()) {
                                    targetPort->defaultValue[c] = static_cast<float>(vecArr[c].GetDouble());
                                }
                            }
                        }
                    }
                }

                // Restore output ports
                if (nVal.HasMember("outputs") && nVal["outputs"].IsArray()) {
                    const auto& outArr = nVal["outputs"].GetArray();
                    for (rapidjson::SizeType pIdx = 0; pIdx < outArr.Size(); ++pIdx) {
                        const auto& pVal = outArr[pIdx];
                        if (!pVal.IsObject()) continue;

                        std::string portId = "";
                        if (pVal.HasMember("id") && pVal["id"].IsString()) portId = pVal["id"].GetString();
                        std::string portName = "";
                        if (pVal.HasMember("name") && pVal["name"].IsString()) portName = pVal["name"].GetString();

                        ShaderPort* targetPort = nullptr;
                        for (auto& outp : node->outputs) {
                            if (!portId.empty() && outp.id == portId) { targetPort = &outp; break; }
                            if (!portName.empty() && outp.name == portName) { targetPort = &outp; break; }
                        }

                        if (!targetPort) {
                            ShaderPort extraPort;
                            extraPort.id = portId.empty() ? ("out_" + std::to_string(node->outputs.size())) : portId;
                            extraPort.name = portName.empty() ? extraPort.id : portName;
                            extraPort.isOutput = true;
                            node->outputs.push_back(extraPort);
                            targetPort = &node->outputs.back();
                        }

                        if (pVal.HasMember("type") && pVal["type"].IsString()) {
                            targetPort->type = StringToPinType(pVal["type"].GetString());
                        }
                    }
                }

                if (!node->id.empty()) {
                    validNodeIds.insert(node->id);
                }
                outGraph.nodes.push_back(std::move(node));
            }
        }

        // 5. Links with Validation
        if (doc.HasMember("links") && doc["links"].IsArray()) {
            const auto& linkArr = doc["links"].GetArray();
            for (rapidjson::SizeType i = 0; i < linkArr.Size(); ++i) {
                const auto& lVal = linkArr[i];
                if (!lVal.IsObject()) continue;

                ShaderLink link;
                if (lVal.HasMember("id") && lVal["id"].IsString()) {
                    link.id = lVal["id"].GetString();
                }
                if (lVal.HasMember("fromNode") && lVal["fromNode"].IsString()) {
                    link.fromNodeId = lVal["fromNode"].GetString();
                }
                if (lVal.HasMember("fromPort") && lVal["fromPort"].IsString()) {
                    link.fromPortId = lVal["fromPort"].GetString();
                }
                if (lVal.HasMember("toNode") && lVal["toNode"].IsString()) {
                    link.toNodeId = lVal["toNode"].GetString();
                }
                if (lVal.HasMember("toPort") && lVal["toPort"].IsString()) {
                    link.toPortId = lVal["toPort"].GetString();
                }

                // Validation: Endpoints must exist
                if (validNodeIds.find(link.fromNodeId) == validNodeIds.end() ||
                    validNodeIds.find(link.toNodeId) == validNodeIds.end()) {
                    // Skip dangling link
                    continue;
                }

                outGraph.links.push_back(std::move(link));
            }
        }

        return true;
    }

    std::string ShaderGraphSerializer::ResolveGraphPath(const std::string& inputPath) {
        if (inputPath.empty()) return "";

        std::vector<std::string> candidates = {
            inputPath,
            inputPath + ".shadergraph"
        };

        std::string fileName = SlipFileSystem::GetFileName(inputPath);
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        std::string rootDir = SlipFileSystem::GetProjectRootPath();

        if (!assetsDir.empty()) {
            candidates.push_back(SlipFileSystem::CombinePath(assetsDir, inputPath));
            candidates.push_back(SlipFileSystem::CombinePath(assetsDir, inputPath + ".shadergraph"));
            candidates.push_back(SlipFileSystem::CombinePath(assetsDir, "materials/" + inputPath));
            candidates.push_back(SlipFileSystem::CombinePath(assetsDir, "materials/" + inputPath + ".shadergraph"));
            if (!fileName.empty() && fileName != inputPath) {
                candidates.push_back(SlipFileSystem::CombinePath(assetsDir, fileName));
                candidates.push_back(SlipFileSystem::CombinePath(assetsDir, fileName + ".shadergraph"));
                candidates.push_back(SlipFileSystem::CombinePath(assetsDir, "materials/" + fileName));
                candidates.push_back(SlipFileSystem::CombinePath(assetsDir, "materials/" + fileName + ".shadergraph"));
            }
        }

        if (!rootDir.empty()) {
            candidates.push_back(SlipFileSystem::CombinePath(rootDir, inputPath));
            candidates.push_back(SlipFileSystem::CombinePath(rootDir, inputPath + ".shadergraph"));
            candidates.push_back(SlipFileSystem::CombinePath(rootDir, "assets/" + inputPath));
            candidates.push_back(SlipFileSystem::CombinePath(rootDir, "assets/materials/" + inputPath));
        }

        // Relative fallbacks
        candidates.push_back("assets/materials/" + inputPath);
        candidates.push_back("assets/materials/" + inputPath + ".shadergraph");
        candidates.push_back("assets/" + inputPath);
        candidates.push_back("assets/" + inputPath + ".shadergraph");

        for (const auto& c : candidates) {
            if (SlipFileSystem::Exists(c)) {
                return SlipFileSystem::NormalizePath(c);
            }
        }

        return inputPath;
    }

    bool ShaderGraphSerializer::DeserializeFromFile(ShaderGraph& outGraph,
                                                    const std::string& filepath,
                                                    std::string* outErrorMessage) {
        std::string resolved = ResolveGraphPath(filepath);
        if (!SlipFileSystem::Exists(resolved)) {
            if (outErrorMessage) {
                *outErrorMessage = "Shader graph file does not exist: " + filepath;
            }
            return false;
        }

        std::string content;
        if (!SlipFileSystem::ReadAllText(resolved, content)) {
            if (outErrorMessage) {
                *outErrorMessage = "Failed to read shader graph file: " + resolved;
            }
            return false;
        }

        return DeserializeFromString(outGraph, content, outErrorMessage);
    }

} // namespace SlipShaderGraph
