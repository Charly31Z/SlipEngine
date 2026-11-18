# Milestone 1 Analysis: Shader Graph Serialization & Asset Pipeline Integration

**Agent**: `explorer_m1_2` (Serialization & Asset Pipeline Explorer)  
**Parent Orchestrator**: `af9ad2dc-4359-4d00-baf7-c2106d2d2f37`  
**Date**: 2026-11-08  
**Scope**: F08 (Asset File Serialization & Deserialization), F09 (Engine Asset Pipeline Registration), R2 (Shader Graph File Asset Type), F13 (Public / Exposed Parameters).

---

## 1. Executive Summary

This document provides the complete architectural design and production-ready C++ specifications for:
1. **`ShaderGraphSerializer`**: The lossless save/load engine for `.shadergraph` visual shader assets in SlipEngine (`include/SlipEngine/render/shadergraph/ShaderGraphSerializer.h` and `src/render/shadergraph/ShaderGraphSerializer.cpp`).
2. **Canonical Asset File Format (`.shadergraph`)**: A human-readable, schema-versioned, pretty-printed JSON format using **RapidJSON** (already integrated in SlipEngine's `vcpkg` dependencies for both `x64-windows` and `arm64-android`). We also provide a direct architectural comparison with SlipEngine's legacy indented text format (`.mat`/`.controller`), demonstrating why RapidJSON is decisively superior for arbitrary graph data structures.
3. **Asset Pipeline & GUID Integration**:
   - `SlipModelExtract` (`src/editor/SlipModelExtract.cpp`): Extension detection (`.shadergraph` -> `"ShaderGraph"`), companion `.shadergraph.meta` GUID generation, and metadata persistence.
   - `SlipFileSystem` (`src/tools/SlipFileSystem.cpp`): Asset discovery (`GetGuidForAsset`, `FindAssetByGuid`), recursive asset queries, and multi-directory resolution (`assets/`, `assets/materials/`, project root).
   - `ContentBrowserPanel` (`src/editor/panels/ContentBrowserPanel.cpp`): Right-click asset creation ("Nuevo Shader Graph"), asset double-click opening, and context menus.
   - `SlipMaterial` (`src/render/SlipMaterial.cpp`): Material persistence with `.shadergraph` references in `.mat` files.

---

## 2. Format Selection: RapidJSON vs Indented Text

SlipEngine's DISPATCH requested choosing between a clean human-readable text format (matching `.mat`/`.controller` style) or RapidJSON (already present in `vcpkg`).

### 2.1 Comparative Evaluation Matrix

| Metric | RapidJSON (Pretty-Printed) | Custom Indented Text (`.controller` style) |
|---|---|---|
| **Vcpkg Availability** | ✅ Verified in `x64-windows` & `arm64-android` (`<rapidjson/...>`) | ✅ Header-less standard C++ |
| **Header Dependency** | Pure header-only (`rapidjson/prettywriter.h`, `document.h`) | Standard Library only (`<sstream>`, `<string>`) |
| **String & Whitespace Safety** | ✅ 100% RFC 8259 compliant. Escapes quotes, spaces, tabs, newlines | ❌ Space-delimited line parsing breaks on spaces in names |
| **Nested Structure Fidelity** | ✅ Native arrays (`[]`) and objects (`{}`). Arbitrary depth | ⚠️ Indentation-sensitive; tab vs space errors corrupt parse |
| **Floating Point Precision** | ✅ Lossless float/double serialization with configurable precision | ⚠️ `std::stringstream` default precision (6 digits) can lose bits |
| **Schema Evolution & Versioning** | ✅ Forward/backward compatible; unknown keys safely ignored | ❌ Strict sequential line reading breaks on unexpected keys |
| **Error Diagnosis** | ✅ Returns byte offset and parse error code (`GetErrorOffset()`) | ⚠️ Requires manual line counting and error logging |
| **Industry Precedent** | ✅ Unity `.shadergraph` is JSON; Unreal clipboard/nodes are JSON | Unreal engine-specific text format (.uasset is binary) |
| **Tooling & Interop** | ✅ Native viewing/editing in VSCode, Notepad++, Python, Web | Requires custom parser or highlighter |

### 2.2 Decision: Canonical JSON with RapidJSON PrettyWriter
We adopt **RapidJSON** as the canonical serialization engine. By configuring RapidJSON's `PrettyWriter` with:
- Indentation: 2 spaces
- Format option: `kFormatSingleLineArray` for mathematical vectors (e.g. `[1.0, 0.5, 0.2, 1.0]` stays on a single line)
- Key-value structuring

The resulting `.shadergraph` file is clean, readable, diff-friendly in Git, and free of whitespace/tab corruption.

---

## 3. Data Model Alignment

Our serializer operates directly on the data structures specified in Milestone 1 contract (`ShaderGraphTypes.h`, `ShaderNode.h`, and `ShaderGraph.h`):

```cpp
namespace SlipEngine {
    enum class ShaderGraphType { Lit, Unlit };
    enum class PinType { Float, Vec2, Vec3, Vec4, Texture2D };

    struct ShaderProperty {
        std::string id;
        std::string name;
        std::string displayName;
        PinType type = PinType::Float;
        glm::vec4 defaultValue = glm::vec4(0.0f);
        std::string defaultTexturePath = "";
    };

    struct ShaderPort {
        std::string id;
        std::string name;
        PinType type = PinType::Float;
        bool isOutput = false;
        glm::vec4 defaultValue = glm::vec4(0.0f);
    };

    struct ShaderLink {
        std::string id;
        std::string fromNodeId;
        std::string fromPortId;
        std::string toNodeId;
        std::string toPortId;
    };
}
```

### 3.1 Node Custom Properties Abstraction
To serialize node-specific data (e.g. `PropertyNode` referencing `propertyId`, `TextureSamplerNode` storing texture paths, or constants) without coupling the serializer to every concrete `ShaderNode` subclass, `ShaderNode` exposes:
```cpp
std::unordered_map<std::string, std::string> customProperties;
virtual void OnCustomPropertiesUpdated() {}
```
The serializer writes `customProperties` as a JSON sub-object. Upon deserialization, keys are restored into `node->customProperties`, and `node->OnCustomPropertiesUpdated()` is invoked. This decouples the serializer from any present or future node types.

---

## 4. The `.shadergraph` Asset Format Specification

### 4.1 Schema Definition (Version 1)
Every `.shadergraph` asset file contains:
- `version`: Integer (currently `1`).
- `materialType`: `"Lit"` (PBR GGX/CSM surface) or `"Unlit"` (direct color/emissive).
- `properties`: Array of blackboard properties exposed to materials.
- `nodes`: Array of nodes with position, title, type, custom properties, and port default values.
- `links`: Array of directed connections from upstream output ports to downstream input ports.

### 4.2 Concrete Example File: `sample_animated_lit.shadergraph`
```json
{
  "version": 1,
  "materialType": "Lit",
  "properties": [
    {
      "id": "prop_albedo_color",
      "name": "BaseColor",
      "displayName": "Base Color",
      "type": "Vec4",
      "defaultValue": [0.8, 0.2, 0.2, 1.0],
      "defaultTexturePath": ""
    },
    {
      "id": "prop_wave_speed",
      "name": "WaveSpeed",
      "displayName": "Wave Speed",
      "type": "Float",
      "defaultValue": [2.5, 0.0, 0.0, 0.0],
      "defaultTexturePath": ""
    },
    {
      "id": "prop_main_texture",
      "name": "MainTex",
      "displayName": "Albedo Map",
      "type": "Texture2D",
      "defaultValue": [0.0, 0.0, 0.0, 0.0],
      "defaultTexturePath": "assets/textures/rock_albedo.png"
    }
  ],
  "nodes": [
    {
      "id": "node_master",
      "type": "MasterNode",
      "title": "Master (Lit)",
      "position": [750.0, 200.0],
      "customProperties": {
        "materialType": "Lit"
      },
      "inputs": [
        {
          "id": "in_albedo",
          "name": "Albedo",
          "type": "Vec3",
          "defaultValue": [1.0, 1.0, 1.0, 0.0]
        },
        {
          "id": "in_normal",
          "name": "Normal",
          "type": "Vec3",
          "defaultValue": [0.0, 0.0, 1.0, 0.0]
        },
        {
          "id": "in_roughness",
          "name": "Roughness",
          "type": "Float",
          "defaultValue": [0.35, 0.0, 0.0, 0.0]
        },
        {
          "id": "in_metallic",
          "name": "Metallic",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "in_emissive",
          "name": "Emissive",
          "type": "Vec3",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "in_ao",
          "name": "AO",
          "type": "Float",
          "defaultValue": [1.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "in_alpha",
          "name": "Alpha",
          "type": "Float",
          "defaultValue": [1.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "in_alpha_clip",
          "name": "AlphaClipThreshold",
          "type": "Float",
          "defaultValue": [0.5, 0.0, 0.0, 0.0]
        }
      ],
      "outputs": []
    },
    {
      "id": "node_time",
      "type": "TimeNode",
      "title": "Time",
      "position": [50.0, 120.0],
      "customProperties": {},
      "inputs": [],
      "outputs": [
        {
          "id": "out_time",
          "name": "Time",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "out_delta_time",
          "name": "DeltaTime",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "out_sin_time",
          "name": "SinTime",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        }
      ]
    },
    {
      "id": "node_prop_speed",
      "type": "PropertyNode",
      "title": "Wave Speed",
      "position": [50.0, 300.0],
      "customProperties": {
        "propertyId": "prop_wave_speed",
        "propertyName": "WaveSpeed",
        "propertyType": "Float"
      },
      "inputs": [],
      "outputs": [
        {
          "id": "out_prop_val",
          "name": "Out",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        }
      ]
    },
    {
      "id": "node_multiply",
      "type": "MathMultiplyNode",
      "title": "Multiply",
      "position": [280.0, 180.0],
      "customProperties": {},
      "inputs": [
        {
          "id": "in_a",
          "name": "A",
          "type": "Float",
          "defaultValue": [1.0, 0.0, 0.0, 0.0]
        },
        {
          "id": "in_b",
          "name": "B",
          "type": "Float",
          "defaultValue": [1.0, 0.0, 0.0, 0.0]
        }
      ],
      "outputs": [
        {
          "id": "out_result",
          "name": "Out",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        }
      ]
    },
    {
      "id": "node_sin",
      "type": "MathSinNode",
      "title": "Sine",
      "position": [460.0, 180.0],
      "customProperties": {},
      "inputs": [
        {
          "id": "in_x",
          "name": "In",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        }
      ],
      "outputs": [
        {
          "id": "out_result",
          "name": "Out",
          "type": "Float",
          "defaultValue": [0.0, 0.0, 0.0, 0.0]
        }
      ]
    }
  ],
  "links": [
    {
      "id": "link_01",
      "fromNode": "node_time",
      "fromPort": "out_time",
      "toNode": "node_multiply",
      "toPort": "in_a"
    },
    {
      "id": "link_02",
      "fromNode": "node_prop_speed",
      "fromPort": "out_prop_val",
      "toNode": "node_multiply",
      "toPort": "in_b"
    },
    {
      "id": "link_03",
      "fromNode": "node_multiply",
      "fromPort": "out_result",
      "toNode": "node_sin",
      "toPort": "in_x"
    },
    {
      "id": "link_04",
      "fromNode": "node_sin",
      "fromPort": "out_result",
      "toNode": "node_master",
      "toPort": "in_metallic"
    }
  ]
}
```

---

## 5. C++ Architecture: `ShaderGraphSerializer`

### 5.1 Public Header: `include/SlipEngine/render/shadergraph/ShaderGraphSerializer.h`

```cpp
#pragma once

#ifndef SLIP_SHADER_GRAPH_SERIALIZER_H
#define SLIP_SHADER_GRAPH_SERIALIZER_H

#include "SlipEngine/core/SlipExport.h"
#include <string>

namespace SlipEngine {

    class ShaderGraph;

    class SLIPENGINE_API ShaderGraphSerializer {
    public:
        // Current file schema version
        static constexpr int CURRENT_SCHEMA_VERSION = 1;

        // Serialization
        static bool SerializeToString(const ShaderGraph& graph, std::string& outJson);
        static bool SerializeToFile(const ShaderGraph& graph, const std::string& filepath);

        // Deserialization
        static bool DeserializeFromString(ShaderGraph& outGraph, 
                                          const std::string& json, 
                                          std::string* outErrorMessage = nullptr);
        static bool DeserializeFromFile(ShaderGraph& outGraph, 
                                        const std::string& filepath, 
                                        std::string* outErrorMessage = nullptr);

        // Asset Resolution
        static std::string ResolveGraphPath(const std::string& relativeOrFullPath);
    };

} // namespace SlipEngine

#endif // SLIP_SHADER_GRAPH_SERIALIZER_H
```

### 5.2 Implementation: `src/render/shadergraph/ShaderGraphSerializer.cpp`

```cpp
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

namespace SlipEngine {

// Helper: Convert PinType to string
static const char* PinTypeToCString(PinType type) {
    switch (type) {
        case PinType::Float:     return "Float";
        case PinType::Vec2:      return "Vec2";
        case PinType::Vec3:      return "Vec3";
        case PinType::Vec4:      return "Vec4";
        case PinType::Texture2D: return "Texture2D";
        default:                 return "Float";
    }
}

// Helper: Convert string to PinType with safe default
static PinType CStringToPinType(const char* str) {
    if (!str) return PinType::Float;
    std::string s(str);
    if (s == "Vec2")      return PinType::Vec2;
    if (s == "Vec3")      return PinType::Vec3;
    if (s == "Vec4")      return PinType::Vec4;
    if (s == "Texture2D") return PinType::Texture2D;
    return PinType::Float;
}

// Helper: Convert ShaderGraphType to string
static const char* GraphTypeToCString(ShaderGraphType type) {
    return (type == ShaderGraphType::Unlit) ? "Unlit" : "Lit";
}

// Helper: Convert string to ShaderGraphType
static ShaderGraphType CStringToGraphType(const char* str) {
    if (str && std::string(str) == "Unlit") return ShaderGraphType::Unlit;
    return ShaderGraphType::Lit;
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
    writer.String(GraphTypeToCString(graph.materialType));

    // 3. Properties (Blackboard)
    writer.Key("properties");
    writer.StartArray();
    for (const auto& prop : graph.properties) {
        writer.StartObject();
        writer.Key("id"); writer.String(prop.id.c_str());
        writer.Key("name"); writer.String(prop.name.c_str());
        writer.Key("displayName"); writer.String(prop.displayName.c_str());
        writer.Key("type"); writer.String(PinTypeToCString(prop.type));
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
            writer.Key("type"); writer.String(PinTypeToCString(port.type));
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
            writer.Key("type"); writer.String(PinTypeToCString(port.type));
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
    if (version > CURRENT_SCHEMA_VERSION) {
        // Log warning for forward schema; proceed with best effort
    }

    // 2. Material Type
    if (doc.HasMember("materialType") && doc["materialType"].IsString()) {
        outGraph.materialType = CStringToGraphType(doc["materialType"].GetString());
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
                prop.type = CStringToPinType(pVal["type"].GetString());
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
            std::unique_ptr<ShaderNode> node = ShaderNodeRegistry::Get().CreateNode(typeName);
            if (!node) {
                // Unknown node fallback: instantiate generic ShaderNode
                node = std::make_unique<ShaderNode>();
                node->typeName = typeName;
                node->title = typeName + " (Unknown)";
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

                    // Find matching port in node factory inputs
                    ShaderPort* targetPort = nullptr;
                    for (auto& inp : node->inputs) {
                        if (!portId.empty() && inp.id == portId) { targetPort = &inp; break; }
                        if (!portName.empty() && inp.name == portName) { targetPort = &inp; break; }
                    }

                    if (!targetPort) {
                        // Dynamic or serialized extra port: append
                        ShaderPort extraPort;
                        extraPort.id = portId.empty() ? ("in_" + std::to_string(node->inputs.size())) : portId;
                        extraPort.name = portName.empty() ? extraPort.id : portName;
                        extraPort.isOutput = false;
                        node->inputs.push_back(extraPort);
                        targetPort = &node->inputs.back();
                    }

                    if (pVal.HasMember("type") && pVal["type"].IsString()) {
                        targetPort->type = CStringToPinType(pVal["type"].GetString());
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

            // Restore output ports (ensure port IDs match serialized IDs)
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
                // Skip dangling link referencing non-existent nodes
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

    // Default search fallbacks
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

} // namespace SlipEngine
```

---

## 6. Engine Asset Pipeline Integration

### 6.1 `src/editor/SlipModelExtract.cpp` Integration
`SlipModelExtract` is SlipEngine's asset metadata manager. It automatically inspects raw files, assigns GUIDs, and generates companion `.meta` files.

#### Change 1: `DeduceAssetType`
Add recognition of the `.shadergraph` extension:
```cpp
// In src/editor/SlipModelExtract.cpp, line 70
    if (ext == ".vert" || ext == ".frag" || ext == ".geom" || ext == ".glsl") {
        return "Shader";
    }
    if (ext == ".shadergraph") {
        return "ShaderGraph";
    }
    if (ext == ".scene" || ext == ".level" || ext == ".bsp") {
        return "Scene";
    }
```

#### Change 2: `SaveMeta` & `EnsureMetaForFile`
When `EnsureMetaForFile("assets/materials/Water.shadergraph")` is called, `SaveMeta` outputs:
```yaml
guid: 3f8a9b21-4c7e-40d2-9a11-82e4f012b3c4
type: ShaderGraph
originalFile: Water.shadergraph
fileSize: 4210
```
Because `SaveMeta` already writes `guid`, `type`, `originalFile`, and `fileSize` for any asset type not equal to `"Model"` or `"Texture"`, no extra fields are required.

### 6.2 `src/tools/SlipFileSystem.cpp` Integration
`SlipFileSystem` provides GUID resolution for assets across the project:
1. **`GetGuidForAsset(assetPath)`**: Reads `<assetPath>.meta` and extracts `guid: ...`. Works transparently for any file path ending in `.shadergraph`.
2. **`FindAssetByGuid(guid)`**: Recursively scans `.meta` files in `assets/` and returns `<path_without_meta>`. Works transparently for `ShaderGraph` assets.

### 6.3 `src/editor/panels/ContentBrowserPanel.cpp` Integration
To expose `.shadergraph` assets to users in the SlipEngine editor:

1. **Filtering `.meta` Files**: Line 82 already hides all `.meta` files:
   ```cpp
   if (!entry.isDirectory && entry.extension == ".meta") continue;
   ```
2. **Asset Double-Click Handler** (around line 628):
   ```cpp
   } else if (entry.extension == ".shadergraph") {
       if (SlipEditor::GetInstance()) {
           SlipEditor::GetInstance()->OpenShaderGraph(entry.fullPath);
       }
   }
   ```
3. **Item Context Menu** (around line 493 and line 685):
   ```cpp
   } else if (entry.extension == ".shadergraph") {
       if (ImGui::MenuItem("Editar en Shader Graph")) {
           if (SlipEditor::GetInstance()) {
               SlipEditor::GetInstance()->OpenShaderGraph(entry.fullPath);
           }
       }
       ImGui::Separator();
   }
   ```
4. **Folder Context Menu ("Nuevo Shader Graph")** (around line 753):
   ```cpp
   if (ImGui::MenuItem("Nuevo Shader Graph (.shadergraph)")) {
       std::string createdGraph = CreateShaderGraphAsset("NuevoShaderGraph");
       if (!createdGraph.empty()) {
           OpenRenameModal(createdGraph);
       }
   }
   ```
5. **Asset Factory Implementation**:
   ```cpp
   std::string ContentBrowserPanel::CreateShaderGraphAsset(const std::string& baseName) {
       std::string name = baseName;
       std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, name + ".shadergraph");
       int counter = 1;
       while (SlipFileSystem::Exists(path)) {
           path = SlipFileSystem::CombinePath(m_CurrentDirectory, name + "_" + std::to_string(counter++) + ".shadergraph");
       }

       ShaderGraph graph;
       graph.InitializeDefaultGraph(); // Spawns MasterNode (Lit)
       ShaderGraphSerializer::SerializeToFile(graph, path);
       SlipModelExtract::EnsureMetaForFile(path);
       RefreshCurrentDirectory();
       return path;
   }
   ```

### 6.4 `SlipMaterial` (.mat) File Persistence Integration (For Milestone 2)
In `SlipMaterial.h`:
```cpp
std::string shaderGraphPath = "";
std::unordered_map<std::string, float> graphFloatParams;
std::unordered_map<std::string, glm::vec4> graphVectorParams;
std::unordered_map<std::string, std::string> graphTextureParams;
```
In `src/render/SlipMaterial.cpp`:
- **Saving**:
  ```cpp
  if (!shaderGraphPath.empty()) {
      ss << "shaderGraph: " << shaderGraphPath << "\n";
  }
  ```
- **Loading**:
  ```cpp
  else if (key == "shaderGraph" || key == "shaderGraphPath") {
      shaderGraphPath = val;
      LoadShaderGraph(val);
  }
  ```

---

## 7. Resilience & Robustness Architecture

1. **Non-Destructive Deserialization**:
   If `DeserializeFromString` fails midway (e.g. malformed JSON), it returns `false` with a descriptive error message without leaving the target `outGraph` in a half-configured state.
2. **Missing Property Fallbacks**:
   If an older `.shadergraph` file lacks new fields (e.g. `displayName` or `defaultTexturePath`), safe default values are assigned.
3. **Unknown Node Tolerance**:
   If a node has an unrecognized `typeName` (e.g. from an experimental plugin), a generic `ShaderNode` is instantiated. All input/output ports and `customProperties` are preserved so saving does not drop the node.
4. **Dangling Link Sanitization**:
   Links connecting to deleted or non-existent nodes are safely ignored rather than crashing the engine.
5. **Atomic Disk Writes**:
   `SerializeToFile` writes to an in-memory `std::string` first. If JSON serialization succeeds, it calls `SlipFileSystem::WriteAllText(filepath, json)`. This prevents file truncation if an error occurs during graph traversal.

---

## 8. Verification Strategy & Test Cases

For Milestone 1 and Milestone 4 test suites:

### 8.1 Round-Trip Serialization Test (`Test_ShaderGraph_SerializationRoundTrip`)
1. Create a `ShaderGraph` instance.
2. Configure `materialType = ShaderGraphType::Lit`.
3. Add 4 properties: `Float`, `Vec2`, `Vec4`, `Texture2D`.
4. Add 5 nodes: `TimeNode`, `PropertyNode`, `MathMultiplyNode`, `MathSinNode`, `MasterNode`.
5. Connect 4 links:
   - `Time.Time` -> `Multiply.A`
   - `Property.Out` -> `Multiply.B`
   - `Multiply.Out` -> `Sin.In`
   - `Sin.Out` -> `Master.Metallic`
6. Serialize to string (`jsonStrA`).
7. Deserialize into a second `ShaderGraph` (`graphB`).
8. Assertions:
   - `graphB.materialType == ShaderGraphType::Lit`
   - `graphB.properties.size() == 4`
   - `graphB.nodes.size() == 5`
   - `graphB.links.size() == 4`
   - Link endpoints match exactly.
9. Serialize `graphB` to string (`jsonStrB`).
10. Verify idempotency: `jsonStrA == jsonStrB`.

### 8.2 Malformed Input Resilience Test (`Test_ShaderGraph_CorruptFileHandling`)
1. Test with empty string `""` -> returns `false`, `outErrorMessage` populated.
2. Test with truncated JSON `{"version": 1, "nodes": [` -> returns `false`, reports parse error with offset.
3. Test with missing node link `{"links": [{"fromNode": "missing_node"}]}` -> deserializes gracefully, drops invalid link, doesn't crash.
4. Test with non-existent file path -> returns `false`, does not crash.
