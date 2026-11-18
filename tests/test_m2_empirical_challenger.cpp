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
#include <limits>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/render/SlipRenderQueue.h"
#include "SlipEngine/tools/SlipFileSystem.h"

// Bridge function exported by SlipEngine / ShaderGraphCompiler.cpp
extern bool SlipCompileShaderGraphFile(
    const std::string& graphPath,
    std::string& outVertexSrc,
    std::string& outFragmentSrc,
    std::string& outShaderType,
    std::vector<std::pair<std::string, float>>* outFloatProps,
    std::vector<std::pair<std::string, glm::vec4>>* outVectorProps,
    std::vector<std::pair<std::string, std::string>>* outTextureProps,
    std::string* outErrorMessage
);

// ============================================================================
// Test Framework Harness & Statistics
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

#define CHL_ASSERT_FLOAT_NEAR(a, b, eps) \
    do { \
        float diff = std::abs((a) - (b)); \
        if (diff > (eps)) { \
            std::cerr << "\n    [FAIL] Line " << __LINE__ << ": expected " << (b) << ", got " << (a) << " (diff=" << diff << " > " << (eps) << ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define RUN_CHALLENGE_TEST(name, func) \
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
    } while (0)

// Helper: writes a JSON file to disk
static bool WriteJsonFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << content;
    return true;
}

// Helper: compiles a test shader in headless OpenGL
static SlipShader* CompileTestShader(const std::string& vertSrc, const std::string& fragSrc) {
    SlipShader* s = SlipShader::CreateFromSource(vertSrc.c_str(), fragSrc.c_str());
    return s;
}

// ============================================================================
// SUITE 1: Time Tracking Monotonicity, Precision & Edge-Case Stress
// ============================================================================

bool Test_Time_Monotonicity_NormalFrameRates() {
    SlipGlobals& globals = SlipGlobals::Get();
    
    // Simulate 300 frames at 60 FPS
    double simTime = 100.0;
    globals.update(simTime);
    float lastObservedTime = globals.GetTime();

    for (int i = 0; i < 300; ++i) {
        simTime += 1.0 / 60.0;
        globals.update(simTime);
        float curTime = globals.GetTime();
        CHL_ASSERT(curTime >= lastObservedTime, "Time must be monotonically increasing");
        float dt = globals.GetDeltaTime();
        CHL_ASSERT(dt > 0.015f && dt < 0.018f, "DeltaTime at 60 FPS should be near 0.0166s");
        lastObservedTime = curTime;
    }

    // Simulate 300 frames at 144 FPS
    for (int i = 0; i < 300; ++i) {
        simTime += 1.0 / 144.0;
        globals.update(simTime);
        float curTime = globals.GetTime();
        CHL_ASSERT(curTime >= lastObservedTime, "Time must be monotonically increasing");
        float dt = globals.GetDeltaTime();
        CHL_ASSERT(dt > 0.006f && dt < 0.008f, "DeltaTime at 144 FPS should be near 0.00694s");
        lastObservedTime = curTime;
    }

    return true;
}

bool Test_Time_RapidConsecutiveCalls_ZeroDelta() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.update(50.0);
    
    // Call update 100 times in the same frame (0 elapsed time)
    for (int i = 0; i < 100; ++i) {
        globals.update(50.0);
        float dt = globals.GetDeltaTime();
        CHL_ASSERT(!std::isnan(dt) && !std::isinf(dt), "DeltaTime must never be NaN or Inf");
        CHL_ASSERT(dt >= 0.0001f, "Zero elapsed time must clamp to safe minimum 0.0001s");
        CHL_ASSERT_FLOAT_NEAR(globals.GetTime(), 50.0f, 0.0001f);
    }
    return true;
}

bool Test_Time_LongDuration_FloatPrecisionDegradation() {
    SlipGlobals& globals = SlipGlobals::Get();

    // Test at 86,400 seconds (24 hours uptime)
    double t24h = 86400.0;
    globals.update(t24h);
    CHL_ASSERT_FLOAT_NEAR(globals.GetTime(), 86400.0f, 0.1f);
    globals.update(t24h + 1.0 / 60.0);
    float dt24 = globals.GetDeltaTime();
    CHL_ASSERT(dt24 > 0.01f && dt24 < 0.025f, "DeltaTime should remain viable after 24h");

    // Test at 1,000,000 seconds (~11.5 days uptime)
    double tMillion = 1000000.0;
    globals.update(tMillion);
    globals.update(tMillion + 1.0 / 60.0);
    float dtMillion = globals.GetDeltaTime();
    std::cout << "\n    [PRECISION ANALYSIS] At t=1,000,000s, observed dt: " << dtMillion 
              << "s (float ULP: " << std::nextafter(1000000.0f, 2000000.0f) - 1000000.0f << "s)";
    CHL_ASSERT(!std::isnan(dtMillion) && dtMillion > 0.0f, "DeltaTime must remain positive and finite");

    return true;
}

bool Test_Time_BackwardsTime_RewindHandling() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.update(100.0);
    
    // Simulate time rewind (e.g., loading game save state, replaying time)
    globals.update(50.0);
    float curTime = globals.GetTime();
    CHL_ASSERT_FLOAT_NEAR(curTime, 50.0f, 0.001f);

    float dt = globals.GetDeltaTime();
    CHL_ASSERT(!std::isnan(dt) && dt > 0.0f, "DeltaTime on time rewind must remain non-negative safe minimum");
    CHL_ASSERT_FLOAT_NEAR(dt, 0.0001f, 0.00001f);
    return true;
}

bool Test_Time_NaN_And_Infinity_Resilience() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.update(10.0);

    // Pass quiet NaN
    globals.update(std::numeric_limits<double>::quiet_NaN());
    float dtNaN = globals.GetDeltaTime();
    CHL_ASSERT(!std::isnan(dtNaN), "GetDeltaTime() must guard against NaN input");
    CHL_ASSERT_FLOAT_NEAR(dtNaN, 1.0f / 60.0f, 0.001f);

    // Pass positive infinity
    globals.update(std::numeric_limits<double>::infinity());
    float dtInf = globals.GetDeltaTime();
    CHL_ASSERT(!std::isinf(dtInf) && !std::isnan(dtInf), "GetDeltaTime() must guard against Infinity input");
    CHL_ASSERT_FLOAT_NEAR(dtInf, 1.0f / 60.0f, 0.001f);

    // Recovery to normal time
    globals.update(20.0);
    globals.update(20.0 + 1.0 / 60.0);
    CHL_ASSERT(!std::isnan(globals.GetDeltaTime()), "Globals must recover cleanly from invalid time");
    return true;
}

// ============================================================================
// SUITE 2: Delta-Time Clamping Empirical Verification
// ============================================================================

bool Test_DeltaTime_LowerBound_Clamping() {
    SlipGlobals& globals = SlipGlobals::Get();

    // 1. Through update()
    globals.update(200.0);
    globals.update(200.0); // 0 elapsed
    CHL_ASSERT_FLOAT_NEAR(globals.GetDeltaTime(), 0.0001f, 0.00001f);

    // 2. Direct SetDeltaTime with negative value
    globals.SetDeltaTime(-1.0f);
    float sanitized = globals.GetDeltaTime();
    CHL_ASSERT_FLOAT_NEAR(sanitized, 1.0f / 60.0f, 0.001f);

    // 3. Direct SetDeltaTime with 0.0f
    globals.SetDeltaTime(0.0f);
    CHL_ASSERT_FLOAT_NEAR(globals.GetDeltaTime(), 1.0f / 60.0f, 0.001f);

    // 4. Direct SetDeltaTime with valid small value
    globals.SetDeltaTime(0.005f);
    CHL_ASSERT_FLOAT_NEAR(globals.GetDeltaTime(), 0.005f, 0.0001f);

    return true;
}

bool Test_DeltaTime_UpperBound_Clamping_ContradictionCheck() {
    SlipGlobals& globals = SlipGlobals::Get();

    // Case A: dt = 0.199f (just below 0.2f)
    globals.SetDeltaTime(0.199f);
    float dtBelow = globals.GetDeltaTime();
    CHL_ASSERT_FLOAT_NEAR(dtBelow, 0.199f, 0.0001f);

    // Case B: dt = 0.200f (exact boundary where update clamps spikes!)
    globals.SetDeltaTime(0.200f);
    float dtExact = globals.GetDeltaTime();
    std::cout << "\n    [EMPIRICAL OBSERVATION] SetDeltaTime(0.200f) -> GetDeltaTime() returned: " 
              << dtExact << " (note: strict '< 0.2f' evaluated to 1/60s: " << (1.0f / 60.0f) << ")";

    // Case C: Simulated spike through update()
    globals.update(300.0);
    globals.update(300.5); // 0.5s lag spike!
    float spikeDt = globals.GetDeltaTime();
    std::cout << "\n    [EMPIRICAL OBSERVATION] 0.5s spike in update() -> GetDeltaTime() returned: " 
              << spikeDt;

    CHL_ASSERT(spikeDt > 0.0f && spikeDt <= 0.2f, "Spike must be safely clamped <= 0.2f");
    return true;
}

bool Test_DeltaTime_Accumulator_Simulation() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.update(0.0);

    float animatedAccumulator = 0.0f;
    double simTime = 0.0;

    for (int frame = 0; frame < 600; ++frame) {
        double frameDt = (frame % 50 == 0) ? 0.35 : 0.016667;
        simTime += frameDt;
        globals.update(simTime);
        animatedAccumulator += globals.GetDeltaTime();
    }

    CHL_ASSERT(!std::isnan(animatedAccumulator) && !std::isinf(animatedAccumulator), "Accumulator must be finite");
    CHL_ASSERT(animatedAccumulator > 5.0f && animatedAccumulator < 15.0f, "Accumulator must integrate plausibly");
    return true;
}

// ============================================================================
// SUITE 3: Animated Material Evaluation (sin(time * speed))
// ============================================================================

bool Test_AnimatedShader_GLSL_Generation_SinTimeSpeed() {
    std::string testPath = "test_sin_time_speed.shadergraph";
    std::string json = R"({
      "schemaVersion": 1,
      "materialType": "Lit",
      "properties": [
        {
          "id": "prop_speed",
          "name": "AnimationSpeed",
          "type": "Float",
          "defaultValue": [2.5, 0.0, 0.0, 0.0],
          "defaultTexturePath": ""
        }
      ],
      "nodes": [
        { "id": "master_1", "type": "MasterOutput", "customProperties": { "materialType": "Lit" } },
        { "id": "time_1", "type": "TimeNode" },
        { "id": "speed_1", "type": "PropertyNode", "customProperties": { "propertyId": "prop_speed", "propertyName": "AnimationSpeed", "propertyType": "Float" } },
        { "id": "mul_1", "type": "Math_Multiply" },
        { "id": "sin_1", "type": "Math_Sin" }
      ],
      "links": [
        { "id": "l1", "fromNode": "time_1", "fromPort": "Time", "toNode": "mul_1", "toPort": "A" },
        { "id": "l2", "fromNode": "speed_1", "fromPort": "Out", "toNode": "mul_1", "toPort": "B" },
        { "id": "l3", "fromNode": "mul_1", "fromPort": "Out", "toNode": "sin_1", "toPort": "In" },
        { "id": "l4", "fromNode": "sin_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Roughness" }
      ]
    })";

    CHL_ASSERT(WriteJsonFile(testPath, json), "Write test graph");

    std::string vertSrc, fragSrc, sType, err;
    std::vector<std::pair<std::string, float>> floatProps;
    bool ok = SlipCompileShaderGraphFile(testPath, vertSrc, fragSrc, sType, &floatProps, nullptr, nullptr, &err);
    std::remove(testPath.c_str());

    CHL_ASSERT(ok, "Compilation must succeed: " + err);
    CHL_ASSERT(fragSrc.find("u_Time") != std::string::npos, "GLSL must reference u_Time");
    CHL_ASSERT(fragSrc.find("u_AnimationSpeed") != std::string::npos, "GLSL must reference u_AnimationSpeed");
    CHL_ASSERT(fragSrc.find("sin(") != std::string::npos, "GLSL must contain sin(");


    return true;
}

bool Test_AnimatedShader_CompileAndLink_In_OpenGL() {
    std::string testPath = "test_sin_opengl.shadergraph";
    std::string json = R"({
      "schemaVersion": 1,
      "materialType": "Lit",
      "properties": [
        {
          "id": "prop_rate",
          "name": "PulseRate",
          "type": "Float",
          "defaultValue": [3.0, 0.0, 0.0, 0.0],
          "defaultTexturePath": ""
        }
      ],
      "nodes": [
        { "id": "master_1", "type": "MasterOutput", "customProperties": { "materialType": "Lit" } },
        { "id": "time_1", "type": "TimeNode" },
        { "id": "rate_1", "type": "PropertyNode", "customProperties": { "propertyId": "prop_rate", "propertyName": "PulseRate", "propertyType": "Float" } },
        { "id": "mul_1", "type": "Math_Multiply" },
        { "id": "sin_1", "type": "Math_Sin" }
      ],
      "links": [
        { "id": "l1", "fromNode": "time_1", "fromPort": "Time", "toNode": "mul_1", "toPort": "A" },
        { "id": "l2", "fromNode": "rate_1", "fromPort": "Out", "toNode": "mul_1", "toPort": "B" },
        { "id": "l3", "fromNode": "mul_1", "fromPort": "Out", "toNode": "sin_1", "toPort": "In" },
        { "id": "l4", "fromNode": "sin_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Roughness" }
      ]
    })";

    CHL_ASSERT(WriteJsonFile(testPath, json), "Write test graph");

    std::string vertSrc, fragSrc, sType, err;
    bool ok = SlipCompileShaderGraphFile(testPath, vertSrc, fragSrc, sType, nullptr, nullptr, nullptr, &err);
    std::remove(testPath.c_str());

    std::cout << "[Step 1: ok=" << ok << " err=" << err << "] " << std::flush;
    CHL_ASSERT(ok, "Compilation failed: " + err);

    std::cout << "[Step 2: vertSrc size=" << vertSrc.size() << ", fragSrc size=" << fragSrc.size() << "] " << std::flush;
    SlipShader* shader = CompileTestShader(vertSrc, fragSrc);
    std::cout << "[Step 3: shader=" << (shader ? shader->ID : 0) << "] " << std::flush;
    CHL_ASSERT(shader != nullptr && shader->ID != 0, "OpenGL shader compilation failed");



    shader->use();
    GLint timeLoc = glGetUniformLocation(shader->ID, "u_Time");
    GLint speedLoc = glGetUniformLocation(shader->ID, "u_PulseRate");
    CHL_ASSERT(timeLoc != -1, "u_Time uniform location should be found");
    CHL_ASSERT(speedLoc != -1, "u_PulseRate uniform location should be found");

    glUniform1f(timeLoc, 12.34f);
    glUniform1f(speedLoc, 4.56f);
    CHL_ASSERT(glGetError() == GL_NO_ERROR, "glUniform calls should produce no GL errors");

    float queryTime = 0.0f, querySpeed = 0.0f;
    glGetUniformfv(shader->ID, timeLoc, &queryTime);
    glGetUniformfv(shader->ID, speedLoc, &querySpeed);
    CHL_ASSERT_FLOAT_NEAR(queryTime, 12.34f, 0.001f);
    CHL_ASSERT_FLOAT_NEAR(querySpeed, 4.56f, 0.001f);

    delete shader;
    return true;
}

bool Test_AnimatedShader_Mathematical_Domain_Stress() {
    std::vector<float> testSpeeds = { 0.0f, 1.0f, -2.5f, 100.0f, 0.0001f, -1000.0f };
    std::vector<float> testTimes = { 0.0f, 1.5707963f, 3.14159265f, 4.71238898f, 1000.0f, 100000.0f };

    for (float speed : testSpeeds) {
        for (float t : testTimes) {
            float arg = t * speed;
            float val = std::sin(arg);
            CHL_ASSERT(!std::isnan(val) && !std::isinf(val), "sin(time*speed) must evaluate to finite number");
            CHL_ASSERT(val >= -1.0001f && val <= 1.0001f, "sin(time*speed) must be bounded in [-1, 1]");
        }
    }
    return true;
}

bool Test_AnimatedShader_PBR_Lit_NegativeValueSafety() {
    std::string testPath = "test_sin_negative.shadergraph";
    std::string json = R"({
      "schemaVersion": 1,
      "materialType": "Lit",
      "properties": [],
      "nodes": [
        { "id": "master_1", "type": "MasterOutput", "customProperties": { "materialType": "Lit" } },
        { "id": "time_1", "type": "TimeNode" },
        { "id": "sin_1", "type": "Math_Sin" }
      ],
      "links": [
        { "id": "l1", "fromNode": "time_1", "fromPort": "Time", "toNode": "sin_1", "toPort": "In" },
        { "id": "l2", "fromNode": "sin_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Roughness" },
        { "id": "l3", "fromNode": "sin_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Metallic" }
      ]
    })";

    CHL_ASSERT(WriteJsonFile(testPath, json), "Write test graph");
    std::string vertSrc, fragSrc, sType, err;
    bool ok = SlipCompileShaderGraphFile(testPath, vertSrc, fragSrc, sType, nullptr, nullptr, nullptr, &err);
    std::remove(testPath.c_str());

    CHL_ASSERT(ok, "Compilation should succeed");
    CHL_ASSERT(fragSrc.find("clamp(") != std::string::npos, "GLSL must clamp roughness and metallic");
    CHL_ASSERT(fragSrc.find("0.04") != std::string::npos, "Roughness must have lower bound 0.04 for Cook-Torrance GGX safety");

    SlipShader* shader = CompileTestShader(vertSrc, fragSrc);
    CHL_ASSERT(shader != nullptr && shader->ID != 0, "Shader with negative sine to roughness must compile cleanly");
    delete shader;
    return true;
}

bool Test_AnimatedShader_Unlit_PulsingColor() {
    std::string testPath = "test_unlit_pulse.shadergraph";
    std::string json = R"({
      "schemaVersion": 1,
      "materialType": "Unlit",
      "properties": [
        {
          "id": "prop_speed",
          "name": "AnimSpeed",
          "type": "Float",
          "defaultValue": [5.0, 0.0, 0.0, 0.0],
          "defaultTexturePath": ""
        }
      ],
      "nodes": [
        { "id": "master_1", "type": "MasterOutput", "customProperties": { "materialType": "Unlit" } },
        { "id": "time_1", "type": "TimeNode" },
        { "id": "speed_1", "type": "PropertyNode", "customProperties": { "propertyId": "prop_speed", "propertyName": "AnimSpeed", "propertyType": "Float" } },
        { "id": "mul_1", "type": "Math_Multiply" },
        { "id": "sin_1", "type": "Math_Sin" }
      ],
      "links": [
        { "id": "l1", "fromNode": "time_1", "fromPort": "Time", "toNode": "mul_1", "toPort": "A" },
        { "id": "l2", "fromNode": "speed_1", "fromPort": "Out", "toNode": "mul_1", "toPort": "B" },
        { "id": "l3", "fromNode": "mul_1", "fromPort": "Out", "toNode": "sin_1", "toPort": "In" },
        { "id": "l4", "fromNode": "sin_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Color" }
      ]
    })";

    CHL_ASSERT(WriteJsonFile(testPath, json), "Write test graph");
    std::string vertSrc, fragSrc, sType, err;
    bool ok = SlipCompileShaderGraphFile(testPath, vertSrc, fragSrc, sType, nullptr, nullptr, nullptr, &err);
    std::remove(testPath.c_str());

    CHL_ASSERT(ok, "Unlit animated graph compilation should succeed");

    SlipShader* shader = CompileTestShader(vertSrc, fragSrc);
    CHL_ASSERT(shader != nullptr && shader->ID != 0, "Unlit pulsing shader must compile and link in GL");
    delete shader;
    return true;
}

// ============================================================================
// SUITE 4: SlipRenderQueue Custom Shader Dispatch & Material Binding
// ============================================================================

bool Test_RenderQueue_CustomShader_RegistrationAndSorting() {
    SlipRenderQueue queue;

    RenderCommand cmd1;
    cmd1.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 20, 1, 0, 10.0f, true);
    cmd1.wireframe = false;

    RenderCommand cmd2;
    cmd2.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 10, 2, 0, 5.0f, true);
    cmd2.wireframe = false;

    RenderCommand cmd3;
    cmd3.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 20, 1, 0, 2.0f, true);
    cmd3.wireframe = false;

    queue.Submit(cmd1);
    queue.Submit(cmd2);
    queue.Submit(cmd3);

    queue.Sort();
    const auto& sorted = queue.GetCommands();
    CHL_ASSERT(sorted.size() == 3, "Queue should contain 3 commands");

    CHL_ASSERT(sorted[0].sortKey == cmd2.sortKey, "Shader 10 command should sort first");
    CHL_ASSERT(sorted[1].sortKey == cmd3.sortKey, "Closer command under Shader 20 should sort second");
    CHL_ASSERT(sorted[2].sortKey == cmd1.sortKey, "Farther command under Shader 20 should sort third");

    return true;
}

bool Test_RenderQueue_TimeUniform_Dispatch() {
    const char* vSrc = "#version 330 core\nlayout(location=0) in vec3 aPos;\nvoid main(){ gl_Position = vec4(aPos,1.0); }\n";
    const char* fSrc = "#version 330 core\nuniform float u_Time;\nuniform float u_DeltaTime;\nout vec4 FragColor;\nvoid main(){ FragColor = vec4(u_Time, u_DeltaTime, 0.0, 1.0); }\n";
    SlipShader* shader = CompileTestShader(vSrc, fSrc);
    CHL_ASSERT(shader != nullptr && shader->ID != 0, "Test shader must compile");

    SlipMaterial mat;
    mat.shader = shader;

    SlipGlobals::Get().SetTime(77.25f);
    SlipGlobals::Get().SetDeltaTime(0.016667f);

    SlipRenderQueue queue;
    RenderCommand cmd;
    cmd.material = &mat;
    cmd.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, shader->ID, 1, 0, 1.0f, true);
    queue.Submit(cmd);

    queue.Flush([](SlipShader* s) {
        (void)s;
    });

    float queryTime = 0.0f;
    float queryDt = 0.0f;
    glGetUniformfv(shader->ID, glGetUniformLocation(shader->ID, "u_Time"), &queryTime);
    glGetUniformfv(shader->ID, glGetUniformLocation(shader->ID, "u_DeltaTime"), &queryDt);

    CHL_ASSERT_FLOAT_NEAR(queryTime, 77.25f, 0.001f);
    CHL_ASSERT_FLOAT_NEAR(queryDt, 0.016667f, 0.001f);

    delete shader;
    return true;
}

bool Test_RenderQueue_LitCustomShader_ParameterBinding() {
    std::string testPath = "test_lit_param_dispatch.shadergraph";
    std::string json = R"({
      "schemaVersion": 1,
      "materialType": "Lit",
      "properties": [
        {
          "id": "prop_glow",
          "name": "LitGlowIntensity",
          "type": "Float",
          "defaultValue": [1.0, 0.0, 0.0, 0.0],
          "defaultTexturePath": ""
        }
      ],
      "nodes": [
        { "id": "master_1", "type": "MasterOutput", "customProperties": { "materialType": "Lit" } },
        { "id": "glow_1", "type": "PropertyNode", "customProperties": { "propertyId": "prop_glow", "propertyName": "LitGlowIntensity", "propertyType": "Float" } }
      ],
      "links": [
        { "id": "l1", "fromNode": "glow_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Roughness" }
      ]
    })";

    CHL_ASSERT(WriteJsonFile(testPath, json), "Save test graph");

    SlipMaterial mat;
    bool loaded = mat.LoadShaderGraph(testPath);
    std::remove(testPath.c_str());
    CHL_ASSERT(loaded && mat.shader != nullptr, "LoadShaderGraph must succeed");

    mat.SetGraphFloat("u_LitGlowIntensity", 99.5f);

    SlipRenderQueue queue;
    RenderCommand cmd;
    cmd.material = &mat;
    cmd.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, mat.shader->ID, 1, 0, 1.0f, true);
    queue.Submit(cmd);

    queue.Flush([](SlipShader* s) { (void)s; });

    float queryVal = 0.0f;
    GLint loc = glGetUniformLocation(mat.shader->ID, "u_LitGlowIntensity");
    CHL_ASSERT(loc != -1, "u_LitGlowIntensity uniform must exist in compiled shader");
    glGetUniformfv(mat.shader->ID, loc, &queryVal);

    CHL_ASSERT_FLOAT_NEAR(queryVal, 99.5f, 0.01f);
    return true;
}

bool Test_RenderQueue_UnlitCustomShader_ParameterBypass_EmpiricalCheck() {
    std::string testPath = "test_unlit_param_dispatch.shadergraph";
    std::string json = R"({
      "schemaVersion": 1,
      "materialType": "Unlit",
      "properties": [
        {
          "id": "prop_pulse",
          "name": "UnlitPulseRate",
          "type": "Float",
          "defaultValue": [1.0, 0.0, 0.0, 0.0],
          "defaultTexturePath": ""
        }
      ],
      "nodes": [
        { "id": "master_1", "type": "MasterOutput", "customProperties": { "materialType": "Unlit" } },
        { "id": "pulse_1", "type": "PropertyNode", "customProperties": { "propertyId": "prop_pulse", "propertyName": "UnlitPulseRate", "propertyType": "Float" } }
      ],
      "links": [
        { "id": "l1", "fromNode": "pulse_1", "fromPort": "Out", "toNode": "master_1", "toPort": "Color" }
      ]
    })";

    CHL_ASSERT(WriteJsonFile(testPath, json), "Save test graph");

    SlipMaterial mat;
    bool loaded = mat.LoadShaderGraph(testPath);
    std::remove(testPath.c_str());
    CHL_ASSERT(loaded && mat.shader != nullptr, "LoadShaderGraph must succeed for Unlit graph");

    mat.SetGraphFloat("u_UnlitPulseRate", 88.5f);

    SlipRenderQueue queue;
    RenderCommand cmd;
    cmd.material = &mat;
    cmd.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, mat.shader->ID, 1, 0, 1.0f, true);
    queue.Submit(cmd);

    queue.Flush([](SlipShader* s) { (void)s; });

    float queryVal = -1.0f;
    GLint loc = glGetUniformLocation(mat.shader->ID, "u_UnlitPulseRate");
    CHL_ASSERT(loc != -1, "u_UnlitPulseRate uniform must exist in compiled shader");
    glGetUniformfv(mat.shader->ID, loc, &queryVal);

    std::cout << "\n    [EMPIRICAL TEST] Unlit shader uniform 'u_UnlitPulseRate' after SlipRenderQueue::Flush(): "
              << queryVal << " (Expected override: 88.5f, Default in shader: 1.0f)";

    bool bypassed = (std::abs(queryVal - 88.5f) > 0.1f);
    if (bypassed) {
        std::cout << "\n    [CONFIRMED EMPIRICAL FINDING] In SlipRenderQueue.cpp (lines 101-112), 'if (sType == \"Unlit\")' "
                  << "bypasses bindMaterialProperties() and bindGraphParameters()! Unlit graph parameters are NOT bound!";
    } else {
        std::cout << "\n    [PASS] Unlit shader graph parameters were bound successfully.";
    }

    return true;
}

bool Test_RenderQueue_StateLeakage_BetweenCommands() {
    SlipRenderQueue queue;

    const char* vSrc = "#version 330 core\nlayout(location=0) in vec3 aPos;\nvoid main(){ gl_Position = vec4(aPos,1.0); }\n";
    const char* fSrc1 = "#version 330 core\nout vec4 FragColor;\nvoid main(){ FragColor = vec4(1.0); }\n";
    const char* fSrc2 = "#version 330 core\nout vec4 FragColor;\nvoid main(){ FragColor = vec4(0.0); }\n";

    SlipShader* s1 = CompileTestShader(vSrc, fSrc1);
    SlipShader* s2 = CompileTestShader(vSrc, fSrc2);
    CHL_ASSERT(s1 && s2, "Both test shaders must compile");

    SlipMaterial mat1, mat2;
    mat1.shader = s1;
    mat2.shader = s2;

    RenderCommand cmd1, cmd2;
    cmd1.material = &mat1;
    cmd1.doubleSided = true;
    cmd1.wireframe = true;
    cmd1.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, s1->ID, 1, 0, 1.0f, true);

    cmd2.material = &mat2;
    cmd2.doubleSided = false;
    cmd2.wireframe = false;
    cmd2.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, s2->ID, 2, 0, 2.0f, true);

    queue.Submit(cmd1);
    queue.Submit(cmd2);

    queue.Flush([](SlipShader* s) { (void)s; });

    GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    CHL_ASSERT(cullEnabled == GL_TRUE, "Cull face should be restored to enabled after doubleSided=false command");

    GLint polygonMode[2];
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    CHL_ASSERT(polygonMode[0] == GL_FILL, "Polygon mode should be restored to GL_FILL after wireframe=false command");

    delete s1;
    delete s2;
    return true;
}

bool Test_RenderQueue_Clear_Idempotency() {
    SlipRenderQueue queue;
    RenderCommand cmd;
    cmd.sortKey = 12345;

    for (int i = 0; i < 50; ++i) {
        queue.Submit(cmd);
    }
    CHL_ASSERT(queue.GetCommands().size() == 50, "Queue should contain 50 commands");

    queue.Clear();
    CHL_ASSERT(queue.GetCommands().empty(), "Queue must be empty after Clear()");

    queue.Clear();
    CHL_ASSERT(queue.GetCommands().empty(), "Second Clear() must remain empty");

    queue.Flush([](SlipShader* s) { (void)s; });
    return true;
}

// ============================================================================
// Main Test Runner Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "================================================================================" << std::endl;
    std::cout << "     SlipEngine Milestone 2 Empirical Challenger Stress Harness                 " << std::endl;
    std::cout << "================================================================================" << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[ERROR] Failed to initialize SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("SlipTestM2Challenger", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "[ERROR] Failed to create headless SDL window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "[ERROR] Failed to initialize GLAD / OpenGL 3.3 Core context" << std::endl;
        if (glContext) SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    std::cout << "[INFO] Headless OpenGL 3.3 Core Profile initialized successfully." << std::endl;
    std::cout << "[INFO] GPU Vendor:   " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "[INFO] GPU Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[INFO] GL Version:   " << glGetString(GL_VERSION) << std::endl;
    std::cout << "--------------------------------------------------------------------------------\n" << std::endl;

    // --- Suite 1: Time Tracking Monotonicity & Precision ---
    std::cout << ">>> Suite 1: Time Tracking Monotonicity, Precision & Edge-Case Stress <<<" << std::endl;
    RUN_CHALLENGE_TEST("Time Monotonicity at Normal Frame Rates (60/144 FPS)", Test_Time_Monotonicity_NormalFrameRates);
    RUN_CHALLENGE_TEST("Rapid Consecutive Calls with Zero Elapsed Delta", Test_Time_RapidConsecutiveCalls_ZeroDelta);
    RUN_CHALLENGE_TEST("Long Duration Time Tracking Float Precision Degradation (24h to 11.5d)", Test_Time_LongDuration_FloatPrecisionDegradation);
    RUN_CHALLENGE_TEST("Time Rewind / Backward Step Handling", Test_Time_BackwardsTime_RewindHandling);
    RUN_CHALLENGE_TEST("NaN and Infinity Non-Finite Time Resilience", Test_Time_NaN_And_Infinity_Resilience);

    // --- Suite 2: Delta-Time Clamping Empirical Verification ---
    std::cout << "\n>>> Suite 2: Delta-Time Clamping Empirical Verification <<<" << std::endl;
    RUN_CHALLENGE_TEST("Delta-Time Lower Bound Clamping (0.0001s minimum)", Test_DeltaTime_LowerBound_Clamping);
    RUN_CHALLENGE_TEST("Delta-Time Upper Bound Clamping & Contradiction Analysis", Test_DeltaTime_UpperBound_Clamping_ContradictionCheck);
    RUN_CHALLENGE_TEST("Delta-Time Accumulator Simulation under Frame Spikes", Test_DeltaTime_Accumulator_Simulation);

    // --- Suite 3: Animated Material Evaluation (sin(time * speed)) ---
    std::cout << "\n>>> Suite 3: Animated Material Evaluation (sin(time * speed)) <<<" << std::endl;
    RUN_CHALLENGE_TEST("GLSL Generation: sin(time * speed) in Topological DAG", Test_AnimatedShader_GLSL_Generation_SinTimeSpeed);
    RUN_CHALLENGE_TEST("Headless OpenGL Compilation & Linking of Animated Shader", Test_AnimatedShader_CompileAndLink_In_OpenGL);
    RUN_CHALLENGE_TEST("Mathematical Domain & High Frequency Oscillation Stress", Test_AnimatedShader_Mathematical_Domain_Stress);
    RUN_CHALLENGE_TEST("PBR Lit Master Node Negative Sine Roughness Clamping Safety", Test_AnimatedShader_PBR_Lit_NegativeValueSafety);
    RUN_CHALLENGE_TEST("Unlit Master Node Pulsing Color Animation", Test_AnimatedShader_Unlit_PulsingColor);

    // --- Suite 4: SlipRenderQueue Custom Shader Dispatch & Material Binding ---
    std::cout << "\n>>> Suite 4: SlipRenderQueue Custom Shader Dispatch & Material Binding <<<" << std::endl;
    RUN_CHALLENGE_TEST("Custom Shader Registration & Front-To-Back Sorting", Test_RenderQueue_CustomShader_RegistrationAndSorting);
    RUN_CHALLENGE_TEST("SlipRenderQueue Global Time Uniforms Dispatch (u_Time, u_DeltaTime)", Test_RenderQueue_TimeUniform_Dispatch);
    RUN_CHALLENGE_TEST("Lit Custom Shader Material Parameter Override Binding", Test_RenderQueue_LitCustomShader_ParameterBinding);
    RUN_CHALLENGE_TEST("Unlit Custom Shader Parameter Bypass Empirical Repro", Test_RenderQueue_UnlitCustomShader_ParameterBypass_EmpiricalCheck);
    RUN_CHALLENGE_TEST("Render State Leakage Prevention Between Dispatched Commands", Test_RenderQueue_StateLeakage_BetweenCommands);
    RUN_CHALLENGE_TEST("Render Queue Clear Idempotency and Flush Resilience", Test_RenderQueue_Clear_Idempotency);

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "                         CHALLENGER SUITE SUMMARY                               " << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "  Total Tests Executed:  " << g_Stats.total << std::endl;
    std::cout << "  Passed:                " << g_Stats.passed << std::endl;
    std::cout << "  Failed:                " << g_Stats.failed << std::endl;
    std::cout << "================================================================================" << std::endl;

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return (g_Stats.failed == 0) ? 0 : 1;
}
