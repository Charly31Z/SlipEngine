#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <cassert>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>
#include <iomanip>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include "SlipEngine/tools/SlipFileSystem.h"

using namespace SlipShaderGraph;

// Test framework macros
struct StressTestStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

static StressTestStats g_Stats;

#define STRESS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n    [FAIL] Line " << __LINE__ << ": " << msg << " (" #cond ")" << std::endl; \
            return false; \
        } \
    } while(0)

#define RUN_STRESS_TEST(name, func) \
    do { \
        g_Stats.total++; \
        std::cout << "  [STRESS TEST] " << name << " ... " << std::flush; \
        auto start = std::chrono::high_resolution_clock::now(); \
        bool res = func(); \
        auto end = std::chrono::high_resolution_clock::now(); \
        double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count(); \
        if (res) { \
            g_Stats.passed++; \
            std::cout << "[PASS] (" << std::fixed << std::setprecision(2) << elapsedMs << " ms)" << std::endl; \
        } else { \
            g_Stats.failed++; \
            std::cout << "[FAILED] (" << std::fixed << std::setprecision(2) << elapsedMs << " ms)" << std::endl; \
        } \
    } while(0)

// ============================================================================
// Suite 1: Deep & Large Graphs (100+ to 500+ Nodes)
// ============================================================================

bool Test_LargeGraph_LinearChain_150Nodes() {
    ShaderGraph graph;
    // Default graph has MasterNode
    MasterNode* master = graph.GetMasterNode();
    STRESS_ASSERT(master != nullptr, "MasterNode should exist by default");

    const int CHAIN_LENGTH = 150;
    std::vector<ShaderNode*> chainNodes;
    chainNodes.reserve(CHAIN_LENGTH);

    for (int i = 0; i < CHAIN_LENGTH; ++i) {
        ShaderNode* node = graph.AddNode("Math_Add", static_cast<float>(i * 50), 100.0f);
        STRESS_ASSERT(node != nullptr, "Node creation failed in chain");
        chainNodes.push_back(node);
    }

    STRESS_ASSERT(graph.nodes.size() == (CHAIN_LENGTH + 1), "Graph should contain 151 nodes");

    // Link chain: node_0 -> node_1 -> ... -> node_149
    for (int i = 0; i < CHAIN_LENGTH - 1; ++i) {
        bool linked = graph.AddLink(chainNodes[i]->id, "Out", chainNodes[i + 1]->id, "A");
        STRESS_ASSERT(linked, "Failed to link nodes in chain");
    }

    // Link last node to MasterNode.Albedo
    bool linkToMaster = graph.AddLink(chainNodes.back()->id, "Out", master->id, "Albedo");
    STRESS_ASSERT(linkToMaster, "Failed to link last chain node to MasterNode");
    STRESS_ASSERT(graph.links.size() == CHAIN_LENGTH, "Graph should contain exactly 150 links");

    // Cycle check on deep linear graph
    STRESS_ASSERT(!graph.HasCycles(), "Linear chain graph should have NO cycles");

    return true;
}

bool Test_LargeGraph_CycleDetection_Rollback() {
    ShaderGraph graph;
    const int CHAIN_LENGTH = 150;
    std::vector<ShaderNode*> chainNodes;
    for (int i = 0; i < CHAIN_LENGTH; ++i) {
        chainNodes.push_back(graph.AddNode("Math_Add"));
    }

    for (int i = 0; i < CHAIN_LENGTH - 1; ++i) {
        graph.AddLink(chainNodes[i]->id, "Out", chainNodes[i + 1]->id, "A");
    }

    STRESS_ASSERT(graph.links.size() == CHAIN_LENGTH - 1, "Expected 149 links");
    STRESS_ASSERT(!graph.HasCycles(), "Linear chain must be acyclic");

    // Attempt to close cycle from last node back to first node: chainNodes[149] -> chainNodes[0]
    bool cycleAdded = graph.AddLink(chainNodes[CHAIN_LENGTH - 1]->id, "Out", chainNodes[0]->id, "B");
    STRESS_ASSERT(!cycleAdded, "AddLink should reject adding a cycle across 150 nodes");
    STRESS_ASSERT(graph.links.size() == CHAIN_LENGTH - 1, "Link count should remain unchanged after cycle rejection");
    STRESS_ASSERT(!graph.HasCycles(), "Graph must remain acyclic after rejected cycle");

    return true;
}

bool Test_LargeGraph_SelfLoop_Rejection() {
    ShaderGraph graph;
    ShaderNode* add = graph.AddNode("Math_Add");
    STRESS_ASSERT(add != nullptr, "Failed to add node");

    bool loopAdded = graph.AddLink(add->id, "Out", add->id, "A");
    STRESS_ASSERT(!loopAdded, "AddLink must reject self-loop (fromNode == toNode)");
    STRESS_ASSERT(graph.links.empty(), "Links list must remain empty after rejected self-loop");

    return true;
}

bool Test_LargeGraph_WideBinaryTree_127Nodes() {
    // 7 levels = 127 nodes (binary tree)
    ShaderGraph graph;
    const int NUM_NODES = 127;
    std::vector<ShaderNode*> treeNodes;
    treeNodes.reserve(NUM_NODES);

    for (int i = 0; i < NUM_NODES; ++i) {
        ShaderNode* node = graph.AddNode("Math_Add");
        treeNodes.push_back(node);
    }

    // Connect binary tree: children (2*i + 1) -> parent i port A, (2*i + 2) -> parent i port B
    int linksCreated = 0;
    for (int i = 0; i < NUM_NODES / 2; ++i) {
        int leftChild = 2 * i + 1;
        int rightChild = 2 * i + 2;

        if (leftChild < NUM_NODES) {
            bool l1 = graph.AddLink(treeNodes[leftChild]->id, "Out", treeNodes[i]->id, "A");
            STRESS_ASSERT(l1, "Failed to link left child");
            linksCreated++;
        }
        if (rightChild < NUM_NODES) {
            bool l2 = graph.AddLink(treeNodes[rightChild]->id, "Out", treeNodes[i]->id, "B");
            STRESS_ASSERT(l2, "Failed to link right child");
            linksCreated++;
        }
    }

    STRESS_ASSERT(graph.links.size() == static_cast<size_t>(linksCreated), "Tree links mismatch");
    STRESS_ASSERT(!graph.HasCycles(), "Binary tree must have NO cycles");

    // Connect root (treeNodes[0]) to MasterNode.Roughness
    MasterNode* master = graph.GetMasterNode();
    bool linkToMaster = graph.AddLink(treeNodes[0]->id, "Out", master->id, "Roughness");
    STRESS_ASSERT(linkToMaster, "Failed to link tree root to master node");
    STRESS_ASSERT(!graph.HasCycles(), "Tree attached to master must have NO cycles");

    return true;
}

bool Test_LargeGraph_BulkHubRemoval_CascadingLinks() {
    ShaderGraph graph;
    ShaderNode* hub = graph.AddNode("Math_Multiply");
    const int SATELLITES = 50;
    std::vector<ShaderNode*> sats;
    for (int i = 0; i < SATELLITES; ++i) {
        sats.push_back(graph.AddNode("Math_Add"));
    }

    // Connect 50 satellites from their Out to hub's A (note: single-input rule replaces, so connect Out to multiple inputs on satellites from hub)
    // Hub feeds all 50 satellites: hub.Out -> sat[i].A
    for (int i = 0; i < SATELLITES; ++i) {
        bool ok = graph.AddLink(hub->id, "Out", sats[i]->id, "A");
        STRESS_ASSERT(ok, "Failed to link hub to satellite");
    }

    STRESS_ASSERT(graph.links.size() == SATELLITES, "Expected 50 links from hub");

    // Remove the central hub node
    bool removed = graph.RemoveNode(hub->id);
    STRESS_ASSERT(removed, "Failed to remove hub node");
    STRESS_ASSERT(graph.FindNode(hub->id) == nullptr, "Hub node should no longer exist");
    STRESS_ASSERT(graph.links.empty(), "All 50 links connected to hub should be cascade-deleted");

    // Remaining nodes must be 50 satellites + 1 MasterNode
    STRESS_ASSERT(graph.nodes.size() == SATELLITES + 1, "Graph should retain non-hub nodes");

    return true;
}

bool Test_LargeGraph_MassiveGraph_500Nodes() {
    ShaderGraph graph;
    const int TOTAL_NODES = 500;
    const char* types[] = {
        "Math_Add", "Math_Multiply", "Procedural_Clamp", "Procedural_Noise",
        "Time_Time", "Transform_CameraPosition", "Vector_Split", "Vector_Combine"
    };
    const int numTypes = sizeof(types) / sizeof(types[0]);

    std::vector<ShaderNode*> nodes;
    nodes.reserve(TOTAL_NODES);

    for (int i = 0; i < TOTAL_NODES; ++i) {
        ShaderNode* n = graph.AddNode(types[i % numTypes]);
        STRESS_ASSERT(n != nullptr, "Failed to instantiate node in massive graph");
        nodes.push_back(n);
    }

    // Connect a random-ish forward DAG without back-edges
    int linksCount = 0;
    for (int i = 0; i < TOTAL_NODES - 1; i += 2) {
        if (!nodes[i]->outputs.empty() && !nodes[i + 1]->inputs.empty()) {
            if (graph.AddLink(nodes[i]->id, nodes[i]->outputs[0].id, nodes[i + 1]->id, nodes[i + 1]->inputs[0].id)) {
                linksCount++;
            }
        }
    }

    STRESS_ASSERT(!graph.HasCycles(), "Forward-only massive DAG must have NO cycles");
    STRESS_ASSERT(graph.nodes.size() == TOTAL_NODES + 1, "Total nodes should match");

    // Test clean destruction
    graph.Clear();
    STRESS_ASSERT(graph.nodes.empty(), "Nodes should be empty after Clear()");
    STRESS_ASSERT(graph.links.empty(), "Links should be empty after Clear()");

    return true;
}

// ============================================================================
// Suite 2: Serialization Round-Trip Idempotency
// ============================================================================

bool Test_Serialization_ComprehensiveRoundTrip_ByteForByte() {
    ShaderGraph graphA;
    graphA.SetMaterialType(ShaderGraphType::Lit);

    // 1. Add Blackboard properties covering all PinTypes
    graphA.AddProperty("baseColor", PinType::Vec4, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
    graphA.AddProperty("roughnessVal", PinType::Float, glm::vec4(0.42f, 0.0f, 0.0f, 0.0f));
    graphA.AddProperty("normalScale", PinType::Vec2, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
    graphA.AddProperty("worldOffset", PinType::Vec3, glm::vec4(0.0f, 10.0f, -5.0f, 0.0f));
    graphA.AddProperty("mainTex", PinType::Texture2D, glm::vec4(0.0f), "textures/brick_diffuse.png");

    // 2. Add 10 diverse nodes
    ShaderNode* nAdd = graphA.AddNode("Math_Add", 100.0f, 150.0f);
    ShaderNode* nSin = graphA.AddNode("Math_Sin", 250.0f, 150.0f);
    ShaderNode* nClamp = graphA.AddNode("Procedural_Clamp", 400.0f, 150.0f);
    ShaderNode* nTime = graphA.AddNode("Time_Time", 50.0f, 300.0f);
    ShaderNode* nCam = graphA.AddNode("Transform_CameraPosition", 50.0f, 450.0f);
    ShaderNode* nNorm = graphA.AddNode("Transform_WorldNormal", 50.0f, 600.0f);
    ShaderNode* nNoise = graphA.AddNode("Procedural_Noise", 200.0f, 400.0f);
    ShaderNode* nTex = graphA.AddNode("Texture_Sampler", 300.0f, 500.0f);
    ShaderNode* nSplit = graphA.AddNode("Vector_Split", 500.0f, 300.0f);
    ShaderNode* nConst = graphA.AddNode("Input_Vec4", 100.0f, 700.0f);

    STRESS_ASSERT(nAdd && nSin && nClamp && nTime && nCam && nNorm && nNoise && nTex && nSplit && nConst,
                  "All nodes must instantiate");

    // Set custom properties
    nTex->customProperties["wrapMode"] = "Repeat";
    nTex->customProperties["filter"] = "Linear";
    nConst->customProperties["value"] = "0.1, 0.2, 0.3, 1.0";

    // Set custom default values on node input ports
    nAdd->inputs[0].defaultValue = glm::vec4(1.5f, 0.0f, 0.0f, 0.0f);
    nAdd->inputs[1].defaultValue = glm::vec4(2.5f, 0.0f, 0.0f, 0.0f);

    // 3. Add Links
    MasterNode* master = graphA.GetMasterNode();
    STRESS_ASSERT(master != nullptr, "MasterNode must exist");

    graphA.AddLink(nTime->id, "Time", nSin->id, "In");
    graphA.AddLink(nSin->id, "Out", nClamp->id, "In");
    graphA.AddLink(nClamp->id, "Out", master->id, "Roughness");
    graphA.AddLink(nNorm->id, "Normal", master->id, "Normal");

    // 4. First Serialization: GraphA -> JSON_A
    std::string jsonA;
    bool s1 = ShaderGraphSerializer::SerializeToString(graphA, jsonA);
    STRESS_ASSERT(s1 && !jsonA.empty(), "Serialization of GraphA failed");

    // 5. First Deserialization: JSON_A -> GraphB
    ShaderGraph graphB;
    std::string errB;
    bool d1 = ShaderGraphSerializer::DeserializeFromString(graphB, jsonA, &errB);
    STRESS_ASSERT(d1, ("Deserialization into GraphB failed: " + errB).c_str());

    // Verify GraphB topology matches GraphA
    STRESS_ASSERT(graphB.materialType == graphA.materialType, "Material type mismatch");
    STRESS_ASSERT(graphB.properties.size() == graphA.properties.size(), "Properties count mismatch");
    STRESS_ASSERT(graphB.nodes.size() == graphA.nodes.size(), "Nodes count mismatch");
    STRESS_ASSERT(graphB.links.size() == graphA.links.size(), "Links count mismatch");

    // 6. Second Serialization: GraphB -> JSON_B
    std::string jsonB;
    bool s2 = ShaderGraphSerializer::SerializeToString(graphB, jsonB);
    STRESS_ASSERT(s2 && !jsonB.empty(), "Serialization of GraphB failed");

    // 7. ASSERT IDEMPOTENCY: JSON_A == JSON_B
    if (jsonA != jsonB) {
        std::cerr << "\n[MISMATCH] JSON_A length = " << jsonA.length() << ", JSON_B length = " << jsonB.length() << std::endl;
        for (size_t i = 0; i < std::min(jsonA.length(), jsonB.length()); ++i) {
            if (jsonA[i] != jsonB[i]) {
                std::cerr << "First difference at char " << i << ": '" << jsonA[i] << "' vs '" << jsonB[i] << "'" << std::endl;
                break;
            }
        }
        return false;
    }

    // 8. Third cycle: JSON_B -> GraphC -> JSON_C
    ShaderGraph graphC;
    std::string errC;
    bool d2 = ShaderGraphSerializer::DeserializeFromString(graphC, jsonB, &errC);
    STRESS_ASSERT(d2, ("Deserialization into GraphC failed: " + errC).c_str());

    std::string jsonC;
    bool s3 = ShaderGraphSerializer::SerializeToString(graphC, jsonC);
    STRESS_ASSERT(s3 && !jsonC.empty(), "Serialization of GraphC failed");

    STRESS_ASSERT(jsonB == jsonC, "Second round-trip idempotency failure: JSON_B != JSON_C");

    return true;
}

bool Test_Serialization_SpecialCharactersAndUnicode() {
    ShaderGraph graph;
    ShaderProperty* p = graph.AddProperty("espacio_y_acentos", PinType::Vec3, glm::vec4(0.0f));
    p->displayName = "Color Superficie / Lit (EspaÃ±ol & SÃ­mbolos: <>&'\"\\)";

    ShaderNode* n = graph.AddNode("Math_Add");
    n->title = "Nodo AdiciÃ³n / Suma (æ—¥æœ¬èªžãƒ»FranÃ§ais)";
    n->customProperties["description"] = "Multi-line\nDescription\tWith \"Quotes\" and \\slashes/";

    std::string json;
    STRESS_ASSERT(ShaderGraphSerializer::SerializeToString(graph, json), "Failed to serialize unicode");

    ShaderGraph loaded;
    std::string err;
    STRESS_ASSERT(ShaderGraphSerializer::DeserializeFromString(loaded, json, &err), ("Failed to load unicode: " + err).c_str());

    const ShaderProperty* loadedP = loaded.FindProperty(p->id);
    STRESS_ASSERT(loadedP != nullptr, "Property missing after unicode round-trip");
    STRESS_ASSERT(loadedP->displayName == p->displayName, "Unicode display name corrupted in round-trip");

    ShaderNode* loadedN = loaded.FindNode(n->id);
    STRESS_ASSERT(loadedN != nullptr, "Node missing after unicode round-trip");
    STRESS_ASSERT(loadedN->title == n->title, "Unicode title corrupted in round-trip");
    STRESS_ASSERT(loadedN->customProperties["description"] == n->customProperties["description"],
                  "Special characters in customProperties corrupted");

    return true;
}

bool Test_Serialization_ExtremeFloatingPointValues() {
    ShaderGraph graph;
    glm::vec4 extremeVal(1e-6f, 1e6f, -3.14159265f, 0.0f);
    graph.AddProperty("extremeProp", PinType::Vec4, extremeVal);

    std::string json;
    STRESS_ASSERT(ShaderGraphSerializer::SerializeToString(graph, json), "Serialization failed");

    ShaderGraph loaded;
    std::string err;
    STRESS_ASSERT(ShaderGraphSerializer::DeserializeFromString(loaded, json, &err), "Deserialization failed");

    const ShaderProperty* p = loaded.FindPropertyByName("extremeProp");
    STRESS_ASSERT(p != nullptr, "Extreme property missing");
    STRESS_ASSERT(std::abs(p->defaultValue.x - extremeVal.x) < 1e-7f, "Extreme float x mismatch");
    STRESS_ASSERT(std::abs(p->defaultValue.y - extremeVal.y) < 1.0f, "Extreme float y mismatch");
    STRESS_ASSERT(std::abs(p->defaultValue.z - extremeVal.z) < 1e-5f, "Extreme float z mismatch");
    STRESS_ASSERT(p->defaultValue.w == 0.0f, "Zero float w mismatch");

    return true;
}

bool Test_Serialization_DiskFile_RoundTrip() {
    ShaderGraph graph;
    graph.AddProperty("tint", PinType::Vec3, glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
    ShaderNode* nAdd = graph.AddNode("Math_Add", 100.0f, 200.0f);
    MasterNode* master = graph.GetMasterNode();
    graph.AddLink(nAdd->id, "Out", master->id, "Albedo");

    const std::string tempPath = "build/temp_stress_roundtrip.shadergraph";
    bool saved = ShaderGraphSerializer::SaveToFile(graph, tempPath);
    STRESS_ASSERT(saved, "Failed to save graph to disk");

    ShaderGraph loaded;
    std::string err;
    bool loadedOk = ShaderGraphSerializer::LoadFromFile(loaded, tempPath, &err);
    STRESS_ASSERT(loadedOk, ("Failed to load graph from disk: " + err).c_str());

    STRESS_ASSERT(loaded.nodes.size() == graph.nodes.size(), "Node count mismatch from file");
    STRESS_ASSERT(loaded.links.size() == graph.links.size(), "Link count mismatch from file");

    // Clean up
    std::remove(tempPath.c_str());
    return true;
}

// ============================================================================
// Suite 3: Dynamic MasterNode Mode Switching Stress
// ============================================================================

bool Test_MasterNode_RapidModeSwitching_50Iterations() {
    ShaderGraph graph;
    MasterNode* master = graph.GetMasterNode();
    STRESS_ASSERT(master != nullptr, "MasterNode must exist");

    for (int i = 0; i < 50; ++i) {
        // Switch to Unlit
        graph.SetMaterialType(ShaderGraphType::Unlit);
        STRESS_ASSERT(graph.materialType == ShaderGraphType::Unlit, "Material type should be Unlit");
        STRESS_ASSERT(master->materialType == ShaderGraphType::Unlit, "MasterNode type should be Unlit");
        STRESS_ASSERT(master->inputs.size() == 4, "Unlit MasterNode must have exactly 4 inputs");
        STRESS_ASSERT(master->FindInput("Color") != nullptr, "Unlit must have 'Color' input");
        STRESS_ASSERT(master->FindInput("Albedo") == nullptr, "Unlit must NOT have 'Albedo' input");

        // Switch to Lit
        graph.SetMaterialType(ShaderGraphType::Lit);
        STRESS_ASSERT(graph.materialType == ShaderGraphType::Lit, "Material type should be Lit");
        STRESS_ASSERT(master->materialType == ShaderGraphType::Lit, "MasterNode type should be Lit");
        STRESS_ASSERT(master->inputs.size() == 8, "Lit MasterNode must have exactly 8 inputs");
        STRESS_ASSERT(master->FindInput("Albedo") != nullptr, "Lit must have 'Albedo' input");
        STRESS_ASSERT(master->FindInput("Color") == nullptr, "Lit must NOT have 'Color' input");
    }

    return true;
}

bool Test_MasterNode_RapidModeSwitching_1000Iterations_NoLeak() {
    ShaderGraph graph;
    for (int i = 0; i < 1000; ++i) {
        graph.SetMaterialType((i % 2 == 0) ? ShaderGraphType::Unlit : ShaderGraphType::Lit);
    }
    STRESS_ASSERT(graph.GetMasterNode() != nullptr, "MasterNode must still be valid after 1000 switches");
    return true;
}

bool Test_MasterNode_LinkBehaviorAcrossModeSwitching() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderNode* colNode = graph.AddNode("Math_Add");
    ShaderNode* emiNode = graph.AddNode("Math_Multiply");

    // In Lit mode: connect colNode -> Albedo, emiNode -> Emissive
    bool l1 = graph.AddLink(colNode->id, "Out", master->id, "Albedo");
    bool l2 = graph.AddLink(emiNode->id, "Out", master->id, "Emissive");
    STRESS_ASSERT(l1 && l2, "Failed to link in Lit mode");
    STRESS_ASSERT(graph.links.size() == 2, "Expected 2 links in Lit mode");

    // Switch to Unlit mode: Albedo disappears, Emissive stays
    graph.SetMaterialType(ShaderGraphType::Unlit);
    STRESS_ASSERT(master->FindInput("Albedo") == nullptr, "Albedo should not exist in Unlit");
    STRESS_ASSERT(master->FindInput("Emissive") != nullptr, "Emissive should exist in Unlit");

    // Can we connect colNode -> Color in Unlit mode?
    bool l3 = graph.AddLink(colNode->id, "Out", master->id, "Color");
    STRESS_ASSERT(l3, "Failed to link to Color in Unlit mode");

    // Serialize in Unlit mode
    std::string json;
    STRESS_ASSERT(ShaderGraphSerializer::SerializeToString(graph, json), "Failed to serialize Unlit graph");

    // Deserialize into fresh graph
    ShaderGraph loaded;
    std::string err;
    STRESS_ASSERT(ShaderGraphSerializer::DeserializeFromString(loaded, json, &err), "Failed to deserialize Unlit graph");
    STRESS_ASSERT(loaded.materialType == ShaderGraphType::Unlit, "Loaded graph must be Unlit");
    MasterNode* loadedMaster = loaded.GetMasterNode();
    STRESS_ASSERT(loadedMaster != nullptr, "Loaded master node must exist");
    STRESS_ASSERT(loadedMaster->materialType == ShaderGraphType::Unlit, "Loaded master must be Unlit");
    STRESS_ASSERT(loadedMaster->FindInput("Color") != nullptr, "Loaded master must have 'Color' input");

    return true;
}

bool Test_SingleInputRule_ReplaceExistingLink() {
    ShaderGraph graph;
    ShaderNode* n1 = graph.AddNode("Math_Add");
    ShaderNode* n2 = graph.AddNode("Math_Subtract");
    ShaderNode* target = graph.AddNode("Math_Multiply");

    // Connect n1.Out -> target.A
    bool ok1 = graph.AddLink(n1->id, "Out", target->id, "A");
    STRESS_ASSERT(ok1, "First link should succeed");
    STRESS_ASSERT(graph.links.size() == 1, "Expected 1 link");
    STRESS_ASSERT(graph.links[0].fromNodeId == n1->id, "Link from n1");

    // Connect n2.Out -> target.A (single input rule should replace n1 with n2)
    bool ok2 = graph.AddLink(n2->id, "Out", target->id, "A");
    STRESS_ASSERT(ok2, "Second link should replace previous link");
    STRESS_ASSERT(graph.links.size() == 1, "Link count should still be 1");
    STRESS_ASSERT(graph.links[0].fromNodeId == n2->id, "Link must now be from n2");

    return true;
}

// ============================================================================
// Suite 4: Corrupted & Fuzzed JSON Resilience
// ============================================================================

bool Test_Fuzz_TruncatedJSON_GracefulFailure() {
    ShaderGraph baseGraph;
    baseGraph.AddProperty("speed", PinType::Float, glm::vec4(1.0f));
    baseGraph.AddNode("Math_Add");
    std::string fullJson;
    ShaderGraphSerializer::SerializeToString(baseGraph, fullJson);

    // Test truncation at various byte lengths
    size_t testCutoffs[] = { 0, 1, 3, 5, 10, 20, 50, 80, fullJson.length() / 2, fullJson.length() - 2 };
    for (size_t cutoff : testCutoffs) {
        if (cutoff > fullJson.length()) continue;
        std::string truncated = fullJson.substr(0, cutoff);
        ShaderGraph g;
        std::string err;
        bool res = ShaderGraphSerializer::DeserializeFromString(g, truncated, &err);
        STRESS_ASSERT(!res, "Truncated JSON must fail to deserialize");
        STRESS_ASSERT(!err.empty(), "Error message must be populated on failure");
    }

    return true;
}

bool Test_Fuzz_NonObjectJSONRoots() {
    const char* nonObjects[] = {
        "", "   \n\t ", "[1, 2, 3]", "\"a simple string\"", "12345", "-99.4", "true", "false", "null"
    };

    for (const char* input : nonObjects) {
        ShaderGraph g;
        std::string err;
        bool res = ShaderGraphSerializer::DeserializeFromString(g, input, &err);
        STRESS_ASSERT(!res, "Non-object JSON root must fail to deserialize");
        STRESS_ASSERT(!err.empty(), "Error message must be populated");
    }

    return true;
}

bool Test_Fuzz_MissingSchemaSections() {
    // Missing version, materialType, nodes, links, properties
    const char* minimalJson = "{}";
    ShaderGraph g;
    std::string err;
    bool res = ShaderGraphSerializer::DeserializeFromString(g, minimalJson, &err);
    STRESS_ASSERT(res, "Empty object JSON should deserialize with default values");
    STRESS_ASSERT(g.materialType == ShaderGraphType::Lit, "Default materialType should be Lit");
    STRESS_ASSERT(g.nodes.empty(), "Nodes should be empty");
    STRESS_ASSERT(g.links.empty(), "Links should be empty");
    STRESS_ASSERT(g.properties.empty(), "Properties should be empty");

    return true;
}

bool Test_Fuzz_UnknownNodeTypesFallback() {
    const char* jsonWithUnknownNode =
        "{\n"
        "  \"version\": 1,\n"
        "  \"materialType\": \"Lit\",\n"
        "  \"nodes\": [\n"
        "    {\n"
        "      \"id\": \"quantum_laser_1\",\n"
        "      \"type\": \"SuperQuantumLaserNode_v99\",\n"
        "      \"title\": \"Quantum Laser\",\n"
        "      \"position\": [100.0, 200.0],\n"
        "      \"inputs\": [{\"id\": \"in1\", \"name\": \"BeamIn\", \"type\": \"float\"}],\n"
        "      \"outputs\": [{\"id\": \"out1\", \"name\": \"BeamOut\", \"type\": \"float\"}]\n"
        "    }\n"
        "  ],\n"
        "  \"links\": []\n"
        "}";

    ShaderGraph g;
    std::string err;
    bool res = ShaderGraphSerializer::DeserializeFromString(g, jsonWithUnknownNode, &err);
    STRESS_ASSERT(res, "Deserializer should gracefully handle unknown node types via UnknownShaderNode fallback");
    STRESS_ASSERT(g.nodes.size() == 1, "Graph should contain the unknown node");
    ShaderNode* unknownNode = g.nodes[0].get();
    STRESS_ASSERT(unknownNode != nullptr, "Node pointer must be valid");
    STRESS_ASSERT(unknownNode->id == "quantum_laser_1", "Node ID must be preserved");
    STRESS_ASSERT(unknownNode->typeName == "SuperQuantumLaserNode_v99", "Unknown type name must be preserved");
    STRESS_ASSERT(unknownNode->inputs.size() == 1, "Input ports should be restored");
    STRESS_ASSERT(unknownNode->outputs.size() == 1, "Output ports should be restored");

    // Test that re-serialization succeeds with unknown node
    std::string reJson;
    bool reOk = ShaderGraphSerializer::SerializeToString(g, reJson);
    STRESS_ASSERT(reOk, "Re-serialization with UnknownShaderNode must succeed");

    return true;
}

bool Test_Fuzz_DanglingLinksPruned() {
    const char* jsonWithDanglingLinks =
        "{\n"
        "  \"version\": 1,\n"
        "  \"nodes\": [\n"
        "    {\"id\": \"real_node_1\", \"type\": \"Math_Add\"}\n"
        "  ],\n"
        "  \"links\": [\n"
        "    {\"id\": \"l1\", \"fromNode\": \"ghost_1\", \"fromPort\": \"Out\", \"toNode\": \"ghost_2\", \"toPort\": \"A\"},\n"
        "    {\"id\": \"l2\", \"fromNode\": \"real_node_1\", \"fromPort\": \"Out\", \"toNode\": \"ghost_2\", \"toPort\": \"A\"},\n"
        "    {\"id\": \"l3\", \"fromNode\": \"ghost_1\", \"fromPort\": \"Out\", \"toNode\": \"real_node_1\", \"toPort\": \"A\"}\n"
        "  ]\n"
        "}";

    ShaderGraph g;
    std::string err;
    bool res = ShaderGraphSerializer::DeserializeFromString(g, jsonWithDanglingLinks, &err);
    STRESS_ASSERT(res, "Deserializer should succeed");
    STRESS_ASSERT(g.nodes.size() == 1, "Expected 1 real node");
    STRESS_ASSERT(g.links.empty(), "All dangling links referencing ghost nodes must be safely discarded");

    return true;
}

bool Test_Fuzz_CorruptedArrayElements() {
    const char* jsonCorruptArrays =
        "{\n"
        "  \"version\": 1,\n"
        "  \"properties\": [ 123, \"not an object\", null, false, {\"id\": \"p1\", \"name\": \"validProp\", \"type\": \"float\"} ],\n"
        "  \"nodes\": [ 456, true, [1, 2], {\"id\": \"n1\", \"type\": \"Math_Add\"} ],\n"
        "  \"links\": [ null, \"link string\", {\"id\": \"l1\", \"fromNode\": \"n1\", \"fromPort\": \"Out\", \"toNode\": \"n1\", \"toPort\": \"A\"} ]\n"
        "}";

    ShaderGraph g;
    std::string err;
    bool res = ShaderGraphSerializer::DeserializeFromString(g, jsonCorruptArrays, &err);
    STRESS_ASSERT(res, "Deserializer should skip non-object array elements without crashing");
    STRESS_ASSERT(g.properties.size() == 1, "Only the 1 valid property should be parsed");
    STRESS_ASSERT(g.nodes.size() == 1, "Only the 1 valid node should be parsed");
    STRESS_ASSERT(g.links.size() == 1, "Only the 1 valid link object should be parsed");

    return true;
}

bool Test_Fuzz_InjectedCyclesInJSON() {
    const char* jsonCycle =
        "{\n"
        "  \"version\": 1,\n"
        "  \"nodes\": [\n"
        "    {\"id\": \"nodeA\", \"type\": \"Math_Add\"},\n"
        "    {\"id\": \"nodeB\", \"type\": \"Math_Add\"}\n"
        "  ],\n"
        "  \"links\": [\n"
        "    {\"id\": \"l1\", \"fromNode\": \"nodeA\", \"fromPort\": \"Out\", \"toNode\": \"nodeB\", \"toPort\": \"A\"},\n"
        "    {\"id\": \"l2\", \"fromNode\": \"nodeB\", \"fromPort\": \"Out\", \"toNode\": \"nodeA\", \"toPort\": \"A\"}\n"
        "  ]\n"
        "}";

    ShaderGraph g;
    std::string err;
    bool res = ShaderGraphSerializer::DeserializeFromString(g, jsonCycle, &err);
    STRESS_ASSERT(res, "Deserializer should load cyclic JSON without hanging or crashing");
    STRESS_ASSERT(g.nodes.size() == 2, "Expected 2 nodes");
    STRESS_ASSERT(g.links.size() == 2, "Expected 2 links");
    STRESS_ASSERT(g.HasCycles(), "HasCycles() must accurately detect the cycle loaded from JSON");

    return true;
}

bool Test_Fuzz_NaN_And_Infinity_JSON_Input() {
    // Standard JSON does not permit raw unquoted NaN or Infinity. RapidJSON returns parse error.
    const char* jsonRawNaN =
        "{\n"
        "  \"version\": 1,\n"
        "  \"properties\": [{\"id\": \"p1\", \"name\": \"nanProp\", \"defaultValue\": [NaN, 0.0, 0.0, 0.0]}]\n"
        "}";

    ShaderGraph g1;
    std::string err1;
    bool res1 = ShaderGraphSerializer::DeserializeFromString(g1, jsonRawNaN, &err1);
    STRESS_ASSERT(!res1, "Raw unquoted NaN in JSON must be rejected as invalid JSON syntax");
    STRESS_ASSERT(!err1.empty(), "Error message should report parse failure");

    // Quoted "NaN" string in numeric slot should be safely skipped (not crash)
    const char* jsonQuotedNaN =
        "{\n"
        "  \"version\": 1,\n"
        "  \"properties\": [{\"id\": \"p2\", \"name\": \"strNanProp\", \"defaultValue\": [\"NaN\", \"Infinity\", 1.0, 2.0]}]\n"
        "}";

    ShaderGraph g2;
    std::string err2;
    bool res2 = ShaderGraphSerializer::DeserializeFromString(g2, jsonQuotedNaN, &err2);
    STRESS_ASSERT(res2, "Quoted strings in numeric default value array should be safely ignored");
    STRESS_ASSERT(g2.properties.size() == 1, "Property should be parsed");
    // Non-number slots should retain their default values (0.0) while numbers are parsed
    STRESS_ASSERT(g2.properties[0].defaultValue.x == 0.0f, "Non-number slot 0 should retain default 0.0");
    STRESS_ASSERT(g2.properties[0].defaultValue.y == 0.0f, "Non-number slot 1 should retain default 0.0");
    STRESS_ASSERT(g2.properties[0].defaultValue.z == 1.0f, "Numeric slot 2 should be parsed as 1.0");
    STRESS_ASSERT(g2.properties[0].defaultValue.w == 2.0f, "Numeric slot 3 should be parsed as 2.0");

    return true;
}

bool Test_Fuzz_Negative_Coordinates_And_Extreme_Positions() {
    const char* jsonNegativePos =
        "{\n"
        "  \"version\": 1,\n"
        "  \"nodes\": [\n"
        "    {\"id\": \"nodeNeg\", \"type\": \"Math_Add\", \"position\": [-999999.5, -888888.25]}\n"
        "  ]\n"
        "}";

    ShaderGraph g;
    std::string err;
    bool res = ShaderGraphSerializer::DeserializeFromString(g, jsonNegativePos, &err);
    STRESS_ASSERT(res, "Negative node positions should be parsed cleanly");
    STRESS_ASSERT(g.nodes.size() == 1, "Expected 1 node");
    STRESS_ASSERT(g.nodes[0]->posX == -999999.5f, "Negative posX preserved");
    STRESS_ASSERT(g.nodes[0]->posY == -888888.25f, "Negative posY preserved");

    // Re-serialize and verify preservation
    std::string outJson;
    STRESS_ASSERT(ShaderGraphSerializer::SerializeToString(g, outJson), "Serialization of negative coords failed");
    ShaderGraph g2;
    STRESS_ASSERT(ShaderGraphSerializer::DeserializeFromString(g2, outJson), "Deserialization of negative coords failed");
    STRESS_ASSERT(g2.nodes[0]->posX == -999999.5f, "Re-serialized negative posX preserved");
    STRESS_ASSERT(g2.nodes[0]->posY == -888888.25f, "Re-serialized negative posY preserved");

    return true;
}

bool Test_Fuzz_Huge_Strings_And_Large_Payloads() {
    ShaderGraph g;
    std::string hugeTitle(5000, 'X');
    std::string hugePropName = "prop_" + std::string(2000, 'y');
    std::string hugeCustomVal(10000, 'Z');

    ShaderNode* n = g.AddNode("Math_Add");
    n->title = hugeTitle;
    n->customProperties["huge"] = hugeCustomVal;

    g.AddProperty(hugePropName, PinType::Float, glm::vec4(1.0f));

    std::string json;
    STRESS_ASSERT(ShaderGraphSerializer::SerializeToString(g, json), "Serialization of huge strings failed");
    STRESS_ASSERT(json.length() >= 17000, "Serialized JSON should reflect payload size");

    ShaderGraph loaded;
    std::string err;
    STRESS_ASSERT(ShaderGraphSerializer::DeserializeFromString(loaded, json, &err), "Deserialization of huge strings failed");
    STRESS_ASSERT(loaded.nodes.size() == 2, "Expected 2 nodes (master + huge)");
    ShaderNode* loadedN = loaded.FindNode(n->id);
    STRESS_ASSERT(loadedN != nullptr, "Huge node missing");
    STRESS_ASSERT(loadedN->title.length() == 5000, "Huge title length preserved");
    STRESS_ASSERT(loadedN->customProperties["huge"].length() == 10000, "Huge custom property length preserved");

    return true;
}

// ============================================================================
// Main Runner
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "================================================================================" << std::endl;
    std::cout << "       SlipEngine Shader Graph EMPIRICAL CHALLENGER STRESS SUITE                " << std::endl;
    std::cout << "================================================================================" << std::endl;

    std::cout << "\n>>> Suite 1: Deep & Large Graphs (100+ to 500+ Nodes) <<<" << std::endl;
    RUN_STRESS_TEST("Linear Chain 150 Nodes & Links", Test_LargeGraph_LinearChain_150Nodes);
    RUN_STRESS_TEST("Cycle Detection & Rollback (150 Nodes)", Test_LargeGraph_CycleDetection_Rollback);
    RUN_STRESS_TEST("Self-Loop Rejection", Test_LargeGraph_SelfLoop_Rejection);
    RUN_STRESS_TEST("Wide Binary Tree (127 Nodes)", Test_LargeGraph_WideBinaryTree_127Nodes);
    RUN_STRESS_TEST("Bulk Hub Removal & Cascading Links (50 satellites)", Test_LargeGraph_BulkHubRemoval_CascadingLinks);
    RUN_STRESS_TEST("Massive Graph (500 Nodes forward DAG)", Test_LargeGraph_MassiveGraph_500Nodes);

    std::cout << "\n>>> Suite 2: Serialization Round-Trip Idempotency <<<" << std::endl;
    RUN_STRESS_TEST("Comprehensive Graph Round-Trip (Byte-For-Byte Idempotent)", Test_Serialization_ComprehensiveRoundTrip_ByteForByte);
    RUN_STRESS_TEST("Special Characters & Unicode in Properties and Titles", Test_Serialization_SpecialCharactersAndUnicode);
    RUN_STRESS_TEST("Extreme Floating-Point Values Preservation", Test_Serialization_ExtremeFloatingPointValues);
    RUN_STRESS_TEST("Disk File Save and Reload Round-Trip", Test_Serialization_DiskFile_RoundTrip);

    std::cout << "\n>>> Suite 3: Dynamic MasterNode Mode Switching Stress <<<" << std::endl;
    RUN_STRESS_TEST("Rapid Mode Switching Lit <-> Unlit (50 iterations)", Test_MasterNode_RapidModeSwitching_50Iterations);
    RUN_STRESS_TEST("Rapid Mode Switching (1000 iterations stress)", Test_MasterNode_RapidModeSwitching_1000Iterations_NoLeak);
    RUN_STRESS_TEST("Link Behavior Across Mode Switching (Lit vs Unlit)", Test_MasterNode_LinkBehaviorAcrossModeSwitching);
    RUN_STRESS_TEST("Single Input Rule (Wire Replacement)", Test_SingleInputRule_ReplaceExistingLink);

    std::cout << "\n>>> Suite 4: Corrupted & Fuzzed JSON Resilience <<<" << std::endl;
    RUN_STRESS_TEST("Truncated JSON Input Graceful Failure", Test_Fuzz_TruncatedJSON_GracefulFailure);
    RUN_STRESS_TEST("Non-Object JSON Root Rejection", Test_Fuzz_NonObjectJSONRoots);
    RUN_STRESS_TEST("Missing Schema Sections Fallback", Test_Fuzz_MissingSchemaSections);
    RUN_STRESS_TEST("Unknown Node Types Fallback to UnknownShaderNode", Test_Fuzz_UnknownNodeTypesFallback);
    RUN_STRESS_TEST("Dangling Links Pruning", Test_Fuzz_DanglingLinksPruned);
    RUN_STRESS_TEST("Corrupted Array Elements (Skipping Non-Objects)", Test_Fuzz_CorruptedArrayElements);
    RUN_STRESS_TEST("Injected Cycles in JSON Detected Accurately", Test_Fuzz_InjectedCyclesInJSON);
    RUN_STRESS_TEST("NaN and Infinity Handling in JSON", Test_Fuzz_NaN_And_Infinity_JSON_Input);
    RUN_STRESS_TEST("Negative Coordinates and Extreme Offsets", Test_Fuzz_Negative_Coordinates_And_Extreme_Positions);
    RUN_STRESS_TEST("Huge Strings and Payloads Resilience", Test_Fuzz_Huge_Strings_And_Large_Payloads);

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "                         STRESS SUITE SUMMARY                                   " << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "  Passed:  " << g_Stats.passed << " / " << g_Stats.total << std::endl;
    std::cout << "  Failed:  " << g_Stats.failed << " / " << g_Stats.total << std::endl;
    std::cout << "================================================================================" << std::endl;

    if (g_Stats.failed == 0) {
        std::cout << "\n[SUCCESS] ALL EMPIRICAL CHALLENGER STRESS TESTS PASSED CLEANLY!\n" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[FAILURE] " << g_Stats.failed << " stress test(s) failed!\n" << std::endl;
        return 1;
    }
}
