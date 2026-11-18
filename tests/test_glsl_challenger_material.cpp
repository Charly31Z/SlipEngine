#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include "test_glsl_challenger.h"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <fstream>

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
#define CHL_ASSERT_FLOAT_EQ_EPS(a, b, eps) CHL_ASSERT((std::abs((a) - (b)) <= (eps)), "Floats must be equal within epsilon")

#define RUN_MATERIAL_CHALLENGE(name, func) \
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

// ============================================================================
// PART 2: SlipMaterial Parameter Overrides & Uniform Reflection Stress
// ============================================================================

// 2.1: SlipMaterial getter/setter APIs consistency
static bool Test_Part2_01_SlipMaterial_ParameterGettersSetters() {
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

    CHL_ASSERT_FALSE(mat.HasGraphFloat("NonExistent"));
    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("NonExistent", 42.0f), 42.0f, 0.001f);
    return true;
}

// 2.2: Rapid parameter override stress: 10,000 updates of floats and vectors
static bool Test_Part2_02_RapidParameterUpdates_Stress10000() {
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

// 2.3: Real OpenGL uniform upload and GPU memory query verification via glGetUniformfv
static bool Test_Part2_03_UniformUploadAndGLReflectionVerification() {
    std::string tempPath = "challenger_test_reflection.shadergraph";
    CHL_ASSERT_TRUE(CreateChallengerReflectionGraph(tempPath.c_str()));

    SlipMaterial mat;
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(tempPath));
    CHL_ASSERT(mat.shader != nullptr, "Active shader compiled on SlipMaterial");

    mat.SetGraphFloat("RoughnessVal", 0.88f);
    mat.SetGraphVector("BaseColor", glm::vec4(0.7f, 0.8f, 0.9f, 1.0f));

    mat.shader->use();
    mat.bindMaterialProperties(mat.shader);

    GLint locRough = glGetUniformLocation(mat.shader->ID, "u_RoughnessVal");
    CHL_ASSERT(locRough != -1, "u_RoughnessVal location found on GPU program");
    float readRough = 0.0f;
    glGetUniformfv(mat.shader->ID, locRough, &readRough);
    CHL_ASSERT_FLOAT_EQ_EPS(readRough, 0.88f, 0.0001f);

    GLint locColor = glGetUniformLocation(mat.shader->ID, "u_BaseColor");
    CHL_ASSERT(locColor != -1, "u_BaseColor location found on GPU program");
    float readColor[4] = {0};
    glGetUniformfv(mat.shader->ID, locColor, readColor);
    CHL_ASSERT_FLOAT_EQ_EPS(readColor[0], 0.7f, 0.0001f);
    CHL_ASSERT_FLOAT_EQ_EPS(readColor[1], 0.8f, 0.0001f);
    CHL_ASSERT_FLOAT_EQ_EPS(readColor[2], 0.9f, 0.0001f);

    std::remove(tempPath.c_str());
    return true;
}

// ============================================================================
// PART 3: Runtime Re-compilation & Shader Resource Management
// ============================================================================

// 3.1: Re-compiling a graph on existing SlipMaterial and asserting clean deletion of old shader program
static bool Test_Part3_01_RecompileGraph_CleanOpenGLProgramReplacement() {
    std::string pathA = "challenger_graph_A.shadergraph";
    std::string pathB = "challenger_graph_B.shadergraph";
    CHL_ASSERT_TRUE(CreateChallengerGraphA(pathA.c_str()));
    CHL_ASSERT_TRUE(CreateChallengerGraphB(pathB.c_str()));

    SlipMaterial mat;
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(pathA));
    CHL_ASSERT(mat.shader != nullptr, "Material shader initialized with Graph A");
    GLuint progIdA = mat.shader->ID;
    CHL_ASSERT(progIdA > 0, "Valid program ID for A");
    CHL_ASSERT_TRUE(glIsProgram(progIdA));
    CHL_ASSERT_EQ(std::string(mat.shaderType), "Lit");

    CHL_ASSERT_TRUE(mat.LoadShaderGraph(pathB));
    CHL_ASSERT(mat.shader != nullptr, "Material shader initialized with Graph B");
    GLuint progIdB = mat.shader->ID;
    CHL_ASSERT(progIdB > 0, "Valid program ID for B");
    CHL_ASSERT_TRUE(glIsProgram(progIdB));
    CHL_ASSERT_EQ(std::string(mat.shaderType), "Unlit");

    CHL_ASSERT(progIdA != progIdB, "New shader program must be created");
    CHL_ASSERT_FALSE(glIsProgram(progIdA)); // Old program must be destroyed

    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
    return true;
}

// 3.2: Rapid re-compilation cycle: 50 switches between Lit and Unlit on single SlipMaterial
static bool Test_Part3_02_RapidRecompilationCycle_50Iterations() {
    std::string pathLit = "challenger_cycle_lit.shadergraph";
    std::string pathUnlit = "challenger_cycle_unlit.shadergraph";
    CHL_ASSERT_TRUE(CreateChallengerGraphA(pathLit.c_str()));
    CHL_ASSERT_TRUE(CreateChallengerGraphB(pathUnlit.c_str()));

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
            CHL_ASSERT_FALSE(glIsProgram(lastId)); // Previous shader destroyed cleanly
        }
        lastId = currId;
    }

    std::remove(pathLit.c_str());
    std::remove(pathUnlit.c_str());
    return true;
}

// 3.3: Overrides persistence across re-compilations with shared property names
static bool Test_Part3_03_PropertyOverridesPersistence_AcrossRecompilations() {
    std::string path = "challenger_persist_props.shadergraph";
    CHL_ASSERT_TRUE(CreateChallengerPropsGraph(path.c_str()));

    SlipMaterial mat;
    CHL_ASSERT_TRUE(mat.LoadShaderGraph(path));

    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("CustomFactor"), 1.0f, 0.001f);

    mat.SetGraphFloat("CustomFactor", 4.25f);

    CHL_ASSERT_TRUE(mat.LoadShaderGraph(path));

    CHL_ASSERT_FLOAT_EQ_EPS(mat.GetGraphFloat("CustomFactor"), 4.25f, 0.001f);

    std::remove(path.c_str());
    return true;
}

// 3.4: Material destruction cleans up m_CustomShader and OpenGL program
static bool Test_Part3_04_MaterialDestruction_DeletesOpenGLProgram() {
    std::string path = "challenger_dtor.shadergraph";
    CHL_ASSERT_TRUE(CreateChallengerGraphA(path.c_str()));

    GLuint programId = 0;
    {
        SlipMaterial mat;
        CHL_ASSERT_TRUE(mat.LoadShaderGraph(path));
        CHL_ASSERT(mat.shader != nullptr, "Shader created");
        programId = mat.shader->ID;
        CHL_ASSERT_TRUE(glIsProgram(programId));
    } // mat destructor runs here

    CHL_ASSERT_FALSE(glIsProgram(programId));

    std::remove(path.c_str());
    return true;
}

// ============================================================================
// PART 4: Asset Serialization & Full Round-Trip Disk Integration
// ============================================================================

// 4.1: Full Round-Trip (.shadergraph -> SlipMaterial -> .mat -> fresh reload)
static bool Test_Part4_01_FullDiskRoundTrip_ShaderGraphAndMaterial() {
    std::string sgPath = "challenger_roundtrip.shadergraph";
    std::string matPath = "challenger_roundtrip.mat";

    CHL_ASSERT_TRUE(CreateChallengerRoundTripGraph(sgPath.c_str()));

    SlipMaterial mat1;
    CHL_ASSERT_TRUE(mat1.LoadShaderGraph(sgPath));
    mat1.SetGraphFloat("SpecularIntensity", 0.95f);
    mat1.SetGraphVector("BaseTint", glm::vec4(0.9f, 0.8f, 0.7f, 1.0f));
    CHL_ASSERT_TRUE(mat1.Save(matPath));

    SlipMaterial mat2(matPath);
    CHL_ASSERT_EQ(mat2.shaderGraphPath, sgPath);
    CHL_ASSERT_FLOAT_EQ_EPS(mat2.GetGraphFloat("SpecularIntensity"), 0.95f, 0.001f);

    glm::vec4 v = mat2.GetGraphVector("BaseTint");
    CHL_ASSERT_FLOAT_EQ_EPS(v.x, 0.9f, 0.001f);
    CHL_ASSERT_FLOAT_EQ_EPS(v.y, 0.8f, 0.001f);
    CHL_ASSERT_FLOAT_EQ_EPS(v.z, 0.7f, 0.001f);

    mat2.init();
    CHL_ASSERT(mat2.shader != nullptr, "Shader re-compiled on fresh material");
    CHL_ASSERT_TRUE(glIsProgram(mat2.shader->ID));

    std::remove(sgPath.c_str());
    std::remove(matPath.c_str());
    return true;
}

// 4.2: Corrupted or non-existent shadergraph fallback is graceful
static bool Test_Part4_02_NonExistentOrCorruptFile_GracefulFallback() {
    SlipMaterial mat;
    bool res = mat.LoadShaderGraph("non_existent_graph_123456.shadergraph");
    CHL_ASSERT_FALSE(res);

    bool resEmpty = mat.LoadShaderGraph("");
    CHL_ASSERT_TRUE(resEmpty);
    return true;
}

// Runner
bool RunSlipMaterialChallenges() {
    std::cout << "\n>>> Part 2: SlipMaterial Parameter Overrides & Uniform Reflection Stress <<<" << std::endl;
    RUN_MATERIAL_CHALLENGE("2.1 SlipMaterial Dynamic Parameter Getters & Setters", Test_Part2_01_SlipMaterial_ParameterGettersSetters);
    RUN_MATERIAL_CHALLENGE("2.2 Rapid Parameter Overrides Stress (10,000 Updates)", Test_Part2_02_RapidParameterUpdates_Stress10000);
    RUN_MATERIAL_CHALLENGE("2.3 Uniform Upload & Real GPU Memory Reflection (glGetUniformfv)", Test_Part2_03_UniformUploadAndGLReflectionVerification);

    std::cout << "\n>>> Part 3: Runtime Re-compilation & Shader Resource Management <<<" << std::endl;
    RUN_MATERIAL_CHALLENGE("3.1 Re-compilation & Clean OpenGL Program Replacement", Test_Part3_01_RecompileGraph_CleanOpenGLProgramReplacement);
    RUN_MATERIAL_CHALLENGE("3.2 Rapid Re-compilation Cycle Stress (50 Iterations)", Test_Part3_02_RapidRecompilationCycle_50Iterations);
    RUN_MATERIAL_CHALLENGE("3.3 Parameter Overrides Persistence Across Re-compilations", Test_Part3_03_PropertyOverridesPersistence_AcrossRecompilations);
    RUN_MATERIAL_CHALLENGE("3.4 Material Destruction OpenGL Program Cleanup", Test_Part3_04_MaterialDestruction_DeletesOpenGLProgram);

    std::cout << "\n>>> Part 4: Asset Serialization & Full Round-Trip Disk Integration <<<" << std::endl;
    RUN_MATERIAL_CHALLENGE("4.1 Full Round-Trip (.shadergraph -> SlipMaterial -> .mat)", Test_Part4_01_FullDiskRoundTrip_ShaderGraphAndMaterial);
    RUN_MATERIAL_CHALLENGE("4.2 Non-Existent or Corrupt Graph Graceful Handling", Test_Part4_02_NonExistentOrCorruptFile_GracefulFallback);
    return true;
}
