#include <glad/glad.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <algorithm>

using namespace SlipShaderGraph;

// ============================================================================
// Challenger Test Harness Framework
// ============================================================================

struct ChallengerStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

static ChallengerStats g_Stats;

#define CHL_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n    [FAIL] Line " << __LINE__ << ": " << msg << " (" #cond ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define CHL_ASSERT_TRUE(cond) CHL_ASSERT((cond), "Condition must be true")
#define CHL_ASSERT_FALSE(cond) CHL_ASSERT(!(cond), "Condition must be false")
#define CHL_ASSERT_EQ(a, b) CHL_ASSERT(((a) == (b)), "Values must be equal")
#define CHL_ASSERT_FLOAT_EQ_EPS(a, b, eps) CHL_ASSERT((std::abs((a) - (b)) <= (eps)), "Floats should be equal within epsilon")

#define RUN_CHALLENGE(name, func) \
    do { \
        g_Stats.total++; \
        std::cout << "  [CHALLENGE] " << name << " ... " << std::flush; \
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
    } while (0)

// Helper to compile vertex + fragment into a native OpenGL program
static GLuint CompileRawGLSL(const std::string& vertSrc, const std::string& fragSrc, std::string& outErr) {
    GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vCode = vertSrc.c_str();
    glShaderSource(vShader, 1, &vCode, nullptr);
    glCompileShader(vShader);

    GLint vSuccess = 0;
    glGetShaderiv(vShader, GL_COMPILE_STATUS, &vSuccess);
    if (!vSuccess) {
        char log[1024];
        glGetShaderInfoLog(vShader, sizeof(log), nullptr, log);
        outErr = std::string("Vertex Shader Compile Error: ") + log;
        glDeleteShader(vShader);
        return 0;
    }

    GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fCode = fragSrc.c_str();
    glShaderSource(fShader, 1, &fCode, nullptr);
    glCompileShader(fShader);

    GLint fSuccess = 0;
    glGetShaderiv(fShader, GL_COMPILE_STATUS, &fSuccess);
    if (!fSuccess) {
        char log[1024];
        glGetShaderInfoLog(fShader, sizeof(log), nullptr, log);
        outErr = std::string("Fragment Shader Compile Error: ") + log;
        glDeleteShader(vShader);
        glDeleteShader(fShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vShader);
    glAttachShader(program, fShader);
    glLinkProgram(program);

    GLint lSuccess = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &lSuccess);
    if (!lSuccess) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        outErr = std::string("Program Link Error: ") + log;
        glDeleteShader(vShader);
        glDeleteShader(fShader);
        glDeleteProgram(program);
        return 0;
    }

    glDeleteShader(vShader);
    glDeleteShader(fShader);
    return program;
}

// ============================================================================
// PART 1: GLSL Generation & Real OpenGL Compilation / Linking
// ============================================================================

// 1.1: Complex Lit PBR Shader Graph with Math, Trig, and Procedural Noise
bool Test_Part1_01_ComplexLitPBR_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    // Noise Node -> Scale input via Float Constant
    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* noiseNode = graph.AddNode("Procedural_Noise");
    ShaderNode* scaleNode = graph.AddNode("Input_Float");
    dynamic_cast<FloatConstantNode*>(scaleNode)->value = 8.5f;

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", noiseNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(scaleNode->id, "Out", noiseNode->id, "Scale"));

    // Trig: Time -> Sin -> Remap to Roughness
    ShaderNode* timeNode = graph.AddNode("Time_Time");
    ShaderNode* sinNode = graph.AddNode("Math_Sin");
    CHL_ASSERT_TRUE(graph.AddLink(timeNode->id, "SinTime", sinNode->id, "In"));

    // Clamp Noise to Roughness
    ShaderNode* clampNode = graph.AddNode("Procedural_Clamp");
    CHL_ASSERT_TRUE(graph.AddLink(noiseNode->id, "Out", clampNode->id, "In"));
    CHL_ASSERT_TRUE(graph.AddLink(clampNode->id, "Out", master->id, "Roughness"));

    // Math: Multiply Albedo Color
    ShaderNode* colNode = graph.AddNode("Input_Vec3");
    dynamic_cast<Vec3ConstantNode*>(colNode)->value = glm::vec3(0.8f, 0.2f, 0.1f);
    CHL_ASSERT_TRUE(graph.AddLink(colNode->id, "Out", master->id, "Albedo"));

    // Metallic from Sin
    CHL_ASSERT_TRUE(graph.AddLink(sinNode->id, "Out", master->id, "Metallic"));

    // Compile GLSL
    ShaderCompilerOptions options;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);
    CHL_ASSERT_FALSE(code.vertexSource.empty());
    CHL_ASSERT_FALSE(code.fragmentSource.empty());

    // Link in OpenGL
    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    if (prog == 0) {
        std::cerr << "\n    [OPENGL ERROR] " << err << std::endl;
        return false;
    }
    CHL_ASSERT_TRUE(glIsProgram(prog));

    // Verify critical uniform locations exist
    glUseProgram(prog);
    GLint locTime = glGetUniformLocation(prog, "u_Time");
    GLint locCam = glGetUniformLocation(prog, "u_CameraPos");
    CHL_ASSERT(locTime != -1 || locCam != -1, "Engine uniforms must be present in compiled shader");

    glDeleteProgram(prog);
    return true;
}

// 1.2: Dynamic Unlit Shader Graph with Animated Time, DeltaTime and Color Modulation
bool Test_Part1_02_DynamicUnlit_AnimatedTimeColor_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Unlit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    ShaderNode* timeNode = graph.AddNode("Time_Time");
    ShaderNode* dtNode = graph.AddNode("Time_DeltaTime");
    ShaderNode* addNode = graph.AddNode("Math_Add");
    CHL_ASSERT_TRUE(graph.AddLink(timeNode->id, "Time", addNode->id, "A"));
    CHL_ASSERT_TRUE(graph.AddLink(dtNode->id, "DeltaTime", addNode->id, "B"));

    ShaderNode* cosNode = graph.AddNode("Math_Cos");
    CHL_ASSERT_TRUE(graph.AddLink(addNode->id, "Out", cosNode->id, "In"));

    // Combine Vector for dynamic color
    ShaderNode* combNode = graph.AddNode("Vector_Combine");
    CHL_ASSERT_TRUE(graph.AddLink(cosNode->id, "Out", combNode->id, "R"));
    CHL_ASSERT_TRUE(graph.AddLink(cosNode->id, "Out", combNode->id, "B"));

    CHL_ASSERT_TRUE(graph.AddLink(combNode->id, "Out", master->id, "Color"));

    ShaderCompilerOptions options;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    if (prog == 0) {
        std::cerr << "\n    [OPENGL ERROR] " << err << std::endl;
        return false;
    }
    CHL_ASSERT_TRUE(glIsProgram(prog));
    glDeleteProgram(prog);
    return true;
}

// 1.3: Deep DAG Chain Stress (100+ chained arithmetic and procedural nodes)
bool Test_Part1_03_DeepDAGChain100Nodes_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    ShaderNode* current = graph.AddNode("Input_Float");
    dynamic_cast<FloatConstantNode*>(current)->value = 1.0f;

    const int CHAIN_DEPTH = 100;
    for (int i = 0; i < CHAIN_DEPTH; ++i) {
        ShaderNode* nextNode = nullptr;
        if (i % 3 == 0) {
            nextNode = graph.AddNode("Math_Add");
            CHL_ASSERT_TRUE(graph.AddLink(current->id, "Out", nextNode->id, "A"));
        } else if (i % 3 == 1) {
            nextNode = graph.AddNode("Procedural_Clamp");
            CHL_ASSERT_TRUE(graph.AddLink(current->id, "Out", nextNode->id, "In"));
        } else {
            nextNode = graph.AddNode("Math_Multiply");
            CHL_ASSERT_TRUE(graph.AddLink(current->id, "Out", nextNode->id, "A"));
        }
        current = nextNode;
    }

    CHL_ASSERT_TRUE(graph.AddLink(current->id, "Out", master->id, "Roughness"));

    ShaderCompilerOptions options;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);
    CHL_ASSERT_EQ(code.executionOrder.size(), (size_t)(CHAIN_DEPTH + 2)); // 1 input + 100 chain + 1 master

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    if (prog == 0) {
        std::cerr << "\n    [OPENGL ERROR Deep DAG] " << err << std::endl;
        return false;
    }
    CHL_ASSERT_TRUE(glIsProgram(prog));
    glDeleteProgram(prog);
    return true;
}

// 1.4: Multi-Procedural Noise and Voronoi Nodes in same graph
bool Test_Part1_04_MultiProceduralNoiseAndVoronoi_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* noiseNode = graph.AddNode("Procedural_Noise");
    ShaderNode* voronoiNode = graph.AddNode("Procedural_Voronoi");

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", noiseNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", voronoiNode->id, "UV"));

    // Multiply noise by voronoi cells
    ShaderNode* mulNode = graph.AddNode("Math_Multiply");
    CHL_ASSERT_TRUE(graph.AddLink(noiseNode->id, "Out", mulNode->id, "A"));
    CHL_ASSERT_TRUE(graph.AddLink(voronoiNode->id, "Cells", mulNode->id, "B"));

    CHL_ASSERT_TRUE(graph.AddLink(mulNode->id, "Out", master->id, "AO"));
    CHL_ASSERT_TRUE(graph.AddLink(voronoiNode->id, "Out", master->id, "Roughness"));

    ShaderCompilerOptions options;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    if (prog == 0) {
        std::cerr << "\n    [OPENGL ERROR Procedural Noise/Voronoi] " << err << std::endl;
        return false;
    }
    CHL_ASSERT_TRUE(glIsProgram(prog));
    glDeleteProgram(prog);
    return true;
}

// 1.5: Dead Code Elimination (Orphaned nodes pruned, compilation succeeds)
bool Test_Part1_05_DeadCodeElimination_PrunesOrphans() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    // Connect valid color to albedo
    ShaderNode* validCol = graph.AddNode("Input_Vec3");
    dynamic_cast<Vec3ConstantNode*>(validCol)->value = glm::vec3(0.5f, 0.5f, 0.5f);
    CHL_ASSERT_TRUE(graph.AddLink(validCol->id, "Out", master->id, "Albedo"));

    // Add 25 disconnected / orphan nodes that do nothing
    for (int i = 0; i < 25; ++i) {
        graph.AddNode("Procedural_Voronoi");
        graph.AddNode("Math_Divide");
        graph.AddNode("Texture_Sampler");
    }

    ShaderCompilerOptions options;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);
    // Only validCol and master should be in execution order
    CHL_ASSERT_EQ(code.executionOrder.size(), (size_t)2);

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    CHL_ASSERT(prog != 0, "Clean compilation despite 75 orphan nodes");
    glDeleteProgram(prog);
    return true;
}

// 1.6: Mobile GLES 3.0 Target Version Emitter
bool Test_Part1_06_MobileGLES300_CompilationCheck() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Unlit);

    MasterNode* master = graph.GetMasterNode();
    ShaderNode* colNode = graph.AddNode("Input_Vec3");
    dynamic_cast<Vec3ConstantNode*>(colNode)->value = glm::vec3(0.0f, 1.0f, 0.0f);
    CHL_ASSERT_TRUE(graph.AddLink(colNode->id, "Out", master->id, "Color"));

    ShaderCompilerOptions options;
    options.targetVersion = GLSLTargetVersion::Mobile_GLES300_ES;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);
    CHL_ASSERT(code.vertexSource.find("#version 300 es") != std::string::npos, "Vertex shader has GLES 3.0 header");
    CHL_ASSERT(code.fragmentSource.find("#version 300 es") != std::string::npos, "Fragment shader has GLES 3.0 header");
    CHL_ASSERT(code.fragmentSource.find("precision highp float;") != std::string::npos, "Fragment shader has precision qualifiers");
    return true;
}

// 1.7: Disconnected MasterNode Inputs fallback safely to default literals
bool Test_Part1_07_DisconnectedMasterNode_SafeDefaults() {
    ShaderGraph litGraph;
    litGraph.SetMaterialType(ShaderGraphType::Lit);
    GeneratedShaderCode litCode = ShaderGraphCompiler::Compile(litGraph);
    CHL_ASSERT_TRUE(litCode.success);

    std::string err;
    GLuint pLit = CompileRawGLSL(litCode.vertexSource, litCode.fragmentSource, err);
    CHL_ASSERT(pLit != 0, "Disconnected Lit master compiles in OpenGL");
    glDeleteProgram(pLit);

    ShaderGraph unlitGraph;
    unlitGraph.SetMaterialType(ShaderGraphType::Unlit);
    GeneratedShaderCode unlitCode = ShaderGraphCompiler::Compile(unlitGraph);
    CHL_ASSERT_TRUE(unlitCode.success);

    GLuint pUnlit = CompileRawGLSL(unlitCode.vertexSource, unlitCode.fragmentSource, err);
    CHL_ASSERT(pUnlit != 0, "Disconnected Unlit master compiles in OpenGL");
    glDeleteProgram(pUnlit);
    return true;
}

// 1.8: Normal mapping path with UV, TilingOffset, and TextureSampler
bool Test_Part1_08_NormalMappingTBNReconstruction_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* tilingNode = graph.AddNode("Texture_TilingAndOffset");
    ShaderNode* samplerNode = graph.AddNode("Texture_Sampler");

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", tilingNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(tilingNode->id, "Out", samplerNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(samplerNode->id, "Color", master->id, "Normal"));

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    CHL_ASSERT_TRUE(code.success);

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    if (prog == 0) {
        std::cerr << "\n    [OPENGL ERROR Normal Mapping] " << err << std::endl;
        return false;
    }
    CHL_ASSERT_TRUE(glIsProgram(prog));
    glDeleteProgram(prog);
    return true;
}

// ============================================================================
// PART 2: SlipMaterial Parameter Overrides & Uniform Reflection Stress
// ============================================================================

// 2.1: Declared Blackboard properties compile as GLSL uniforms
bool Test_Part2_01_BlackboardProperties_GLSLUniformsEmission() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    graph.AddProperty("RoughnessMultiplier", PinType::Float, glm::vec4(0.75f));
    graph.AddProperty("TintColor", PinType::Vec4, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
    graph.AddProperty("NormalMap", PinType::Texture2D, glm::vec4(0.0f), "assets/textures/normal.png");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    CHL_ASSERT_TRUE(code.success);

    CHL_ASSERT(code.fragmentSource.find("uniform float u_RoughnessMultiplier;") != std::string::npos, "Float uniform emitted");
    CHL_ASSERT(code.fragmentSource.find("uniform vec4 u_TintColor;") != std::string::npos, "Vec4 uniform emitted");
    CHL_ASSERT(code.fragmentSource.find("uniform sampler2D u_NormalMap;") != std::string::npos, "Sampler2D uniform emitted");

    CHL_ASSERT_EQ(code.textureBindings.size(), (size_t)1);
    CHL_ASSERT_EQ(code.textureBindings[0].textureUnit, 20); // Unit 20
    return true;
}

// 2.2: SlipMaterial getter/setter APIs consistency
bool Test_Part2_02_SlipMaterial_ParameterGettersSetters() {
    SlipMaterial mat;
    mat.SetGraphFloat("Roughness", 0.35f);
    mat.SetGraphVector("EmissionColor", glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
    mat.SetGraphTexture("AlbedoTex", "assets/textures/diffuse.png");

    CHL_ASSERT_TRUE(mat.HasGraphFloat("Roughness"));
    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("Roughness"), 0.35f, 0.001f);

    CHL_ASSERT_TRUE(mat.HasGraphVector("EmissionColor"));
    glm::vec4 v = mat.GetGraphVector("EmissionColor");
    CHL_ASSERT(std::abs(v.x - 0.2f) < 0.001f && std::abs(v.y - 0.4f) < 0.001f && std::abs(v.z - 0.8f) < 0.001f, "Vector values match");

    CHL_ASSERT_TRUE(mat.HasGraphTexture("AlbedoTex"));
    CHL_ASSERT_EQ(mat.GetGraphTexture("AlbedoTex"), "assets/textures/diffuse.png");

    // Non-existent fallback
    CHL_ASSERT_FALSE(mat.HasGraphFloat("NonExistent"));
    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("NonExistent", 42.0f), 42.0f, 0.001f);
    return true;
}

// 2.3: Rapid parameter override stress: 10,000 updates of floats and vectors
bool Test_Part2_03_RapidParameterUpdates_Stress10000() {
    SlipMaterial mat;
    const int ITERATIONS = 10000;
    for (int i = 0; i < ITERATIONS; ++i) {
        float fVal = (float)i * 0.001f;
        mat.SetGraphFloat("DynamicFloat", fVal);
        mat.SetGraphVector("DynamicVec", glm::vec4(fVal, fVal * 2.0f, fVal * 3.0f, 1.0f));
    }

    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("DynamicFloat"), 9.999f, 0.01f);
    glm::vec4 finalVec = mat.GetGraphVector("DynamicVec");
    CHL_ASSERT_FLOAT_EQ_EPS(finalVec.y, 19.998f, 0.02f);
    return true;
}

// 2.4: Real OpenGL uniform upload and GPU memory query verification via glGetUniformfv
bool Test_Part2_04_UniformUploadAndGLReflectionVerification() {
    // 1. Build a graph with exposed properties and link property to master node
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderProperty* propRough = graph.AddProperty("RoughnessVal", PinType::Float, glm::vec4(0.5f));
    ShaderNode* propNode = graph.AddNode("Input_Property");
    dynamic_cast<PropertyNode*>(propNode)->SetProperty(*propRough);
    CHL_ASSERT_TRUE(graph.AddLink(propNode->id, "Out", master->id, "Roughness"));

    ShaderProperty* propColor = graph.AddProperty("BaseColor", PinType::Vec4, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    ShaderNode* propColorNode = graph.AddNode("Input_Property");
    dynamic_cast<PropertyNode*>(propColorNode)->SetProperty(*propColor);
    CHL_ASSERT_TRUE(graph.AddLink(propColorNode->id, "Out", master->id, "Albedo"));

    std::string tempPath = "challenger_test_reflection.shadergraph";
    CHL_ASSERT_TRUE(graph.SaveToFile(tempPath));

    // 2. Load into SlipMaterial
    SlipMaterial mat;
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(tempPath));
    CHL_ASSERT(mat.shader != nullptr, "Active shader compiled");

    // 3. Set custom overrides
    mat.SetGraphFloat("RoughnessVal", 0.88f);
    mat.SetGraphVector("BaseColor", glm::vec4(0.7f, 0.8f, 0.9f, 1.0f));

    // 4. Bind parameters to shader program
    mat.shader->use();
    mat.bindMaterialProperties(mat.shader);

    // 5. Query GPU uniform locations and inspect values directly from OpenGL driver
    GLint locRough = glGetUniformLocation(mat.shader->ID, "u_RoughnessVal");
    CHL_ASSERT(locRough != -1, "u_RoughnessVal location found on GPU");
    float readRough = 0.0f;
    glGetUniformfv(mat.shader->ID, locRough, &readRough);
    CHL_ASSERT_FLOAT_EQ_EPS(readRough, 0.88f, 0.0001f);

    GLint locColor = glGetUniformLocation(mat.shader->ID, "u_BaseColor");
    CHL_ASSERT(locColor != -1, "u_BaseColor location found on GPU");
    float readColor[4] = {0};
    glGetUniformfv(mat.shader->ID, locColor, readColor);
    CHL_ASSERT_FLOAT_EQ_EPS(readColor[0], 0.7f, 0.0001f);
    CHL_ASSERT_FLOAT_EQ_EPS(readColor[1], 0.8f, 0.0001f);
    CHL_ASSERT_FLOAT_EQ_EPS(readColor[2], 0.9f, 0.0001f);

    std::remove(tempPath.c_str());
    return true;
}

// 2.5: Custom texture binding units starting at slot 20
bool Test_Part2_05_TextureUnitStartingSlot20() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    graph.AddProperty("TexA", PinType::Texture2D, glm::vec4(0.0f), "texA.png");
    graph.AddProperty("TexB", PinType::Texture2D, glm::vec4(0.0f), "texB.png");
    graph.AddProperty("TexC", PinType::Texture2D, glm::vec4(0.0f), "texC.png");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    CHL_ASSERT_TRUE(code.success);
    CHL_ASSERT_EQ(code.textureBindings.size(), (size_t)3);
    CHL_ASSERT_EQ(code.textureBindings[0].textureUnit, 20);
    CHL_ASSERT_EQ(code.textureBindings[1].textureUnit, 21);
    CHL_ASSERT_EQ(code.textureBindings[2].textureUnit, 22);
    return true;
}

// ============================================================================
// PART 3: Runtime Re-compilation & Shader Resource Management
// ============================================================================

// 3.1 & 3.2: Re-compiling a graph on existing SlipMaterial and asserting clean deletion of old shader program
bool Test_Part3_01_RecompileGraph_CleanOpenGLProgramReplacement() {
    // Create Graph A (Lit)
    ShaderGraph graphA;
    graphA.SetMaterialType(ShaderGraphType::Lit);
    std::string pathA = "challenger_graph_A.shadergraph";
    CHL_ASSERT_TRUE(graphA.SaveToFile(pathA));

    // Create Graph B (Unlit)
    ShaderGraph graphB;
    graphB.SetMaterialType(ShaderGraphType::Unlit);
    std::string pathB = "challenger_graph_B.shadergraph";
    CHL_ASSERT_TRUE(graphB.SaveToFile(pathB));

    SlipMaterial mat;
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(pathA));
    CHL_ASSERT(mat.shader != nullptr, "Material shader initialized with Graph A");
    GLuint progIdA = mat.shader->ID;
    CHL_ASSERT(progIdA > 0, "Valid program ID for A");
    CHL_ASSERT_TRUE(glIsProgram(progIdA));
    CHL_ASSERT_EQ(std::string(mat.shaderType), "Lit");

    // Recompile with Graph B
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(pathB));
    CHL_ASSERT(mat.shader != nullptr, "Material shader initialized with Graph B");
    GLuint progIdB = mat.shader->ID;
    CHL_ASSERT(progIdB > 0, "Valid program ID for B");
    CHL_ASSERT_TRUE(glIsProgram(progIdB));
    CHL_ASSERT_EQ(std::string(mat.shaderType), "Unlit");

    // CRITICAL ADVERSARIAL ASSERTION: Old program A must be deleted!
    CHL_ASSERT(progIdA != progIdB, "New shader program must be created");
    CHL_ASSERT_FALSE(glIsProgram(progIdA)); // Must have been deleted!

    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
    return true;
}

// 3.3: Rapid re-compilation cycle: 50 switches between Lit and Unlit on single SlipMaterial
bool Test_Part3_02_RapidRecompilationCycle_50Iterations() {
    ShaderGraph graphLit;
    graphLit.SetMaterialType(ShaderGraphType::Lit);
    std::string pathLit = "challenger_cycle_lit.shadergraph";
    CHL_ASSERT_TRUE(graphLit.SaveToFile(pathLit));

    ShaderGraph graphUnlit;
    graphUnlit.SetMaterialType(ShaderGraphType::Unlit);
    std::string pathUnlit = "challenger_cycle_unlit.shadergraph";
    CHL_ASSERT_TRUE(graphUnlit.SaveToFile(pathUnlit));

    SlipMaterial mat;
    GLuint lastId = 0;

    for (int i = 0; i < 50; ++i) {
        const std::string& targetPath = (i % 2 == 0) ? pathLit : pathUnlit;
        const char* expectedType = (i % 2 == 0) ? "Lit" : "Unlit";

        CHL_ASSERT_TRUE(mat.LoadShaderGraph(targetPath));
        CHL_ASSERT(mat.shader != nullptr, "Shader must be valid");
        CHL_ASSERT_EQ(std::string(mat.shaderType), expectedType);

        GLuint currId = mat.shader->ID;
        CHL_ASSERT_TRUE(glIsProgram(currId));

        if (lastId != 0) {
            CHL_ASSERT_FALSE(glIsProgram(lastId)); // Previous shader was destroyed
        }
        lastId = currId;
    }

    std::remove(pathLit.c_str());
    std::remove(pathUnlit.c_str());
    return true;
}

// 3.4: Overrides persistence across re-compilations with shared property names
bool Test_Part3_03_PropertyOverridesPersistence_AcrossRecompilations() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    graph.AddProperty("CustomFactor", PinType::Float, glm::vec4(1.0f));
    std::string path = "challenger_persist_props.shadergraph";
    CHL_ASSERT_TRUE(graph.SaveToFile(path));

    SlipMaterial mat;
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(path));

    // Initially gets default value (1.0f)
    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("CustomFactor"), 1.0f, 0.001f);

    // User overrides value
    mat.SetGraphFloat("CustomFactor", 4.25f);

    // Re-compile same graph
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(path));

    // Override must be preserved, not overwritten by graph default!
    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("CustomFactor"), 4.25f, 0.001f);

    std::remove(path.c_str());
    return true;
}

// 3.5: Material destruction cleans up m_CustomShader and OpenGL program
bool Test_Part3_04_MaterialDestruction_DeletesOpenGLProgram() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    std::string path = "challenger_dtor.shadergraph";
    CHL_ASSERT_TRUE(graph.SaveToFile(path));

    GLuint programId = 0;
    {
        SlipMaterial mat;
        CHL_ASSERT_TRUE(mat.LoadShaderGraph(path));
        CHL_ASSERT(mat.shader != nullptr, "Shader created");
        programId = mat.shader->ID;
        CHL_ASSERT_TRUE(glIsProgram(programId));
    } // mat destructor called here

    // Program must be deleted
    CHL_ASSERT_FALSE(glIsProgram(programId));

    std::remove(path.c_str());
    return true;
}

// ============================================================================
// PART 4: Asset Serialization & Full Round-Trip Disk Integration
// ============================================================================

// 4.1: Full Round-Trip (.shadergraph -> SlipMaterial -> .mat -> fresh reload)
bool Test_Part4_01_FullDiskRoundTrip_ShaderGraphAndMaterial() {
    std::string sgPath = "challenger_roundtrip.shadergraph";
    std::string matPath = "challenger_roundtrip.mat";

    // 1. Create and save shader graph
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    graph.AddProperty("SpecularIntensity", PinType::Float, glm::vec4(0.5f));
    graph.AddProperty("BaseTint", PinType::Vec4, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
    CHL_ASSERT_TRUE(graph.SaveToFile(sgPath));

    // 2. Load into Material and set overrides
    SlipMaterial mat1;
    CHL_ASSERT_TRUE(mat1.LoadShaderGraph(sgPath));
    mat1.SetGraphFloat("SpecularIntensity", 0.95f);
    mat1.SetGraphVector("BaseTint", glm::vec4(0.9f, 0.8f, 0.7f, 1.0f));
    CHL_ASSERT_TRUE(mat1.Save(matPath));

    // 3. Reload in fresh SlipMaterial
    SlipMaterial mat2(matPath);
    CHL_ASSERT_EQ(mat2.shaderGraphPath, sgPath);
    CHL_ASSERT_FLOAT_EQ_EPS(mat2.GetGraphFloat("SpecularIntensity"), 0.95f, 0.001f);

    glm::vec4 v = mat2.GetGraphVector("BaseTint");
    CHL_ASSERT_FLOAT_EQ_EPS(v.x, 0.9f, 0.001f);
    CHL_ASSERT_FLOAT_EQ_EPS(v.y, 0.8f, 0.001f);
    CHL_ASSERT_FLOAT_EQ_EPS(v.z, 0.7f, 0.001f);

    // Initializing mat2 compiles the shader graph
    mat2.init();
    CHL_ASSERT(mat2.shader != nullptr, "Shader re-compiled on fresh material");
    CHL_ASSERT_TRUE(glIsProgram(mat2.shader->ID));

    std::remove(sgPath.c_str());
    std::remove(matPath.c_str());
    return true;
}

// 4.2: Corrupted or non-existent shadergraph fallback is graceful
bool Test_Part4_02_NonExistentOrCorruptFile_GracefulFallback() {
    SlipMaterial mat;
    // Non-existent path returns false without throwing or crashing
    bool res = mat.LoadShaderGraph("non_existent_graph_123456.shadergraph");
    CHL_ASSERT_FALSE(res);

    // Empty path resets to default shader safely
    bool resEmpty = mat.LoadShaderGraph("");
    CHL_ASSERT_TRUE(resEmpty);
    return true;
}

// 4.3: Verify sample assets exist and compile
bool Test_Part4_03_SampleAssets_CompilationCheck() {
    std::vector<std::string> samplePaths = {
        "assets/materials/sample_animated_procedural.shadergraph",
        "assets/materials/sample_lit_pbr.shadergraph"
    };

    for (const auto& path : samplePaths) {
        if (!SlipFileSystem::Exists(path)) {
            std::cout << "\n    [NOTE] Sample asset " << path << " not on disk yet (M4 scope)." << std::endl;
            continue;
        }

        SlipShaderGraph::ShaderGraph g;
        std::string err;
        CHL_ASSERT(SlipShaderGraph::ShaderGraphSerializer::DeserializeFromFile(g, path, &err), "Sample deserializes");

        SlipShaderGraph::GeneratedShaderCode code = SlipShaderGraph::ShaderGraphCompiler::Compile(g);
        CHL_ASSERT(code.success, "Sample compiles to GLSL");

        std::string glErr;
        GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, glErr);
        CHL_ASSERT(prog != 0, "Sample links in OpenGL");
        glDeleteProgram(prog);
    }
    return true;
}

// ============================================================================
// MAIN RUNNER
// ============================================================================

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    std::cout << "================================================================================" << std::endl;
    std::cout << "   SlipEngine Milestone 2 EMPIRICAL CHALLENGER STRESS SUITE (OpenGL GLSL)       " << std::endl;
    std::cout << "================================================================================" << std::endl;

    // Initialize Headless OpenGL Context via SDL2 + GLAD
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[CRITICAL ERROR] Failed to initialize SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("SlipGLSLChallengerContext", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "[CRITICAL ERROR] Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "[CRITICAL ERROR] Failed to initialize GLAD / OpenGL 3.3 Core profile" << std::endl;
        if (glCtx) SDL_GL_DeleteContext(glCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "[INFO] Headless OpenGL Context loaded successfully." << std::endl;
    std::cout << "[INFO] Vendor:   " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "[INFO] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[INFO] GLSL:     " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "--------------------------------------------------------------------------------\n" << std::endl;

    // PART 1: GLSL CodeGen & OpenGL Linking
    std::cout << ">>> Part 1: GLSL Generation & Real OpenGL Compilation / Linking <<<" << std::endl;
    RUN_CHALLENGE("1.1 Complex Lit PBR Graph with Math, Trig & Noise", Test_Part1_01_ComplexLitPBR_OpenGLCompilation);
    RUN_CHALLENGE("1.2 Dynamic Unlit Graph with Animated Time & Color", Test_Part1_02_DynamicUnlit_AnimatedTimeColor_OpenGLCompilation);
    RUN_CHALLENGE("1.3 Deep DAG Chain (100+ Nodes) Stress Compilation", Test_Part1_03_DeepDAGChain100Nodes_OpenGLCompilation);
    RUN_CHALLENGE("1.4 Multi-Procedural Noise and Voronoi Evaluation", Test_Part1_04_MultiProceduralNoiseAndVoronoi_OpenGLCompilation);
    RUN_CHALLENGE("1.5 Dead Code Elimination with 75 Orphan Nodes", Test_Part1_05_DeadCodeElimination_PrunesOrphans);
    RUN_CHALLENGE("1.6 Mobile GLES 3.0 Target Header & Precision Emitter", Test_Part1_06_MobileGLES300_CompilationCheck);
    RUN_CHALLENGE("1.7 Disconnected MasterNode Safe Fallback Defaults", Test_Part1_07_DisconnectedMasterNode_SafeDefaults);
    RUN_CHALLENGE("1.8 Normal Mapping TBN Perturbation Compilation", Test_Part1_08_NormalMappingTBNReconstruction_OpenGLCompilation);

    // PART 2: SlipMaterial Parameter Overrides & Reflection
    std::cout << "\n>>> Part 2: SlipMaterial Parameter Overrides & Uniform Reflection Stress <<<" << std::endl;
    RUN_CHALLENGE("2.1 Blackboard Properties GLSL Uniform Emission", Test_Part2_01_BlackboardProperties_GLSLUniformsEmission);
    RUN_CHALLENGE("2.2 SlipMaterial Dynamic Parameter Getters & Setters", Test_Part2_02_SlipMaterial_ParameterGettersSetters);
    RUN_CHALLENGE("2.3 Rapid Parameter Overrides Stress (10,000 Updates)", Test_Part2_03_RapidParameterUpdates_Stress10000);
    RUN_CHALLENGE("2.4 Uniform Upload & Real GPU Memory Reflection (glGetUniformfv)", Test_Part2_04_UniformUploadAndGLReflectionVerification);
    RUN_CHALLENGE("2.5 Texture Unit Assignment Boundary (Slots 20+)", Test_Part2_05_TextureUnitStartingSlot20);

    // PART 3: Runtime Re-compilation & Shader Resource Management
    std::cout << "\n>>> Part 3: Runtime Re-compilation & Shader Resource Management <<<" << std::endl;
    RUN_CHALLENGE("3.1 Re-compilation & Clean OpenGL Program Replacement", Test_Part3_01_RecompileGraph_CleanOpenGLProgramReplacement);
    RUN_CHALLENGE("3.2 Rapid Re-compilation Cycle Stress (50 Iterations)", Test_Part3_02_RapidRecompilationCycle_50Iterations);
    RUN_CHALLENGE("3.3 Parameter Overrides Persistence Across Re-compilations", Test_Part3_03_PropertyOverridesPersistence_AcrossRecompilations);
    RUN_CHALLENGE("3.4 Material Destruction OpenGL Program Cleanup", Test_Part3_04_MaterialDestruction_DeletesOpenGLProgram);

    // PART 4: Asset Serialization & Full Round-Trip Disk Integration
    std::cout << "\n>>> Part 4: Asset Serialization & Full Round-Trip Disk Integration <<<" << std::endl;
    RUN_CHALLENGE("4.1 Full Round-Trip (.shadergraph -> SlipMaterial -> .mat)", Test_Part4_01_FullDiskRoundTrip_ShaderGraphAndMaterial);
    RUN_CHALLENGE("4.2 Non-Existent or Corrupt Graph Graceful Handling", Test_Part4_02_NonExistentOrCorruptFile_GracefulFallback);
    RUN_CHALLENGE("4.3 Sample Shader Graph Assets Compilation Check", Test_Part4_03_SampleAssets_CompilationCheck);

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "                  CHALLENGER STRESS SUITE EXECUTION SUMMARY                     " << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "  TOTAL CHALLENGES: " << g_Stats.total << std::endl;
    std::cout << "  PASSED:           " << g_Stats.passed << std::endl;
    std::cout << "  FAILED:           " << g_Stats.failed << std::endl;
    std::cout << "================================================================================" << std::endl;

    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (g_Stats.failed == 0) {
        std::cout << "\n[SUCCESS] ALL " << g_Stats.total << " EMPIRICAL CHALLENGES PASSED CLEANLY!\n" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[FAILURE] " << g_Stats.failed << " challenge(s) failed out of " << g_Stats.total << "\n" << std::endl;
        return 1;
    }
}
