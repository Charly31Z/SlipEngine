#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"

#include "test_glsl_challenger.h"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cmath>

using namespace SlipShaderGraph;

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

#define RUN_COMPILER_CHALLENGE(name, func) \
    do { \
        g_GlobalChallengerStats.total++; \
        std::cout << "  [CHALLENGE] " << name << " ... " << std::flush; \
        auto start = std::chrono::high_resolution_clock::now(); \
        bool res = func(); \
        auto end = std::chrono::high_resolution_clock::now(); \
        double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count(); \
        if (res) { \
            g_GlobalChallengerStats.passed++; \
            std::cout << "[PASS] (" << std::fixed << std::setprecision(2) << elapsedMs << " ms)" << std::endl; \
        } else { \
            g_GlobalChallengerStats.failed++; \
            std::cout << "[FAILED] (" << std::fixed << std::setprecision(2) << elapsedMs << " ms)" << std::endl; \
        } \
    } while (0)

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

// 1.1: Complex Lit PBR Shader Graph with Math, Trig, and Procedural Noise
static bool Test_Part1_01_ComplexLitPBR_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* noiseNode = graph.AddNode("Procedural_Noise");
    ShaderNode* scaleNode = graph.AddNode("Input_Float");
    dynamic_cast<FloatConstantNode*>(scaleNode)->value = 8.5f;

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", noiseNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(scaleNode->id, "Out", noiseNode->id, "Scale"));

    ShaderNode* timeNode = graph.AddNode("Time_Time");
    ShaderNode* sinNode = graph.AddNode("Math_Sin");
    CHL_ASSERT_TRUE(graph.AddLink(timeNode->id, "SinTime", sinNode->id, "In"));

    ShaderNode* clampNode = graph.AddNode("Procedural_Clamp");
    CHL_ASSERT_TRUE(graph.AddLink(noiseNode->id, "Out", clampNode->id, "In"));
    CHL_ASSERT_TRUE(graph.AddLink(clampNode->id, "Out", master->id, "Roughness"));

    ShaderNode* colNode = graph.AddNode("Input_Vec3");
    dynamic_cast<Vec3ConstantNode*>(colNode)->value = glm::vec3(0.8f, 0.2f, 0.1f);
    CHL_ASSERT_TRUE(graph.AddLink(colNode->id, "Out", master->id, "Albedo"));
    CHL_ASSERT_TRUE(graph.AddLink(sinNode->id, "Out", master->id, "Metallic"));

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

    glUseProgram(prog);
    GLint locTime = glGetUniformLocation(prog, "u_Time");
    GLint locCam = glGetUniformLocation(prog, "u_CameraPos");
    CHL_ASSERT(locTime != -1 || locCam != -1, "Engine uniforms must be present in compiled shader");

    glDeleteProgram(prog);
    return true;
}

// 1.2: Dynamic Unlit Shader Graph with Animated Time, DeltaTime and Color Modulation
static bool Test_Part1_02_DynamicUnlit_AnimatedTimeColor_OpenGLCompilation() {
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

    ShaderNode* combNode = graph.AddNode("Vector_Combine");
    CHL_ASSERT_TRUE(graph.AddLink(cosNode->id, "Out", combNode->id, "X"));
    CHL_ASSERT_TRUE(graph.AddLink(cosNode->id, "Out", combNode->id, "Z"));

    CHL_ASSERT_TRUE(graph.AddLink(combNode->id, "Vec3", master->id, "Color"));

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

// 1.3: Deep DAG Chain Stress (100+ chained nodes)
static bool Test_Part1_03_DeepDAGChain100Nodes_OpenGLCompilation() {
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
    CHL_ASSERT_EQ(code.executionOrder.size(), (size_t)(CHAIN_DEPTH + 2));

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
static bool Test_Part1_04_MultiProceduralNoiseAndVoronoi_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* noiseNode = graph.AddNode("Procedural_Noise");
    ShaderNode* voronoiNode = graph.AddNode("Procedural_Voronoi");

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", noiseNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", voronoiNode->id, "UV"));

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
static bool Test_Part1_05_DeadCodeElimination_PrunesOrphans() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    MasterNode* master = graph.GetMasterNode();
    CHL_ASSERT(master != nullptr, "Master node must exist");

    ShaderNode* validCol = graph.AddNode("Input_Vec3");
    dynamic_cast<Vec3ConstantNode*>(validCol)->value = glm::vec3(0.5f, 0.5f, 0.5f);
    CHL_ASSERT_TRUE(graph.AddLink(validCol->id, "Out", master->id, "Albedo"));

    for (int i = 0; i < 25; ++i) {
        graph.AddNode("Procedural_Voronoi");
        graph.AddNode("Math_Divide");
        graph.AddNode("Texture_Sampler");
    }

    ShaderCompilerOptions options;
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph, options);
    CHL_ASSERT_TRUE(code.success);
    CHL_ASSERT_EQ(code.executionOrder.size(), (size_t)2);

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    CHL_ASSERT(prog != 0, "Clean compilation despite 75 orphan nodes");
    glDeleteProgram(prog);
    return true;
}

// 1.6: Mobile GLES 3.0 Target Version Emitter
static bool Test_Part1_06_MobileGLES300_CompilationCheck() {
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
static bool Test_Part1_07_DisconnectedMasterNode_SafeDefaults() {
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
static bool Test_Part1_08_NormalMappingTBNReconstruction_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* tilingNode = graph.AddNode("Texture_TilingAndOffset");
    ShaderNode* samplerNode = graph.AddNode("Texture_Sampler");

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", tilingNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(tilingNode->id, "Out", samplerNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(samplerNode->id, "RGB", master->id, "Normal"));

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

// 1.8b: Normal mapping path with connected texture property to Sampler pin
static bool Test_Part1_08b_NormalMappingWithConnectedSampler_OpenGLCompilation() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderNode* uvNode = graph.AddNode("Texture_UV");
    ShaderNode* tilingNode = graph.AddNode("Texture_TilingAndOffset");
    ShaderNode* samplerNode = graph.AddNode("Texture_Sampler");

    ShaderProperty* propTex = graph.AddProperty("NormalMap", PinType::Texture2D, glm::vec4(0.0f), "normal.png");
    ShaderNode* propNode = graph.AddNode("Input_Property");
    dynamic_cast<PropertyNode*>(propNode)->SetProperty(*propTex);

    CHL_ASSERT_TRUE(graph.AddLink(uvNode->id, "UV", tilingNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(tilingNode->id, "Out", samplerNode->id, "UV"));
    CHL_ASSERT_TRUE(graph.AddLink(propNode->id, "Out", samplerNode->id, "Sampler"));
    CHL_ASSERT_TRUE(graph.AddLink(samplerNode->id, "RGB", master->id, "Normal"));

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    CHL_ASSERT_TRUE(code.success);

    std::string err;
    GLuint prog = CompileRawGLSL(code.vertexSource, code.fragmentSource, err);
    if (prog == 0) {
        std::cerr << "\n    [OPENGL ERROR Normal Mapping Connected] " << err << std::endl;
        return false;
    }
    CHL_ASSERT_TRUE(glIsProgram(prog));
    glDeleteProgram(prog);
    return true;
}

// 1.9: Blackboard Properties GLSL Uniform Emission & Texture Slot 20+
static bool Test_Part1_09_BlackboardUniformsAndTextureSlots() {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);

    graph.AddProperty("RoughnessMultiplier", PinType::Float, glm::vec4(0.75f));
    graph.AddProperty("TintColor", PinType::Vec4, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
    graph.AddProperty("NormalMap", PinType::Texture2D, glm::vec4(0.0f), "assets/textures/normal.png");
    graph.AddProperty("ExtraTex", PinType::Texture2D, glm::vec4(0.0f), "assets/textures/extra.png");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    CHL_ASSERT_TRUE(code.success);

    CHL_ASSERT(code.fragmentSource.find("uniform float u_RoughnessMultiplier;") != std::string::npos, "Float uniform emitted");
    CHL_ASSERT(code.fragmentSource.find("uniform vec4 u_TintColor;") != std::string::npos, "Vec4 uniform emitted");
    CHL_ASSERT(code.fragmentSource.find("uniform sampler2D u_NormalMap;") != std::string::npos, "Sampler2D uniform emitted");

    CHL_ASSERT_EQ(code.textureBindings.size(), (size_t)2);
    CHL_ASSERT_EQ(code.textureBindings[0].textureUnit, 20); // Unit 20
    CHL_ASSERT_EQ(code.textureBindings[1].textureUnit, 21); // Unit 21
    return true;
}

// Runner
bool RunGLSLCompilerChallenges() {
    std::cout << ">>> Part 1: GLSL Generation & Real OpenGL Compilation / Linking <<<" << std::endl;
    RUN_COMPILER_CHALLENGE("1.1 Complex Lit PBR Graph with Math, Trig & Noise", Test_Part1_01_ComplexLitPBR_OpenGLCompilation);
    RUN_COMPILER_CHALLENGE("1.2 Dynamic Unlit Graph with Animated Time & Color", Test_Part1_02_DynamicUnlit_AnimatedTimeColor_OpenGLCompilation);
    RUN_COMPILER_CHALLENGE("1.3 Deep DAG Chain (100+ Nodes) Stress Compilation", Test_Part1_03_DeepDAGChain100Nodes_OpenGLCompilation);
    RUN_COMPILER_CHALLENGE("1.4 Multi-Procedural Noise and Voronoi Evaluation", Test_Part1_04_MultiProceduralNoiseAndVoronoi_OpenGLCompilation);
    RUN_COMPILER_CHALLENGE("1.5 Dead Code Elimination with 75 Orphan Nodes", Test_Part1_05_DeadCodeElimination_PrunesOrphans);
    RUN_COMPILER_CHALLENGE("1.6 Mobile GLES 3.0 Target Header & Precision Emitter", Test_Part1_06_MobileGLES300_CompilationCheck);
    RUN_COMPILER_CHALLENGE("1.7 Disconnected MasterNode Safe Fallback Defaults", Test_Part1_07_DisconnectedMasterNode_SafeDefaults);
    RUN_COMPILER_CHALLENGE("1.8a Normal Mapping Unconnected Sampler Fallback [BUG: texture(0.0, uv)]", Test_Part1_08_NormalMappingTBNReconstruction_OpenGLCompilation);
    RUN_COMPILER_CHALLENGE("1.8b Normal Mapping Connected Sampler Property", Test_Part1_08b_NormalMappingWithConnectedSampler_OpenGLCompilation);
    RUN_COMPILER_CHALLENGE("1.9 Blackboard Uniforms & Texture Slot 20 Allocation", Test_Part1_09_BlackboardUniformsAndTextureSlots);
    return true;
}

// Helpers for Material Unit
bool CreateChallengerReflectionGraph(const char* filepath) {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    MasterNode* master = graph.GetMasterNode();

    ShaderProperty* propRough = graph.AddProperty("RoughnessVal", PinType::Float, glm::vec4(0.5f));
    ShaderNode* propNode = graph.AddNode("Input_Property");
    dynamic_cast<PropertyNode*>(propNode)->SetProperty(*propRough);
    graph.AddLink(propNode->id, "Out", master->id, "Roughness");

    ShaderProperty* propColor = graph.AddProperty("BaseColor", PinType::Vec4, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    ShaderNode* propColorNode = graph.AddNode("Input_Property");
    dynamic_cast<PropertyNode*>(propColorNode)->SetProperty(*propColor);
    graph.AddLink(propColorNode->id, "Out", master->id, "Albedo");

    return graph.SaveToFile(filepath);
}

bool CreateChallengerGraphA(const char* filepath) {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    return graph.SaveToFile(filepath);
}

bool CreateChallengerGraphB(const char* filepath) {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Unlit);
    return graph.SaveToFile(filepath);
}

bool CreateChallengerPropsGraph(const char* filepath) {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    graph.AddProperty("CustomFactor", PinType::Float, glm::vec4(1.0f));
    return graph.SaveToFile(filepath);
}

bool CreateChallengerRoundTripGraph(const char* filepath) {
    ShaderGraph graph;
    graph.SetMaterialType(ShaderGraphType::Lit);
    graph.AddProperty("SpecularIntensity", PinType::Float, glm::vec4(0.5f));
    graph.AddProperty("BaseTint", PinType::Vec4, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
    return graph.SaveToFile(filepath);
}
