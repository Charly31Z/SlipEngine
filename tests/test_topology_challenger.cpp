#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cassert>
#include <sstream>

#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"

using namespace SlipShaderGraph;

static int g_TestsTotal = 0;
static int g_TestsPassed = 0;
static int g_TestsFailed = 0;

#define CHK_ASSERT(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "  [FAIL] " << msg << " (" #expr ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        g_TestsTotal++; \
        std::cout << "[RUN] " << #fn << " ... " << std::flush; \
        if (fn()) { \
            g_TestsPassed++; \
            std::cout << "[PASS]" << std::endl; \
        } else { \
            g_TestsFailed++; \
            std::cout << "[FAILED]" << std::endl; \
        } \
    } while (0)

// ============================================================================
// PART 1: DAG CYCLE DETECTION TESTS
// ============================================================================

// 1.1 Linear chain: A -> B -> C -> D -> Master (must be DAG, HasCycles == false)
bool Test_LinearChain_NoCycle() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("FloatConstantNode");
    ShaderNode* nB = graph.AddNode("Math_Add");
    ShaderNode* nC = graph.AddNode("Math_Multiply");
    ShaderNode* nD = graph.AddNode("Procedural_Clamp");
    MasterNode* master = graph.GetMasterNode();

    CHK_ASSERT(nA && nB && nC && nD && master, "All nodes must exist");

    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "A"), "A -> B should succeed");
    CHK_ASSERT(graph.AddLink(nB->id, "Out", nC->id, "A"), "B -> C should succeed");
    CHK_ASSERT(graph.AddLink(nC->id, "Out", nD->id, "In"), "C -> D should succeed");
    CHK_ASSERT(graph.AddLink(nD->id, "Out", master->id, "Roughness"), "D -> Master should succeed");

    CHK_ASSERT(!graph.HasCycles(), "Linear chain must not have cycles");
    CHK_ASSERT(graph.links.size() == 4, "Must have exactly 4 links");
    return true;
}

// 1.2 Diamond topology: A -> B, A -> C, B -> D, C -> D (multi-path DAG, HasCycles == false)
bool Test_DiamondTopology_ValidDAG() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("FloatConstantNode");
    ShaderNode* nB = graph.AddNode("Math_Sin");
    ShaderNode* nC = graph.AddNode("Math_Cos");
    ShaderNode* nD = graph.AddNode("Math_Add");

    CHK_ASSERT(nA && nB && nC && nD, "Nodes created");

    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "In"), "A -> B");
    CHK_ASSERT(graph.AddLink(nA->id, "Out", nC->id, "In"), "A -> C");
    CHK_ASSERT(graph.AddLink(nB->id, "Out", nD->id, "A"), "B -> D.A");
    CHK_ASSERT(graph.AddLink(nC->id, "Out", nD->id, "B"), "C -> D.B");

    CHK_ASSERT(!graph.HasCycles(), "Diamond DAG must not have cycles");
    CHK_ASSERT(graph.links.size() == 4, "Must have 4 links");
    return true;
}

// 1.3 Direct cycle: A -> B, then try B -> A (must be rejected)
bool Test_DirectCycle_DetectedAndRejected() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("Math_Add");
    ShaderNode* nB = graph.AddNode("Math_Add");

    CHK_ASSERT(nA && nB, "Nodes created");
    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "A"), "A -> B succeeds");
    CHK_ASSERT(!graph.HasCycles(), "Single link has no cycle");

    // Attempt direct cycle B -> A
    bool cycleLinkAdded = graph.AddLink(nB->id, "Out", nA->id, "A");
    CHK_ASSERT(!cycleLinkAdded, "AddLink must reject direct cycle B -> A");
    CHK_ASSERT(!graph.HasCycles(), "Graph must remain acyclic after rejected link");
    CHK_ASSERT(graph.links.size() == 1, "Graph must retain only original link");
    return true;
}

// 1.4 Multi-node loop: A -> B -> C -> A (3-node loop)
bool Test_MultiNodeLoop_3Nodes() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("Math_Add");
    ShaderNode* nB = graph.AddNode("Math_Multiply");
    ShaderNode* nC = graph.AddNode("Math_Subtract");

    CHK_ASSERT(nA && nB && nC, "Nodes created");
    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "A"), "A -> B");
    CHK_ASSERT(graph.AddLink(nB->id, "Out", nC->id, "A"), "B -> C");
    CHK_ASSERT(!graph.HasCycles(), "A -> B -> C has no cycle yet");

    // Attempt closing loop C -> A
    bool loopAdded = graph.AddLink(nC->id, "Out", nA->id, "A");
    CHK_ASSERT(!loopAdded, "AddLink must reject cycle C -> A");
    CHK_ASSERT(!graph.HasCycles(), "Graph must remain acyclic");
    CHK_ASSERT(graph.links.size() == 2, "Graph must keep only valid links");
    return true;
}

// 1.5 Multi-node loop: A -> B -> C -> D -> E -> A (5-node loop)
bool Test_MultiNodeLoop_5Nodes() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("Math_Add");
    ShaderNode* nB = graph.AddNode("Math_Multiply");
    ShaderNode* nC = graph.AddNode("Math_Subtract");
    ShaderNode* nD = graph.AddNode("Math_Divide");
    ShaderNode* nE = graph.AddNode("Math_Add");

    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "A"), "A -> B");
    CHK_ASSERT(graph.AddLink(nB->id, "Out", nC->id, "A"), "B -> C");
    CHK_ASSERT(graph.AddLink(nC->id, "Out", nD->id, "A"), "C -> D");
    CHK_ASSERT(graph.AddLink(nD->id, "Out", nE->id, "A"), "D -> E");
    CHK_ASSERT(!graph.HasCycles(), "5-chain has no cycles");

    // Attempt closing loop E -> A
    bool loopAdded = graph.AddLink(nE->id, "Out", nA->id, "A");
    CHK_ASSERT(!loopAdded, "AddLink must reject 5-node loop E -> A");
    CHK_ASSERT(!graph.HasCycles(), "Graph must remain acyclic");
    CHK_ASSERT(graph.links.size() == 4, "Must have 4 links");
    return true;
}

// 1.6 Self-loop: Node connecting output to its own input (A -> A)
bool Test_SelfLoop_DirectAddLink() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("Math_Add");
    CHK_ASSERT(nA, "Node created");

    // Attempt self-loop via AddLink
    bool selfLoopAdded = graph.AddLink(nA->id, "Out", nA->id, "A");
    CHK_ASSERT(!selfLoopAdded, "AddLink must reject self-loop");
    CHK_ASSERT(!graph.HasCycles(), "Graph has no cycles");
    CHK_ASSERT(graph.links.empty(), "No links added");
    return true;
}

// 1.7 Self-loop in HasCycles() when manually present in links
bool Test_SelfLoop_ManualHasCycles() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("Math_Add");
    CHK_ASSERT(nA, "Node created");

    // Force self-loop link directly into links vector
    graph.links.emplace_back("manual_self_link", nA->id, "Out", nA->id, "A");
    CHK_ASSERT(graph.HasCycles(), "HasCycles must detect self-loop when present in links");
    return true;
}

// 1.8 Disconnected component cycle detection
bool Test_DisconnectedComponentCycle() {
    ShaderGraph graph;
    // Connected component to master
    ShaderNode* n1 = graph.AddNode("FloatConstantNode");
    MasterNode* master = graph.GetMasterNode();
    CHK_ASSERT(graph.AddLink(n1->id, "Out", master->id, "Roughness"), "Link to master");

    // Disconnected island with 2-node cycle: X -> Y -> X
    ShaderNode* nX = graph.AddNode("Math_Add");
    ShaderNode* nY = graph.AddNode("Math_Multiply");
    graph.links.emplace_back("link_xy", nX->id, "Out", nY->id, "A");
    graph.links.emplace_back("link_yx", nY->id, "Out", nX->id, "A");

    CHK_ASSERT(graph.HasCycles(), "HasCycles must detect cycle in disconnected island");
    return true;
}

// 1.9 Complex DAG with shared sub-graphs
bool Test_ComplexDAG_SharedSubgraphs() {
    ShaderGraph graph;
    // Layer 0
    ShaderNode* root = graph.AddNode("FloatConstantNode");
    // Layer 1
    ShaderNode* b1 = graph.AddNode("Math_Sin");
    ShaderNode* b2 = graph.AddNode("Math_Cos");
    // Layer 2
    ShaderNode* c1 = graph.AddNode("Math_Add");
    // Layer 3
    ShaderNode* d1 = graph.AddNode("Math_Multiply");

    CHK_ASSERT(graph.AddLink(root->id, "Out", b1->id, "In"), "root -> b1");
    CHK_ASSERT(graph.AddLink(root->id, "Out", b2->id, "In"), "root -> b2");
    CHK_ASSERT(graph.AddLink(b1->id, "Out", c1->id, "A"), "b1 -> c1");
    CHK_ASSERT(graph.AddLink(b2->id, "Out", c1->id, "B"), "b2 -> c1");
    CHK_ASSERT(graph.AddLink(c1->id, "Out", d1->id, "A"), "c1 -> d1");
    CHK_ASSERT(graph.AddLink(root->id, "Out", d1->id, "B"), "root -> d1"); // skip connection

    CHK_ASSERT(!graph.HasCycles(), "Complex DAG with skip-connection must be acyclic");
    CHK_ASSERT(graph.links.size() == 6, "Must have 6 links");

    // Attempt back edge from d1 to root
    bool backEdge = graph.AddLink(d1->id, "Out", root->id, "Out"); // root has no input, will fail
    // Try back edge from d1 to b1.In
    bool backEdge2 = graph.AddLink(d1->id, "Out", b1->id, "In");
    CHK_ASSERT(!backEdge2, "Back edge d1 -> b1 must be rejected due to cycle");
    CHK_ASSERT(!graph.HasCycles(), "Graph remains acyclic");
    return true;
}

// 1.10 Cycle rejection preserves graph state (Rollback check)
bool Test_CycleRejection_RollbackBehavior() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("FloatConstantNode");
    ShaderNode* nB = graph.AddNode("Math_Add");
    ShaderNode* nC = graph.AddNode("Math_Multiply");

    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "A"), "A -> B");
    CHK_ASSERT(graph.AddLink(nB->id, "Out", nC->id, "A"), "B -> C");
    size_t linkCountBefore = graph.links.size();

    // Now try to link C -> A. But A has no input ports, so try C -> B.A
    // C depends on B, so C -> B is a cycle.
    // Note: B.A already has link from A!
    bool cycleAdded = graph.AddLink(nC->id, "Out", nB->id, "A");
    CHK_ASSERT(!cycleAdded, "AddLink must reject cycle C -> B");

    // Observe what happened to graph.links!
    // Did the original link A -> B survive, or was it wiped out by RemoveLinksConnectedToPort?
    std::cout << "\n    [DIAGNOSTIC] Links remaining after rejected cycle: " << graph.links.size() 
              << " (before was " << linkCountBefore << ")" << std::endl;
    for (const auto& l : graph.links) {
        std::cout << "      Link: " << l.fromNodeId << " -> " << l.toNodeId << ":" << l.toPortId << std::endl;
    }

    // Graph must not have cycles regardless
    CHK_ASSERT(!graph.HasCycles(), "Graph must not have cycles");
    return true;
}

// ============================================================================
// PART 2: PIN COMPATIBILITY, PROMOTIONS & SWIZZLES
// ============================================================================

// 2.1 Exact Type Compatibility
bool Test_PinCompatibility_ExactTypes() {
    CHK_ASSERT(ArePinsCompatible(PinType::Float, PinType::Float), "Float == Float");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec2, PinType::Vec2), "Vec2 == Vec2");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec3, PinType::Vec3), "Vec3 == Vec3");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec4, PinType::Vec4), "Vec4 == Vec4");
    CHK_ASSERT(ArePinsCompatible(PinType::Texture2D, PinType::Texture2D), "Texture2D == Texture2D");
    CHK_ASSERT(ArePinsCompatible(PinType::Matrix4, PinType::Matrix4), "Matrix4 == Matrix4");
    CHK_ASSERT(ArePinsCompatible(PinType::Boolean, PinType::Boolean), "Boolean == Boolean");
    return true;
}

// 2.2 Promotions & Swizzles Between Numeric Types (Float, Vec2, Vec3, Vec4)
bool Test_PinCompatibility_NumericPromotionsAndSwizzles() {
    // Float promotions to vectors
    CHK_ASSERT(ArePinsCompatible(PinType::Float, PinType::Vec2), "Float -> Vec2");
    CHK_ASSERT(ArePinsCompatible(PinType::Float, PinType::Vec3), "Float -> Vec3");
    CHK_ASSERT(ArePinsCompatible(PinType::Float, PinType::Vec4), "Float -> Vec4");

    // Vector to float truncations
    CHK_ASSERT(ArePinsCompatible(PinType::Vec2, PinType::Float), "Vec2 -> Float");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec3, PinType::Float), "Vec3 -> Float");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec4, PinType::Float), "Vec4 -> Float");

    // Vector cross-resizing
    CHK_ASSERT(ArePinsCompatible(PinType::Vec2, PinType::Vec3), "Vec2 -> Vec3");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec2, PinType::Vec4), "Vec2 -> Vec4");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec3, PinType::Vec2), "Vec3 -> Vec2");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec3, PinType::Vec4), "Vec3 -> Vec4");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec4, PinType::Vec2), "Vec4 -> Vec2");
    CHK_ASSERT(ArePinsCompatible(PinType::Vec4, PinType::Vec3), "Vec4 -> Vec3");
    return true;
}

// 2.3 Disallowed Type Combinations
bool Test_PinCompatibility_DisallowedTypes() {
    std::vector<PinType> numericTypes = { PinType::Float, PinType::Vec2, PinType::Vec3, PinType::Vec4 };

    for (PinType numType : numericTypes) {
        CHK_ASSERT(!ArePinsCompatible(PinType::Texture2D, numType), "Texture2D cannot connect to numeric");
        CHK_ASSERT(!ArePinsCompatible(numType, PinType::Texture2D), "Numeric cannot connect to Texture2D");
        CHK_ASSERT(!ArePinsCompatible(PinType::Matrix4, numType), "Matrix4 cannot connect to numeric");
        CHK_ASSERT(!ArePinsCompatible(numType, PinType::Matrix4), "Numeric cannot connect to Matrix4");
        CHK_ASSERT(!ArePinsCompatible(PinType::Boolean, numType), "Boolean cannot connect to numeric");
        CHK_ASSERT(!ArePinsCompatible(numType, PinType::Boolean), "Numeric cannot connect to Boolean");
    }

    CHK_ASSERT(!ArePinsCompatible(PinType::Texture2D, PinType::Matrix4), "Texture2D != Matrix4");
    CHK_ASSERT(!ArePinsCompatible(PinType::Matrix4, PinType::Texture2D), "Matrix4 != Texture2D");
    CHK_ASSERT(!ArePinsCompatible(PinType::Boolean, PinType::Texture2D), "Boolean != Texture2D");
    CHK_ASSERT(!ArePinsCompatible(PinType::Boolean, PinType::Matrix4), "Boolean != Matrix4");
    return true;
}

// 2.4 GLSL Expression Conversion for all 12 dimension conversions
bool Test_ConvertGLSLExpression_AllDimensions() {
    std::string val = "x";

    // Same dimension
    CHK_ASSERT(ConvertGLSLExpression(PinType::Float, PinType::Float, val) == "x", "1 -> 1");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec3, PinType::Vec3, val) == "x", "3 -> 3");

    // 1 -> N promotions
    CHK_ASSERT(ConvertGLSLExpression(PinType::Float, PinType::Vec2, val) == "vec2(x)", "1 -> 2");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Float, PinType::Vec3, val) == "vec3(x)", "1 -> 3");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Float, PinType::Vec4, val) == "vec4(x)", "1 -> 4");

    // 2 -> N
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec2, PinType::Float, val) == "(x).x", "2 -> 1");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec2, PinType::Vec3, val) == "vec3(x, 0.0)", "2 -> 3");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec2, PinType::Vec4, val) == "vec4(x, 0.0, 1.0)", "2 -> 4");

    // 3 -> N
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec3, PinType::Float, val) == "(x).x", "3 -> 1");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec3, PinType::Vec2, val) == "(x).xy", "3 -> 2");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec3, PinType::Vec4, val) == "vec4(x, 1.0)", "3 -> 4");

    // 4 -> N
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec4, PinType::Float, val) == "(x).x", "4 -> 1");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec4, PinType::Vec2, val) == "(x).xy", "4 -> 2");
    CHK_ASSERT(ConvertGLSLExpression(PinType::Vec4, PinType::Vec3, val) == "(x).xyz", "4 -> 3");
    return true;
}

// 2.5 GLSL Value Formatting
bool Test_FormatGLSLValue() {
    glm::vec4 v(1.0f, 2.5f, 3.25f, 4.125f);
    CHK_ASSERT(FormatGLSLValue(PinType::Float, v) == "1.0000", "Format Float");
    CHK_ASSERT(FormatGLSLValue(PinType::Vec2, v) == "vec2(1.0000, 2.5000)", "Format Vec2");
    CHK_ASSERT(FormatGLSLValue(PinType::Vec3, v) == "vec3(1.0000, 2.5000, 3.2500)", "Format Vec3");
    CHK_ASSERT(FormatGLSLValue(PinType::Vec4, v) == "vec4(1.0000, 2.5000, 3.2500, 4.1250)", "Format Vec4");
    return true;
}

// ============================================================================
// PART 3: PORT PRESERVATION DURING LIT/UNLIT TOGGLES
// ============================================================================

// 3.1 Verify MasterNode inputs in Lit mode
bool Test_MasterNode_LitPorts() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();
    CHK_ASSERT(master != nullptr, "MasterNode must exist");
    CHK_ASSERT(master->inputs.size() == 8, "Lit MasterNode must have 8 inputs");

    CHK_ASSERT(master->FindInput("Albedo") != nullptr, "Albedo input");
    CHK_ASSERT(master->FindInput("Normal") != nullptr, "Normal input");
    CHK_ASSERT(master->FindInput("Roughness") != nullptr, "Roughness input");
    CHK_ASSERT(master->FindInput("Metallic") != nullptr, "Metallic input");
    CHK_ASSERT(master->FindInput("Emissive") != nullptr, "Emissive input");
    CHK_ASSERT(master->FindInput("AO") != nullptr, "AO input");
    CHK_ASSERT(master->FindInput("Alpha") != nullptr, "Alpha input");
    CHK_ASSERT(master->FindInput("AlphaClipThreshold") != nullptr, "AlphaClipThreshold input");

    CHK_ASSERT(master->FindInput("Albedo")->type == PinType::Vec3, "Albedo type");
    CHK_ASSERT(master->FindInput("Normal")->type == PinType::Vec3, "Normal type");
    CHK_ASSERT(master->FindInput("Roughness")->type == PinType::Float, "Roughness type");
    CHK_ASSERT(master->FindInput("Metallic")->type == PinType::Float, "Metallic type");
    CHK_ASSERT(master->FindInput("Emissive")->type == PinType::Vec3, "Emissive type");
    CHK_ASSERT(master->FindInput("AO")->type == PinType::Float, "AO type");
    CHK_ASSERT(master->FindInput("Alpha")->type == PinType::Float, "Alpha type");
    CHK_ASSERT(master->FindInput("AlphaClipThreshold")->type == PinType::Float, "AlphaClipThreshold type");
    return true;
}

// 3.2 Verify MasterNode inputs in Unlit mode
bool Test_MasterNode_UnlitPorts() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Unlit);
    MasterNode* master = graph.GetMasterNode();
    CHK_ASSERT(master != nullptr, "MasterNode must exist");
    CHK_ASSERT(master->inputs.size() == 4, "Unlit MasterNode must have 4 inputs");

    CHK_ASSERT(master->FindInput("Color") != nullptr, "Color input");
    CHK_ASSERT(master->FindInput("Emissive") != nullptr, "Emissive input");
    CHK_ASSERT(master->FindInput("Alpha") != nullptr, "Alpha input");
    CHK_ASSERT(master->FindInput("AlphaClipThreshold") != nullptr, "AlphaClipThreshold input");

    CHK_ASSERT(master->FindInput("Albedo") == nullptr, "Albedo must not exist in Unlit");
    CHK_ASSERT(master->FindInput("Normal") == nullptr, "Normal must not exist in Unlit");
    CHK_ASSERT(master->FindInput("Roughness") == nullptr, "Roughness must not exist in Unlit");
    CHK_ASSERT(master->FindInput("Metallic") == nullptr, "Metallic must not exist in Unlit");
    CHK_ASSERT(master->FindInput("AO") == nullptr, "AO must not exist in Unlit");
    return true;
}

// 3.3 Connect 8 links in Lit, toggle to Unlit, inspect port & link state
bool Test_PortPreservation_ToggleLitToUnlit() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();
    CHK_ASSERT(master != nullptr, "MasterNode");

    // Create 8 source nodes
    ShaderNode* sAlbedo = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sNormal = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sRoughness = graph.AddNode("FloatConstantNode");
    ShaderNode* sMetallic = graph.AddNode("FloatConstantNode");
    ShaderNode* sEmissive = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sAO = graph.AddNode("FloatConstantNode");
    ShaderNode* sAlpha = graph.AddNode("FloatConstantNode");
    ShaderNode* sClip = graph.AddNode("FloatConstantNode");

    CHK_ASSERT(graph.AddLink(sAlbedo->id, "Out", master->id, "Albedo"), "Link Albedo");
    CHK_ASSERT(graph.AddLink(sNormal->id, "Out", master->id, "Normal"), "Link Normal");
    CHK_ASSERT(graph.AddLink(sRoughness->id, "Out", master->id, "Roughness"), "Link Roughness");
    CHK_ASSERT(graph.AddLink(sMetallic->id, "Out", master->id, "Metallic"), "Link Metallic");
    CHK_ASSERT(graph.AddLink(sEmissive->id, "Out", master->id, "Emissive"), "Link Emissive");
    CHK_ASSERT(graph.AddLink(sAO->id, "Out", master->id, "AO"), "Link AO");
    CHK_ASSERT(graph.AddLink(sAlpha->id, "Out", master->id, "Alpha"), "Link Alpha");
    CHK_ASSERT(graph.AddLink(sClip->id, "Out", master->id, "AlphaClipThreshold"), "Link Clip");

    CHK_ASSERT(graph.links.size() == 8, "8 links in Lit mode");

    // Toggle to Unlit
    graph.SetMaterialType(ShaderGraphType::Unlit);
    CHK_ASSERT(graph.materialType == ShaderGraphType::Unlit, "Graph type is Unlit");
    CHK_ASSERT(master->materialType == ShaderGraphType::Unlit, "Master type is Unlit");
    CHK_ASSERT(master->inputs.size() == 4, "Unlit has 4 ports");

    // Check link retention
    std::cout << "\n    [DIAGNOSTIC] Links remaining after Lit -> Unlit toggle: " << graph.links.size() << std::endl;
    for (const auto& l : graph.links) {
        std::cout << "      Link: " << l.fromNodeId << " -> " << l.toNodeId << ":" << l.toPortId << std::endl;
    }

    // Shared ports: Emissive, Alpha, AlphaClipThreshold should still be linked
    const ShaderLink* lEmissive = graph.FindLinkToInput(master->id, "Emissive");
    const ShaderLink* lAlpha = graph.FindLinkToInput(master->id, "Alpha");
    const ShaderLink* lClip = graph.FindLinkToInput(master->id, "AlphaClipThreshold");

    CHK_ASSERT(lEmissive != nullptr, "Emissive link preserved in Unlit");
    CHK_ASSERT(lAlpha != nullptr, "Alpha link preserved in Unlit");
    CHK_ASSERT(lClip != nullptr, "AlphaClipThreshold link preserved in Unlit");

    return true;
}

// 3.4 Connect 8 links in Lit, toggle to Unlit, toggle back to Lit (Full Round-Trip Port Restoration)
bool Test_PortPreservation_ToggleLitUnlitLitRoundTrip() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderNode* sAlbedo = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sNormal = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sRoughness = graph.AddNode("FloatConstantNode");
    ShaderNode* sMetallic = graph.AddNode("FloatConstantNode");
    ShaderNode* sEmissive = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sAO = graph.AddNode("FloatConstantNode");
    ShaderNode* sAlpha = graph.AddNode("FloatConstantNode");
    ShaderNode* sClip = graph.AddNode("FloatConstantNode");

    graph.AddLink(sAlbedo->id, "Out", master->id, "Albedo");
    graph.AddLink(sNormal->id, "Out", master->id, "Normal");
    graph.AddLink(sRoughness->id, "Out", master->id, "Roughness");
    graph.AddLink(sMetallic->id, "Out", master->id, "Metallic");
    graph.AddLink(sEmissive->id, "Out", master->id, "Emissive");
    graph.AddLink(sAO->id, "Out", master->id, "AO");
    graph.AddLink(sAlpha->id, "Out", master->id, "Alpha");
    graph.AddLink(sClip->id, "Out", master->id, "AlphaClipThreshold");

    CHK_ASSERT(graph.links.size() == 8, "8 initial links in Lit");

    // Toggle Lit -> Unlit
    graph.SetMaterialType(ShaderGraphType::Unlit);

    // Toggle Unlit -> Lit
    graph.SetMaterialType(ShaderGraphType::Lit);
    CHK_ASSERT(master->inputs.size() == 8, "Lit restored with 8 ports");

    // Verify all 8 links are still connected to their target ports
    CHK_ASSERT(graph.FindLinkToInput(master->id, "Albedo") != nullptr, "Albedo link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "Normal") != nullptr, "Normal link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "Roughness") != nullptr, "Roughness link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "Metallic") != nullptr, "Metallic link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "Emissive") != nullptr, "Emissive link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "AO") != nullptr, "AO link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "Alpha") != nullptr, "Alpha link restored");
    CHK_ASSERT(graph.FindLinkToInput(master->id, "AlphaClipThreshold") != nullptr, "AlphaClipThreshold link restored");
    CHK_ASSERT(graph.links.size() == 8, "All 8 links intact after round-trip toggle");

    return true;
}

// 3.5 Serialization Across Lit/Unlit Toggles
bool Test_PortPreservation_SerializationAcrossToggles() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderNode* sAlbedo = graph.AddNode("Vec3ConstantNode");
    ShaderNode* sEmissive = graph.AddNode("Vec3ConstantNode");
    graph.AddLink(sAlbedo->id, "Out", master->id, "Albedo");
    graph.AddLink(sEmissive->id, "Out", master->id, "Emissive");

    // Serialize in Lit
    std::string jsonLit;
    CHK_ASSERT(ShaderGraphSerializer::SerializeToString(graph, jsonLit), "Serialization produced JSON");
    CHK_ASSERT(!jsonLit.empty(), "Serialization produced JSON");

    // Deserialize into graph2
    ShaderGraph graph2;
    CHK_ASSERT(ShaderGraphSerializer::DeserializeFromString(graph2, jsonLit), "Deserialized");
    CHK_ASSERT(graph2.materialType == ShaderGraphType::Lit, "Graph2 is Lit");
    CHK_ASSERT(graph2.links.size() == 2, "Graph2 has 2 links");

    // Switch graph2 to Unlit
    graph2.SetMaterialType(ShaderGraphType::Unlit);
    std::string jsonUnlit;
    CHK_ASSERT(ShaderGraphSerializer::SerializeToString(graph2, jsonUnlit), "Serialization produced JSON");

    // Deserialize into graph3
    ShaderGraph graph3;
    CHK_ASSERT(ShaderGraphSerializer::DeserializeFromString(graph3, jsonUnlit), "Deserialized Unlit");
    CHK_ASSERT(graph3.materialType == ShaderGraphType::Unlit, "Graph3 is Unlit");

    // Switch graph3 back to Lit
    graph3.SetMaterialType(ShaderGraphType::Lit);
    CHK_ASSERT(graph3.FindLinkToInput(graph3.GetMasterNode()->id, "Albedo") != nullptr, "Albedo restored in graph3");
    CHK_ASSERT(graph3.FindLinkToInput(graph3.GetMasterNode()->id, "Emissive") != nullptr, "Emissive restored in graph3");

    return true;
}

// 3.6 Default value preservation across Lit/Unlit toggles
bool Test_MasterNode_DefaultValuePreservation_AcrossToggles() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();
    CHK_ASSERT(master != nullptr, "MasterNode");

    // Customize default values
    ShaderPort* emissivePort = master->FindInput("Emissive");
    CHK_ASSERT(emissivePort != nullptr, "Emissive port");
    glm::vec4 customEmissive(0.8f, 0.4f, 0.1f, 1.0f);
    emissivePort->defaultValue = customEmissive;

    ShaderPort* roughPort = master->FindInput("Roughness");
    CHK_ASSERT(roughPort != nullptr, "Roughness port");
    glm::vec4 customRough(0.75f, 0.0f, 0.0f, 0.0f);
    roughPort->defaultValue = customRough;

    // Toggle Lit -> Unlit
    graph.SetMaterialType(ShaderGraphType::Unlit);
    ShaderPort* unlitEmissive = master->FindInput("Emissive");
    CHK_ASSERT(unlitEmissive != nullptr, "Emissive exists in Unlit");

    std::cout << "\n    [EMPIRICAL OBSERVATION] Emissive defaultValue in Unlit: ("
              << unlitEmissive->defaultValue.x << ", " << unlitEmissive->defaultValue.y << ", "
              << unlitEmissive->defaultValue.z << ", " << unlitEmissive->defaultValue.w << ")" << std::endl;

    bool emissivePreservedInUnlit = (unlitEmissive->defaultValue == customEmissive);

    // Toggle Unlit -> Lit
    graph.SetMaterialType(ShaderGraphType::Lit);
    ShaderPort* litEmissiveAgain = master->FindInput("Emissive");
    ShaderPort* litRoughAgain = master->FindInput("Roughness");

    std::cout << "    [EMPIRICAL OBSERVATION] Emissive defaultValue after return to Lit: ("
              << litEmissiveAgain->defaultValue.x << ", " << litEmissiveAgain->defaultValue.y << ", "
              << litEmissiveAgain->defaultValue.z << ", " << litEmissiveAgain->defaultValue.w << ")" << std::endl;
    std::cout << "    [EMPIRICAL OBSERVATION] Roughness defaultValue after return to Lit: "
              << litRoughAgain->defaultValue.x << std::endl;

    bool litPreserved = (litEmissiveAgain->defaultValue == customEmissive && litRoughAgain->defaultValue == customRough);

    // Document whether port default values are preserved across toggles
    if (!emissivePreservedInUnlit || !litPreserved) {
        std::cout << "    [FINDING] Port default values are reset to factory defaults upon SetMaterialType()!" << std::endl;
    }

    return true;
}

// 1.11 Deep DAG Scale Test (1000 nodes)
bool Test_DeepChain_CycleDetection_Scale1000() {
    ShaderGraph graph;
    const int NODE_COUNT = 1000;
    std::vector<ShaderNode*> chainNodes;
    chainNodes.reserve(NODE_COUNT);

    for (int i = 0; i < NODE_COUNT; ++i) {
        chainNodes.push_back(graph.AddNode("Math_Add"));
    }

    // Connect linear chain
    for (int i = 0; i < NODE_COUNT - 1; ++i) {
        bool ok = graph.AddLink(chainNodes[i]->id, "Out", chainNodes[i + 1]->id, "A");
        if (!ok) return false;
    }

    CHK_ASSERT(!graph.HasCycles(), "1000-node linear chain must be acyclic");
    CHK_ASSERT(graph.links.size() == (size_t)(NODE_COUNT - 1), "All chain links added");

    // Attempt back edge from node 999 to node 0 (creating 1000-node loop)
    bool cycleOk = graph.AddLink(chainNodes[NODE_COUNT - 1]->id, "Out", chainNodes[0]->id, "A");
    CHK_ASSERT(!cycleOk, "AddLink must reject cycle across 1000 nodes");
    CHK_ASSERT(!graph.HasCycles(), "Graph remains acyclic after 1000-node cycle rejected");

    return true;
}

// 1.12 Empirical Bug Check: Pre-existing link destruction on cycle rejection
bool Test_CycleRejection_PreExistingLinkLoss_EmpiricalCheck() {
    ShaderGraph graph;
    ShaderNode* nA = graph.AddNode("FloatConstantNode");
    ShaderNode* nB = graph.AddNode("Math_Add");
    ShaderNode* nC = graph.AddNode("Math_Multiply");

    // Legitimate wire: A -> B.A
    CHK_ASSERT(graph.AddLink(nA->id, "Out", nB->id, "A"), "A -> B");
    // Wire: B -> C.A
    CHK_ASSERT(graph.AddLink(nB->id, "Out", nC->id, "A"), "B -> C");

    const ShaderLink* linkBefore = graph.FindLinkToInput(nB->id, "A");
    CHK_ASSERT(linkBefore != nullptr, "Link A -> B.A exists before test");
    std::string originalLinkId = linkBefore->id;

    // Now try to link C -> B.A (creating cycle C -> B -> C)
    bool added = graph.AddLink(nC->id, "Out", nB->id, "A");
    CHK_ASSERT(!added, "Cycle C -> B must be rejected");

    // Check whether the original wire A -> B.A survived:
    const ShaderLink* linkAfter = graph.FindLinkToInput(nB->id, "A");
    if (linkAfter == nullptr) {
        std::cout << "\n    [CONFIRMED EMPIRICAL BUG] Pre-existing link " << originalLinkId
                  << " (A -> B.A) was DESTROYED by failed cycle attempt C -> B.A!" << std::endl;
    } else {
        std::cout << "\n    [INFO] Pre-existing link was preserved." << std::endl;
    }

    return true;
}

// 2.6 End-to-End Pin Compatibility in AddLink (Texture2D vs Float)
bool Test_TexturePin_EndToEndConnection() {
    ShaderGraph graph;
    ShaderProperty* texProp = graph.AddProperty("MainTex", PinType::Texture2D);
    CHK_ASSERT(texProp != nullptr, "Texture property created");

    auto propNode = std::make_unique<PropertyNode>();
    propNode->SetProperty(*texProp);
    ShaderNode* pNode = graph.AddNode(std::move(propNode));

    ShaderNode* samplerNode = graph.AddNode("Texture_Sampler");
    ShaderNode* addNode = graph.AddNode("Math_Add");

    CHK_ASSERT(pNode && samplerNode && addNode, "Nodes created");

    // Linking Texture2D property to TextureSampler's Sampler input (both Texture2D) -> MUST SUCCEED
    bool linkSampler = graph.AddLink(pNode->id, "Out", samplerNode->id, "Sampler");
    CHK_ASSERT(linkSampler, "Texture2D -> Texture2D link must succeed");

    // Linking Texture2D property to MathAdd's A input (Texture2D -> Float) -> MUST FAIL
    bool linkAdd = graph.AddLink(pNode->id, "Out", addNode->id, "A");
    CHK_ASSERT(!linkAdd, "Texture2D -> Float link must be rejected by AddLink");

    return true;
}

// 3.7 Single Input Rule: Link Replacement
bool Test_SingleInputRule_LinkReplacement() {
    ShaderGraph graph;
    MasterNode* master = graph.GetMasterNode();
    ShaderNode* nA = graph.AddNode("Vec3ConstantNode");
    ShaderNode* nB = graph.AddNode("Vec3ConstantNode");

    // First wire: nA -> master:Albedo
    CHK_ASSERT(graph.AddLink(nA->id, "Out", master->id, "Albedo"), "First link nA -> Albedo");
    const ShaderLink* l1 = graph.FindLinkToInput(master->id, "Albedo");
    CHK_ASSERT(l1 && l1->fromNodeId == nA->id, "Albedo wired to nA");

    // Second wire: nB -> master:Albedo (should replace nA)
    CHK_ASSERT(graph.AddLink(nB->id, "Out", master->id, "Albedo"), "Second link nB -> Albedo");
    const ShaderLink* l2 = graph.FindLinkToInput(master->id, "Albedo");
    CHK_ASSERT(l2 && l2->fromNodeId == nB->id, "Albedo replaced with wire to nB");
    CHK_ASSERT(graph.links.size() == 1, "Only 1 link on Albedo input");

    return true;
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int main() {
    std::cout << "================================================================================" << std::endl;
    std::cout << "      SlipEngine Shader Graph Topology & Port Challenger Test Suite            " << std::endl;
    std::cout << "================================================================================" << std::endl;

    std::cout << "\n>>> Part 1: DAG Cycle Detection Tests <<<" << std::endl;
    RUN_TEST(Test_LinearChain_NoCycle);
    RUN_TEST(Test_DiamondTopology_ValidDAG);
    RUN_TEST(Test_DirectCycle_DetectedAndRejected);
    RUN_TEST(Test_MultiNodeLoop_3Nodes);
    RUN_TEST(Test_MultiNodeLoop_5Nodes);
    RUN_TEST(Test_SelfLoop_DirectAddLink);
    RUN_TEST(Test_SelfLoop_ManualHasCycles);
    RUN_TEST(Test_DisconnectedComponentCycle);
    RUN_TEST(Test_ComplexDAG_SharedSubgraphs);
    RUN_TEST(Test_CycleRejection_RollbackBehavior);
    RUN_TEST(Test_DeepChain_CycleDetection_Scale1000);
    RUN_TEST(Test_CycleRejection_PreExistingLinkLoss_EmpiricalCheck);

    std::cout << "\n>>> Part 2: Pin Compatibility, Promotions & Swizzles Tests <<<" << std::endl;
    RUN_TEST(Test_PinCompatibility_ExactTypes);
    RUN_TEST(Test_PinCompatibility_NumericPromotionsAndSwizzles);
    RUN_TEST(Test_PinCompatibility_DisallowedTypes);
    RUN_TEST(Test_ConvertGLSLExpression_AllDimensions);
    RUN_TEST(Test_FormatGLSLValue);
    RUN_TEST(Test_TexturePin_EndToEndConnection);

    std::cout << "\n>>> Part 3: Port Preservation During Lit/Unlit Toggles Tests <<<" << std::endl;
    RUN_TEST(Test_MasterNode_LitPorts);
    RUN_TEST(Test_MasterNode_UnlitPorts);
    RUN_TEST(Test_PortPreservation_ToggleLitToUnlit);
    RUN_TEST(Test_PortPreservation_ToggleLitUnlitLitRoundTrip);
    RUN_TEST(Test_PortPreservation_SerializationAcrossToggles);
    RUN_TEST(Test_MasterNode_DefaultValuePreservation_AcrossToggles);
    RUN_TEST(Test_SingleInputRule_LinkReplacement);

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "Summary: " << g_TestsPassed << "/" << g_TestsTotal << " tests passed. ("
              << g_TestsFailed << " failed)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    return (g_TestsFailed == 0) ? 0 : 1;
}

