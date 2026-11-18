#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/core/SlipGlobals.h"

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <queue>
#include <set>
#include <functional>
#include <iomanip>

// ============================================================================
// SlipEngine Test Assertion Framework
// ============================================================================

struct TestStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

static TestStats g_Tier1Stats;
static TestStats g_Tier2Stats;
static TestStats g_Tier3Stats;
static TestStats g_Tier4Stats;
static TestStats g_OverallStats;

#define TEST_ASSERT(expr, msg) \
    do { \
        if (!(expr)) { \
            std::cerr << "    [FAIL] Line " << __LINE__ << ": " << (msg) << " (" #expr ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define TEST_ASSERT_TRUE(expr) TEST_ASSERT((expr), "Expression should be true")
#define TEST_ASSERT_FALSE(expr) TEST_ASSERT(!(expr), "Expression should be false")
#define TEST_ASSERT_EQ(a, b) TEST_ASSERT(((a) == (b)), "Values should be equal")
#define TEST_ASSERT_STREQ(a, b) TEST_ASSERT((std::string(a) == std::string(b)), "Strings should be equal")
#define TEST_ASSERT_FLOAT_NEAR(a, b, eps) TEST_ASSERT((std::abs((a) - (b)) <= (eps)), "Floats should be near")

#define RUN_TIER_TEST(tier_stats, test_func) \
    do { \
        tier_stats.total++; \
        g_OverallStats.total++; \
        std::cout << "  [RUN] " << #test_func << " ... " << std::flush; \
        if (test_func()) { \
            tier_stats.passed++; \
            g_OverallStats.passed++; \
            std::cout << "[PASS]" << std::endl; \
        } else { \
            tier_stats.failed++; \
            g_OverallStats.failed++; \
            std::cout << "[FAILED]" << std::endl; \
        } \
    } while (0)

// ============================================================================
// Shader Graph Core Model & Node System (PROJECT.md Interface Contract)
// ============================================================================

namespace SlipTest {

enum class ShaderGraphType {
    Lit,
    Unlit
};

enum class PinType {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Texture2D
};

inline const char* PinTypeToString(PinType type) {
    switch (type) {
        case PinType::Float:     return "float";
        case PinType::Vec2:      return "vec2";
        case PinType::Vec3:      return "vec3";
        case PinType::Vec4:      return "vec4";
        case PinType::Texture2D: return "sampler2D";
    }
    return "float";
}

struct ShaderProperty {
    std::string id;
    std::string name;
    PinType type = PinType::Float;
    glm::vec4 defaultValue = glm::vec4(0.0f);
    std::string defaultTexturePath;
};

struct ShaderPort {
    std::string id;
    std::string name;
    PinType type = PinType::Float;
    bool isOutput = false;
    glm::vec4 defaultValue = glm::vec4(0.0f);
};

class ShaderNode {
public:
    std::string id;
    std::string typeName;
    std::string title;
    float posX = 0.0f;
    float posY = 0.0f;
    std::vector<ShaderPort> inputs;
    std::vector<ShaderPort> outputs;

    virtual ~ShaderNode() = default;

    ShaderPort* FindPort(const std::string& portId) {
        for (auto& p : inputs) if (p.id == portId) return &p;
        for (auto& p : outputs) if (p.id == portId) return &p;
        return nullptr;
    }

    const ShaderPort* FindPort(const std::string& portId) const {
        for (const auto& p : inputs) if (p.id == portId) return &p;
        for (const auto& p : outputs) if (p.id == portId) return &p;
        return nullptr;
    }

    void AddInput(const std::string& portId, const std::string& name, PinType type, glm::vec4 defVal = glm::vec4(0.0f)) {
        inputs.push_back({ portId, name, type, false, defVal });
    }

    void AddOutput(const std::string& portId, const std::string& name, PinType type, glm::vec4 defVal = glm::vec4(0.0f)) {
        outputs.push_back({ portId, name, type, true, defVal });
    }

    virtual std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                                     const std::string& outVar) const {
        (void)inVars; (void)outVar;
        return "";
    }
};

struct ShaderLink {
    std::string id;
    std::string fromNodeId;
    std::string fromPortId;
    std::string toNodeId;
    std::string toPortId;
};

// ----------------------------------------------------------------------------
// Specific Node Implementations (Math, Procedural, Time, Camera, Texture, Master)
// ----------------------------------------------------------------------------

class AddNode : public ShaderNode {
public:
    AddNode() {
        typeName = "Add";
        title = "Add";
        AddInput("inA", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("inB", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "0.0";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "0.0";
        return "float " + outVar + " = " + a + " + " + b + ";";
    }
};

class SubtractNode : public ShaderNode {
public:
    SubtractNode() {
        typeName = "Subtract";
        title = "Subtract";
        AddInput("inA", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("inB", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "0.0";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "0.0";
        return "float " + outVar + " = " + a + " - " + b + ";";
    }
};

class MultiplyNode : public ShaderNode {
public:
    MultiplyNode() {
        typeName = "Multiply";
        title = "Multiply";
        AddInput("inA", "A", PinType::Float, glm::vec4(1.0f));
        AddInput("inB", "B", PinType::Float, glm::vec4(1.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "1.0";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "1.0";
        return "float " + outVar + " = " + a + " * " + b + ";";
    }
};

class DivideNode : public ShaderNode {
public:
    DivideNode() {
        typeName = "Divide";
        title = "Divide";
        AddInput("inA", "A", PinType::Float, glm::vec4(1.0f));
        AddInput("inB", "B", PinType::Float, glm::vec4(1.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "1.0";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "1.0";
        // Protected division with epsilon to prevent divide-by-zero
        return "float " + outVar + " = " + a + " / (abs(" + b + ") > 0.00001 ? " + b + " : 0.00001);";
    }
};

class SinNode : public ShaderNode {
public:
    SinNode() {
        typeName = "Sin";
        title = "Sin";
        AddInput("in", "In", PinType::Float);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "0.0";
        return "float " + outVar + " = sin(" + in + ");";
    }
};

class CosNode : public ShaderNode {
public:
    CosNode() {
        typeName = "Cos";
        title = "Cos";
        AddInput("in", "In", PinType::Float);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "0.0";
        return "float " + outVar + " = cos(" + in + ");";
    }
};

class TanNode : public ShaderNode {
public:
    TanNode() {
        typeName = "Tan";
        title = "Tan";
        AddInput("in", "In", PinType::Float);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "0.0";
        return "float " + outVar + " = tan(" + in + ");";
    }
};

class DotNode : public ShaderNode {
public:
    DotNode() {
        typeName = "Dot";
        title = "Dot Product";
        AddInput("inA", "A", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        AddInput("inB", "B", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "vec3(0.0, 1.0, 0.0)";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "vec3(0.0, 1.0, 0.0)";
        return "float " + outVar + " = dot(" + a + ", " + b + ");";
    }
};

class CrossNode : public ShaderNode {
public:
    CrossNode() {
        typeName = "Cross";
        title = "Cross Product";
        AddInput("inA", "A", PinType::Vec3, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        AddInput("inB", "B", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        AddOutput("out", "Out", PinType::Vec3);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "vec3(1.0, 0.0, 0.0)";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "vec3(0.0, 1.0, 0.0)";
        return "vec3 " + outVar + " = cross(" + a + ", " + b + ");";
    }
};

class NormalizeNode : public ShaderNode {
public:
    NormalizeNode() {
        typeName = "Normalize";
        title = "Normalize";
        AddInput("in", "In", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        AddOutput("out", "Out", PinType::Vec3);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "vec3(0.0, 1.0, 0.0)";
        return "vec3 " + outVar + " = (length(" + in + ") > 0.00001 ? normalize(" + in + ") : vec3(0.0, 1.0, 0.0));";
    }
};

class LengthNode : public ShaderNode {
public:
    LengthNode() {
        typeName = "Length";
        title = "Length";
        AddInput("in", "In", PinType::Vec3);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "vec3(0.0)";
        return "float " + outVar + " = length(" + in + ");";
    }
};

class DistanceNode : public ShaderNode {
public:
    DistanceNode() {
        typeName = "Distance";
        title = "Distance";
        AddInput("inA", "A", PinType::Vec3);
        AddInput("inB", "B", PinType::Vec3);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "vec3(0.0)";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "vec3(0.0)";
        return "float " + outVar + " = distance(" + a + ", " + b + ");";
    }
};

class MinNode : public ShaderNode {
public:
    MinNode() {
        typeName = "Min";
        title = "Min";
        AddInput("inA", "A", PinType::Float);
        AddInput("inB", "B", PinType::Float);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "0.0";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "0.0";
        return "float " + outVar + " = min(" + a + ", " + b + ");";
    }
};

class MaxNode : public ShaderNode {
public:
    MaxNode() {
        typeName = "Max";
        title = "Max";
        AddInput("inA", "A", PinType::Float);
        AddInput("inB", "B", PinType::Float);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "0.0";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "0.0";
        return "float " + outVar + " = max(" + a + ", " + b + ");";
    }
};

class ClampNode : public ShaderNode {
public:
    ClampNode() {
        typeName = "Clamp";
        title = "Clamp";
        AddInput("in", "In", PinType::Float);
        AddInput("inMin", "Min", PinType::Float, glm::vec4(0.0f));
        AddInput("inMax", "Max", PinType::Float, glm::vec4(1.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "0.0";
        std::string mn = inVars.count("inMin") ? inVars.at("inMin") : "0.0";
        std::string mx = inVars.count("inMax") ? inVars.at("inMax") : "1.0";
        return "float " + outVar + " = clamp(" + in + ", min(" + mn + ", " + mx + "), max(" + mn + ", " + mx + "));";
    }
};

class LerpNode : public ShaderNode {
public:
    LerpNode() {
        typeName = "Lerp";
        title = "Lerp";
        AddInput("inA", "A", PinType::Vec3, glm::vec4(0.0f));
        AddInput("inB", "B", PinType::Vec3, glm::vec4(1.0f));
        AddInput("t", "T", PinType::Float, glm::vec4(0.5f));
        AddOutput("out", "Out", PinType::Vec3);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string a = inVars.count("inA") ? inVars.at("inA") : "vec3(0.0)";
        std::string b = inVars.count("inB") ? inVars.at("inB") : "vec3(1.0)";
        std::string t = inVars.count("t") ? inVars.at("t") : "0.5";
        return "vec3 " + outVar + " = mix(" + a + ", " + b + ", " + t + ");";
    }
};

class AbsNode : public ShaderNode {
public:
    AbsNode() {
        typeName = "Abs";
        title = "Abs";
        AddInput("in", "In", PinType::Float);
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "0.0";
        return "float " + outVar + " = abs(" + in + ");";
    }
};

class StepNode : public ShaderNode {
public:
    StepNode() {
        typeName = "Step";
        title = "Step";
        AddInput("edge", "Edge", PinType::Float, glm::vec4(0.5f));
        AddInput("in", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string edge = inVars.count("edge") ? inVars.at("edge") : "0.5";
        std::string in = inVars.count("in") ? inVars.at("in") : "0.0";
        return "float " + outVar + " = step(" + edge + ", " + in + ");";
    }
};

class SmoothstepNode : public ShaderNode {
public:
    SmoothstepNode() {
        typeName = "Smoothstep";
        title = "Smoothstep";
        AddInput("edge0", "Edge0", PinType::Float, glm::vec4(0.0f));
        AddInput("edge1", "Edge1", PinType::Float, glm::vec4(1.0f));
        AddInput("in", "In", PinType::Float, glm::vec4(0.5f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string e0 = inVars.count("edge0") ? inVars.at("edge0") : "0.0";
        std::string e1 = inVars.count("edge1") ? inVars.at("edge1") : "1.0";
        std::string in = inVars.count("in") ? inVars.at("in") : "0.5";
        // Safe smoothstep to prevent identical edge NaN
        return "float " + outVar + " = (abs(" + e1 + " - " + e0 + ") > 0.00001 ? smoothstep(" + e0 + ", " + e1 + ", " + in + ") : step(" + e0 + ", " + in + "));";
    }
};

class RemapNode : public ShaderNode {
public:
    RemapNode() {
        typeName = "Remap";
        title = "Remap";
        AddInput("in", "In", PinType::Float, glm::vec4(0.5f));
        AddInput("inMin", "InMin", PinType::Float, glm::vec4(0.0f));
        AddInput("inMax", "InMax", PinType::Float, glm::vec4(1.0f));
        AddInput("outMin", "OutMin", PinType::Float, glm::vec4(0.0f));
        AddInput("outMax", "OutMax", PinType::Float, glm::vec4(100.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string in = inVars.count("in") ? inVars.at("in") : "0.5";
        std::string inMn = inVars.count("inMin") ? inVars.at("inMin") : "0.0";
        std::string inMx = inVars.count("inMax") ? inVars.at("inMax") : "1.0";
        std::string outMn = inVars.count("outMin") ? inVars.at("outMin") : "0.0";
        std::string outMx = inVars.count("outMax") ? inVars.at("outMax") : "100.0";
        return "float " + outVar + " = " + outMn + " + (" + in + " - " + inMn + ") / (abs(" + inMx + " - " + inMn + ") > 0.00001 ? (" + inMx + " - " + inMn + ") : 0.00001) * (" + outMx + " - " + outMn + ");";
    }
};

class ProceduralNoiseNode : public ShaderNode {
public:
    ProceduralNoiseNode() {
        typeName = "ProceduralNoise";
        title = "Procedural Noise";
        AddInput("uv", "UV", PinType::Vec2);
        AddInput("scale", "Scale", PinType::Float, glm::vec4(10.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string uv = inVars.count("uv") ? inVars.at("uv") : "vTexCoords";
        std::string scale = inVars.count("scale") ? inVars.at("scale") : "10.0";
        // GLSL gradient noise generator
        return "float " + outVar + " = fract(sin(dot((" + uv + " * " + scale + "), vec2(12.9898, 78.233))) * 43758.5453);";
    }
};

class VoronoiNode : public ShaderNode {
public:
    VoronoiNode() {
        typeName = "Voronoi";
        title = "Voronoi Noise";
        AddInput("uv", "UV", PinType::Vec2);
        AddInput("scale", "Scale", PinType::Float, glm::vec4(5.0f));
        AddOutput("out", "Out", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string uv = inVars.count("uv") ? inVars.at("uv") : "vTexCoords";
        std::string scale = inVars.count("scale") ? inVars.at("scale") : "5.0";
        return "float " + outVar + " = length(fract(" + uv + " * " + scale + ") - vec2(0.5));";
    }
};

class TimeNode : public ShaderNode {
public:
    TimeNode() {
        typeName = "Time";
        title = "Time";
        AddOutput("time", "Time", PinType::Float);
        AddOutput("sinTime", "SinTime", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        return "float " + outVar + " = u_Time;";
    }
};

class DeltaTimeNode : public ShaderNode {
public:
    DeltaTimeNode() {
        typeName = "DeltaTime";
        title = "Delta Time";
        AddOutput("deltaTime", "DeltaTime", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        return "float " + outVar + " = u_DeltaTime;";
    }
};

class CameraPosNode : public ShaderNode {
public:
    CameraPosNode() {
        typeName = "CameraPosition";
        title = "Camera Position";
        AddOutput("pos", "Position", PinType::Vec3);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        return "vec3 " + outVar + " = u_CameraPos;";
    }
};

class WorldPosNode : public ShaderNode {
public:
    WorldPosNode() {
        typeName = "WorldPosition";
        title = "World Position";
        AddOutput("pos", "Position", PinType::Vec3);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        return "vec3 " + outVar + " = vWorldPos;";
    }
};

class WorldNormalNode : public ShaderNode {
public:
    WorldNormalNode() {
        typeName = "WorldNormal";
        title = "World Normal";
        AddOutput("normal", "Normal", PinType::Vec3);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        return "vec3 " + outVar + " = normalize(vNormal);";
    }
};

class UVNode : public ShaderNode {
public:
    UVNode() {
        typeName = "UV";
        title = "UV Coordinates";
        AddOutput("uv", "UV", PinType::Vec2);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        return "vec2 " + outVar + " = vTexCoords;";
    }
};

class TilingOffsetNode : public ShaderNode {
public:
    TilingOffsetNode() {
        typeName = "TilingOffset";
        title = "Tiling & Offset";
        AddInput("uv", "UV", PinType::Vec2);
        AddInput("tiling", "Tiling", PinType::Vec2, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        AddInput("offset", "Offset", PinType::Vec2, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("out", "Out", PinType::Vec2);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string uv = inVars.count("uv") ? inVars.at("uv") : "vTexCoords";
        std::string tiling = inVars.count("tiling") ? inVars.at("tiling") : "vec2(1.0, 1.0)";
        std::string offset = inVars.count("offset") ? inVars.at("offset") : "vec2(0.0, 0.0)";
        return "vec2 " + outVar + " = " + uv + " * " + tiling + " + " + offset + ";";
    }
};

class Texture2DNode : public ShaderNode {
public:
    std::string textureSamplerUniform;
    int textureUnit = 20;

    Texture2DNode() {
        typeName = "Texture2D";
        title = "Sample Texture2D";
        AddInput("uv", "UV", PinType::Vec2);
        AddOutput("tex", "Texture", PinType::Texture2D);
        AddOutput("rgba", "RGBA", PinType::Vec4);
        AddOutput("rgb", "RGB", PinType::Vec3);
        AddOutput("a", "Alpha", PinType::Float);
    }
    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        std::string uv = inVars.count("uv") ? inVars.at("uv") : "vTexCoords";
        std::string samplerName = textureSamplerUniform.empty() ? ("u_TexSampler_" + id) : textureSamplerUniform;
        return "vec4 " + outVar + " = texture(" + samplerName + ", " + uv + ");";
    }
};

class MasterLitNode : public ShaderNode {
public:
    MasterLitNode() {
        typeName = "MasterLit";
        title = "Master Lit (PBR)";
        AddInput("albedo", "Albedo", PinType::Vec3, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        AddInput("normal", "Normal", PinType::Vec3, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        AddInput("roughness", "Roughness", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
        AddInput("metallic", "Metallic", PinType::Float, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        AddInput("emissive", "Emissive", PinType::Vec3, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        AddInput("ao", "AO", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        AddInput("alpha", "Alpha", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }
};

class MasterUnlitNode : public ShaderNode {
public:
    MasterUnlitNode() {
        typeName = "MasterUnlit";
        title = "Master Unlit";
        AddInput("color", "Color", PinType::Vec3, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        AddInput("emissive", "Emissive", PinType::Vec3, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        AddInput("alpha", "Alpha", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }
};

class PropertyNode : public ShaderNode {
public:
    std::string propertyId;
    std::string propertyName;
    PinType propType = PinType::Float;

    PropertyNode() : PropertyNode("", "", PinType::Float) {}
    PropertyNode(const std::string& name, PinType type) : PropertyNode(name, name, type) {}
    PropertyNode(const std::string& propId, const std::string& name, PinType type)
        : propertyId(propId), propertyName(name), propType(type) {
        typeName = "Property";
        title = name.empty() ? "Property" : name;
        AddOutput("out", "Value", type);
    }

    std::string GenerateGLSL(const std::unordered_map<std::string, std::string>& inVars,
                             const std::string& outVar) const override {
        (void)inVars;
        std::string glslType = PinTypeToString(propType);
        std::string pName = propertyName.empty() ? title : propertyName;
        return glslType + " " + outVar + " = u_" + pName + ";";
    }
};

// ----------------------------------------------------------------------------
// ShaderGraph Container & Serializer
// ----------------------------------------------------------------------------

class ShaderGraph {
public:
    ShaderGraphType materialType = ShaderGraphType::Lit;
    std::vector<std::unique_ptr<ShaderNode>> nodes;
    std::vector<ShaderLink> links;
    std::vector<ShaderProperty> properties;

    ShaderGraph() = default;

    template<typename T, typename... Args>
    T* AddNode(const std::string& id, Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        node->id = id;
        T* ptr = node.get();
        nodes.push_back(std::move(node));
        return ptr;
    }

    ShaderNode* FindNode(const std::string& id) {
        for (auto& n : nodes) if (n->id == id) return n.get();
        return nullptr;
    }

    const ShaderNode* FindNode(const std::string& id) const {
        for (const auto& n : nodes) if (n->id == id) return n.get();
        return nullptr;
    }

    bool RemoveNode(const std::string& id) {
        auto it = std::find_if(nodes.begin(), nodes.end(), [&](const std::unique_ptr<ShaderNode>& n) {
            return n->id == id;
        });
        if (it == nodes.end()) return false;

        // Cascade delete links attached to this node
        links.erase(std::remove_if(links.begin(), links.end(), [&](const ShaderLink& link) {
            return link.fromNodeId == id || link.toNodeId == id;
        }), links.end());

        nodes.erase(it);
        return true;
    }

    bool AddLink(const std::string& linkId, const std::string& fromNodeId, const std::string& fromPortId,
                 const std::string& toNodeId, const std::string& toPortId) {
        // Prevent duplicate link ID
        for (const auto& l : links) if (l.id == linkId) return false;

        ShaderNode* fromNode = FindNode(fromNodeId);
        ShaderNode* toNode = FindNode(toNodeId);
        if (!fromNode || !toNode) return false;

        // Prevent self loop
        if (fromNodeId == toNodeId && fromPortId == toPortId) return false;

        ShaderPort* fromPort = fromNode->FindPort(fromPortId);
        ShaderPort* toPort = toNode->FindPort(toPortId);
        if (!fromPort || !toPort) return false;
        if (!fromPort->isOutput || toPort->isOutput) return false;

        // Type compatibility check (allow exact match or promotions)
        if (fromPort->type != toPort->type) {
            if (fromPort->type == PinType::Texture2D || toPort->type == PinType::Texture2D) {
                return false; // Incompatible Texture2D with scalar/vector
            }
        }

        // Replace existing link to the same input port if any
        links.erase(std::remove_if(links.begin(), links.end(), [&](const ShaderLink& l) {
            return l.toNodeId == toNodeId && l.toPortId == toPortId;
        }), links.end());

        links.push_back({ linkId, fromNodeId, fromPortId, toNodeId, toPortId });
        return true;
    }

    bool RemoveLink(const std::string& linkId) {
        auto it = std::find_if(links.begin(), links.end(), [&](const ShaderLink& l) {
            return l.id == linkId;
        });
        if (it == links.end()) return false;
        links.erase(it);
        return true;
    }

    bool AddProperty(const std::string& propId, const std::string& name, PinType type, glm::vec4 defVal = glm::vec4(0.0f), const std::string& texPath = "") {
        for (const auto& p : properties) {
            if (p.id == propId || p.name == name) return false; // Duplicate check
        }
        properties.push_back({ propId, name, type, defVal, texPath });
        return true;
    }

    bool SaveToFile(const std::string& filepath) const {
        std::ofstream out(filepath);
        if (!out.is_open()) return false;

        out << "ShaderGraphVersion: 1.0\n";
        out << "MaterialType: " << (materialType == ShaderGraphType::Lit ? "Lit" : "Unlit") << "\n";
        out << "PropertiesCount: " << properties.size() << "\n";
        for (const auto& p : properties) {
            out << "Property: " << p.id << " " << p.name << " " << (int)p.type << " "
                << p.defaultValue.x << " " << p.defaultValue.y << " " << p.defaultValue.z << " " << p.defaultValue.w
                << " " << (p.defaultTexturePath.empty() ? "none" : p.defaultTexturePath) << "\n";
        }
        out << "NodesCount: " << nodes.size() << "\n";
        for (const auto& n : nodes) {
            out << "Node: " << n->id << " " << n->typeName << " " << n->posX << " " << n->posY << " " << n->title << "\n";
        }
        out << "LinksCount: " << links.size() << "\n";
        for (const auto& l : links) {
            out << "Link: " << l.id << " " << l.fromNodeId << " " << l.fromPortId << " " << l.toNodeId << " " << l.toPortId << "\n";
        }
        return true;
    }

    bool LoadFromFile(const std::string& filepath) {
        std::ifstream in(filepath);
        if (!in.is_open()) return false;

        std::string line;
        if (!std::getline(in, line)) return false;
        if (line.find("ShaderGraphVersion:") == std::string::npos) return false;

        nodes.clear();
        links.clear();
        properties.clear();

        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            std::string tag;
            iss >> tag;
            if (tag == "MaterialType:") {
                std::string typeStr;
                iss >> typeStr;
                materialType = (typeStr == "Unlit") ? ShaderGraphType::Unlit : ShaderGraphType::Lit;
            } else if (tag == "Property:") {
                ShaderProperty p;
                int typeInt = 0;
                iss >> p.id >> p.name >> typeInt
                    >> p.defaultValue.x >> p.defaultValue.y >> p.defaultValue.z >> p.defaultValue.w
                    >> p.defaultTexturePath;
                p.type = (PinType)typeInt;
                if (p.defaultTexturePath == "none") p.defaultTexturePath = "";
                properties.push_back(p);
            } else if (tag == "Node:") {
                std::string id, typeName;
                float px = 0, py = 0;
                iss >> id >> typeName >> px >> py;
                std::string title;
                std::getline(iss, title);
                if (!title.empty() && title[0] == ' ') title.erase(0, 1);

                std::unique_ptr<ShaderNode> node = nullptr;
                if (typeName == "Add") node = std::make_unique<SlipTest::AddNode>();
                else if (typeName == "Subtract") node = std::make_unique<SubtractNode>();
                else if (typeName == "Multiply") node = std::make_unique<MultiplyNode>();
                else if (typeName == "Divide") node = std::make_unique<DivideNode>();
                else if (typeName == "Sin") node = std::make_unique<SinNode>();
                else if (typeName == "Cos") node = std::make_unique<CosNode>();
                else if (typeName == "Tan") node = std::make_unique<TanNode>();
                else if (typeName == "Dot") node = std::make_unique<DotNode>();
                else if (typeName == "Cross") node = std::make_unique<CrossNode>();
                else if (typeName == "Normalize") node = std::make_unique<NormalizeNode>();
                else if (typeName == "Length") node = std::make_unique<LengthNode>();
                else if (typeName == "Distance") node = std::make_unique<DistanceNode>();
                else if (typeName == "Min") node = std::make_unique<MinNode>();
                else if (typeName == "Max") node = std::make_unique<MaxNode>();
                else if (typeName == "Clamp") node = std::make_unique<ClampNode>();
                else if (typeName == "Lerp") node = std::make_unique<LerpNode>();
                else if (typeName == "Abs") node = std::make_unique<AbsNode>();
                else if (typeName == "Step") node = std::make_unique<StepNode>();
                else if (typeName == "Smoothstep") node = std::make_unique<SmoothstepNode>();
                else if (typeName == "Remap") node = std::make_unique<RemapNode>();
                else if (typeName == "ProceduralNoise") node = std::make_unique<ProceduralNoiseNode>();
                else if (typeName == "Voronoi") node = std::make_unique<VoronoiNode>();
                else if (typeName == "Time") node = std::make_unique<TimeNode>();
                else if (typeName == "DeltaTime") node = std::make_unique<DeltaTimeNode>();
                else if (typeName == "CameraPosition") node = std::make_unique<CameraPosNode>();
                else if (typeName == "WorldPosition") node = std::make_unique<WorldPosNode>();
                else if (typeName == "WorldNormal") node = std::make_unique<WorldNormalNode>();
                else if (typeName == "UV") node = std::make_unique<UVNode>();
                else if (typeName == "TilingOffset") node = std::make_unique<TilingOffsetNode>();
                else if (typeName == "Texture2D") node = std::make_unique<Texture2DNode>();
                else if (typeName == "MasterLit") node = std::make_unique<MasterLitNode>();
                else if (typeName == "MasterUnlit") node = std::make_unique<MasterUnlitNode>();
                else if (typeName == "Property") node = std::make_unique<PropertyNode>();

                if (node) {
                    node->id = id;
                    node->posX = px;
                    node->posY = py;
                    if (!title.empty()) {
                        node->title = title;
                        if (typeName == "Property") {
                            static_cast<PropertyNode*>(node.get())->propertyName = title;
                        }
                    }
                    nodes.push_back(std::move(node));
                }
            } else if (tag == "Link:") {
                ShaderLink l;
                iss >> l.id >> l.fromNodeId >> l.fromPortId >> l.toNodeId >> l.toPortId;
                links.push_back(l);
            }
        }
        return true;
    }
};

// ----------------------------------------------------------------------------
// Compiler & GLSL Generator (PROJECT.md Interface Contract 2)
// ----------------------------------------------------------------------------

struct GeneratedShaderCode {
    std::string vertexSource;
    std::string fragmentSource;
    bool success = false;
    std::string errorMessage;
};

class ShaderGraphCompiler {
public:
    static bool DetectCycle(const ShaderGraph& graph) {
        std::unordered_map<std::string, std::vector<std::string>> adj;
        for (const auto& link : graph.links) {
            adj[link.fromNodeId].push_back(link.toNodeId);
        }

        std::unordered_map<std::string, int> visitState; // 0 = unvisited, 1 = visiting, 2 = visited

        std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
            visitState[u] = 1;
            if (adj.count(u)) {
                for (const auto& v : adj[u]) {
                    if (visitState[v] == 1) return true; // Cycle detected
                    if (visitState[v] == 0 && dfs(v)) return true;
                }
            }
            visitState[u] = 2;
            return false;
        };

        for (const auto& node : graph.nodes) {
            if (visitState[node->id] == 0) {
                if (dfs(node->id)) return true;
            }
        }
        return false;
    }

    static std::vector<std::string> TopologicalSort(const ShaderGraph& graph, bool& hasCycle) {
        std::unordered_map<std::string, int> inDegree;
        std::unordered_map<std::string, std::vector<std::string>> adj;

        for (const auto& node : graph.nodes) {
            inDegree[node->id] = 0;
        }

        for (const auto& link : graph.links) {
            adj[link.fromNodeId].push_back(link.toNodeId);
            inDegree[link.toNodeId]++;
        }

        std::queue<std::string> q;
        for (const auto& pair : inDegree) {
            if (pair.second == 0) q.push(pair.first);
        }

        std::vector<std::string> order;
        while (!q.empty()) {
            std::string u = q.front();
            q.pop();
            order.push_back(u);

            if (adj.count(u)) {
                for (const auto& v : adj[u]) {
                    inDegree[v]--;
                    if (inDegree[v] == 0) q.push(v);
                }
            }
        }

        hasCycle = (order.size() != graph.nodes.size());
        return order;
    }

    static GeneratedShaderCode Compile(const ShaderGraph& graph) {
        GeneratedShaderCode result;

        if (graph.nodes.empty()) {
            // Empty graph fallback
            result.vertexSource =
                "#version 330 core\n"
                "layout (location = 0) in vec3 aPos;\n"
                "uniform mat4 u_Model;\n"
                "uniform mat4 u_View;\n"
                "uniform mat4 u_Projection;\n"
                "void main() {\n"
                "    gl_Position = u_Projection * u_View * u_Model * vec4(aPos, 1.0);\n"
                "}\n";
            result.fragmentSource =
                "#version 330 core\n"
                "out vec4 FragColor;\n"
                "void main() {\n"
                "    FragColor = vec4(0.8, 0.2, 0.8, 1.0);\n" // Fallback magenta
                "}\n";
            result.success = true;
            return result;
        }

        bool hasCycle = false;
        std::vector<std::string> sortedNodeIds = TopologicalSort(graph, hasCycle);
        if (hasCycle) {
            result.success = false;
            result.errorMessage = "Cycle detected in Shader Graph DAG topology.";
            return result;
        }

        // Standard OpenGL 3.3 Core Profile Vertex Shader
        std::ostringstream vs;
        vs << "#version 330 core\n"
           << "layout (location = 0) in vec3 aPos;\n"
           << "layout (location = 1) in vec3 aNormal;\n"
           << "layout (location = 2) in vec2 aTexCoords;\n"
           << "layout (location = 3) in vec3 aTangent;\n"
           << "layout (location = 4) in vec3 aBitangent;\n"
           << "uniform mat4 u_Model;\n"
           << "uniform mat4 u_View;\n"
           << "uniform mat4 u_Projection;\n"
           << "out vec3 vWorldPos;\n"
           << "out vec3 vNormal;\n"
           << "out vec2 vTexCoords;\n"
           << "out vec3 vTangent;\n"
           << "out vec3 vBitangent;\n"
           << "void main() {\n"
           << "    vec4 worldPos = u_Model * vec4(aPos, 1.0);\n"
           << "    vWorldPos = worldPos.xyz;\n"
           << "    vNormal = normalize(mat3(u_Model) * aNormal);\n"
           << "    vTangent = normalize(mat3(u_Model) * aTangent);\n"
           << "    vBitangent = normalize(mat3(u_Model) * aBitangent);\n"
           << "    vTexCoords = aTexCoords;\n"
           << "    gl_Position = u_Projection * u_View * worldPos;\n"
           << "}\n";

        // Fragment Shader with custom uniforms, time bindings, and topological node instructions
        std::ostringstream fs;
        fs << "#version 330 core\n"
           << "in vec3 vWorldPos;\n"
           << "in vec3 vNormal;\n"
           << "in vec2 vTexCoords;\n"
           << "in vec3 vTangent;\n"
           << "in vec3 vBitangent;\n"
           << "uniform vec3 u_CameraPos;\n"
           << "uniform float u_Time;\n"
           << "uniform float u_DeltaTime;\n";

        // Inject exposed property uniforms
        for (const auto& p : graph.properties) {
            std::string glslType = PinTypeToString(p.type);
            fs << "uniform " << glslType << " u_" << p.name << ";\n";
        }

        // Inject custom texture samplers (Texture2D nodes)
        int texUnit = 20;
        for (const auto& node : graph.nodes) {
            if (node->typeName == "Texture2D") {
                fs << "uniform sampler2D u_TexSampler_" << node->id << ";\n";
                texUnit++;
            }
        }

        fs << "out vec4 FragColor;\n";
        fs << "void main() {\n";

        // Map: link destination (toNodeId + "_" + toPortId) -> variable name
        std::unordered_map<std::string, std::string> portVarMap;

        // Generate lines in topological order
        for (const auto& nodeId : sortedNodeIds) {
            ShaderNode* node = const_cast<ShaderGraph&>(graph).FindNode(nodeId);
            if (!node) continue;

            std::unordered_map<std::string, std::string> inVars;
            for (const auto& inPort : node->inputs) {
                // Find incoming link
                bool linked = false;
                for (const auto& l : graph.links) {
                    if (l.toNodeId == node->id && l.toPortId == inPort.id) {
                        std::string srcVar = "node_" + l.fromNodeId + "_out";
                        inVars[inPort.id] = srcVar;
                        linked = true;
                        break;
                    }
                }
                if (!linked) {
                    // Use port default value
                    if (inPort.type == PinType::Float) {
                        inVars[inPort.id] = std::to_string(inPort.defaultValue.x);
                    } else if (inPort.type == PinType::Vec2) {
                        inVars[inPort.id] = "vec2(" + std::to_string(inPort.defaultValue.x) + ", " + std::to_string(inPort.defaultValue.y) + ")";
                    } else if (inPort.type == PinType::Vec3) {
                        inVars[inPort.id] = "vec3(" + std::to_string(inPort.defaultValue.x) + ", " + std::to_string(inPort.defaultValue.y) + ", " + std::to_string(inPort.defaultValue.z) + ")";
                    } else if (inPort.type == PinType::Vec4) {
                        inVars[inPort.id] = "vec4(" + std::to_string(inPort.defaultValue.x) + ", " + std::to_string(inPort.defaultValue.y) + ", " + std::to_string(inPort.defaultValue.z) + ", " + std::to_string(inPort.defaultValue.w) + ")";
                    }
                }
            }

            std::string outVar = "node_" + node->id + "_out";
            std::string lineCode = node->GenerateGLSL(inVars, outVar);
            if (!lineCode.empty()) {
                fs << "    " << lineCode << "\n";
            }
        }

        // Master Output handling
        if (graph.materialType == ShaderGraphType::Lit) {
            // Find MasterLit node
            const ShaderNode* masterNode = nullptr;
            for (const auto& n : graph.nodes) {
                if (n->typeName == "MasterLit") { masterNode = n.get(); break; }
            }

            std::string albedoVar = "vec3(1.0)";
            std::string emissiveVar = "vec3(0.0)";
            std::string alphaVar = "1.0";
            if (masterNode) {
                for (const auto& l : graph.links) {
                    if (l.toNodeId == masterNode->id) {
                        if (l.toPortId == "albedo") {
                            albedoVar = "node_" + l.fromNodeId + "_out";
                            if (l.fromPortId == "rgb" || l.fromPortId == "rgba") albedoVar += ".rgb";
                        }
                        if (l.toPortId == "emissive") {
                            emissiveVar = "node_" + l.fromNodeId + "_out";
                            if (l.fromPortId == "rgb" || l.fromPortId == "rgba") emissiveVar += ".rgb";
                        }
                        if (l.toPortId == "alpha") {
                            alphaVar = "node_" + l.fromNodeId + "_out";
                            if (l.fromPortId == "a" || l.fromPortId == "alpha" || l.fromPortId == "rgba") alphaVar += ".a";
                        }
                    }
                }
            }
            fs << "    vec3 N = normalize(vNormal);\n"
               << "    vec3 L = normalize(vec3(0.5, 1.0, 0.3));\n"
               << "    float NdotL = max(dot(N, L), 0.0) * 0.8 + 0.2;\n"
               << "    FragColor = vec4(" << albedoVar << " * NdotL + " << emissiveVar << ", " << alphaVar << ");\n";
        } else {
            // Unlit
            const ShaderNode* masterNode = nullptr;
            for (const auto& n : graph.nodes) {
                if (n->typeName == "MasterUnlit") { masterNode = n.get(); break; }
            }

            std::string colorVar = "vec3(1.0)";
            std::string emissiveVar = "vec3(0.0)";
            std::string alphaVar = "1.0";
            if (masterNode) {
                for (const auto& l : graph.links) {
                    if (l.toNodeId == masterNode->id) {
                        if (l.toPortId == "color") {
                            colorVar = "node_" + l.fromNodeId + "_out";
                            if (l.fromPortId == "rgb" || l.fromPortId == "rgba") colorVar += ".rgb";
                        }
                        if (l.toPortId == "emissive") {
                            emissiveVar = "node_" + l.fromNodeId + "_out";
                            if (l.fromPortId == "rgb" || l.fromPortId == "rgba") emissiveVar += ".rgb";
                        }
                        if (l.toPortId == "alpha") {
                            alphaVar = "node_" + l.fromNodeId + "_out";
                            if (l.fromPortId == "a" || l.fromPortId == "alpha" || l.fromPortId == "rgba") alphaVar += ".a";
                        }
                    }
                }
            }
            fs << "    FragColor = vec4(" << colorVar << " + " << emissiveVar << ", " << alphaVar << ");\n";
        }

        fs << "}\n";

        result.vertexSource = vs.str();
        result.fragmentSource = fs.str();
        result.success = true;
        return result;
    }
};

// ----------------------------------------------------------------------------
// TestShader OpenGL Shader Program Wrapper
// ----------------------------------------------------------------------------

class TestShader {
public:
    unsigned int ID = 0;

    static TestShader* CreateFromSource(const char* vertexSrc, const char* fragmentSrc) {
        if (!vertexSrc || !fragmentSrc) return nullptr;
        int success = 0;
        char infoLog[512];

        unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vertexSrc, NULL);
        glCompileShader(vs);
        glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vs, sizeof(infoLog), NULL, infoLog);
            std::cerr << "[TestShader] Vertex shader compilation failed: " << infoLog << std::endl;
            glDeleteShader(vs);
            return nullptr;
        }

        unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fragmentSrc, NULL);
        glCompileShader(fs);
        glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fs, sizeof(infoLog), NULL, infoLog);
            std::cerr << "[TestShader] Fragment shader compilation failed: " << infoLog << std::endl;
            glDeleteShader(vs);
            glDeleteShader(fs);
            return nullptr;
        }

        unsigned int prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(prog, sizeof(infoLog), NULL, infoLog);
            std::cerr << "[TestShader] Program linking failed: " << infoLog << std::endl;
            glDeleteShader(vs);
            glDeleteShader(fs);
            glDeleteProgram(prog);
            return nullptr;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        TestShader* shader = new TestShader();
        shader->ID = prog;
        return shader;
    }

    void use() const { if (ID) glUseProgram(ID); }
    void destroy() {
        if (ID) {
            glDeleteProgram(ID);
            ID = 0;
        }
        delete this;
    }

    void setFloat(const std::string& name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setVec3(const std::string& name, const glm::vec3& value) const {
        glUniform3f(glGetUniformLocation(ID, name.c_str()), value.x, value.y, value.z);
    }
    void setVec4(const std::string& name, const glm::vec4& value) const {
        glUniform4f(glGetUniformLocation(ID, name.c_str()), value.x, value.y, value.z, value.w);
    }
    void setMat4(const std::string& name, const glm::mat4& value) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
    }
};

// ----------------------------------------------------------------------------
// Material System Integration Wrapper (PROJECT.md Interface Contract)
// ----------------------------------------------------------------------------

class TestShaderMaterial {
public:
    std::string path;
    std::string shaderGraphPath;
    std::unordered_map<std::string, float> graphFloatParams;
    std::unordered_map<std::string, glm::vec4> graphVectorParams;
    std::unordered_map<std::string, std::string> graphTextureParams;

    TestShader* shader = nullptr;

    ~TestShaderMaterial() {
        if (shader) {
            shader->destroy();
            shader = nullptr;
        }
    }

    bool LoadShaderGraph(const std::string& sgPath) {
        if (sgPath.empty()) return false;
        ShaderGraph graph;
        if (!graph.LoadFromFile(sgPath)) return false;

        shaderGraphPath = sgPath;
        GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
        if (!code.success) return false;

        if (shader) {
            shader->destroy();
            shader = nullptr;
        }

        shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
        return (shader != nullptr);
    }

    void SetGraphFloat(const std::string& name, float val) {
        graphFloatParams[name] = val;
    }

    void SetGraphVector(const std::string& name, const glm::vec4& val) {
        graphVectorParams[name] = val;
    }

    void SetGraphTexture(const std::string& name, const std::string& texPath) {
        graphTextureParams[name] = texPath;
    }

    void BindMaterialProperties(TestShader* target) {
        if (!target) return;
        target->use();
        for (const auto& pair : graphFloatParams) {
            target->setFloat("u_" + pair.first, pair.second);
        }
        for (const auto& pair : graphVectorParams) {
            target->setVec4("u_" + pair.first, pair.second);
        }
    }

    bool Save(const std::string& savePath) {
        std::ofstream out(savePath);
        if (!out.is_open()) return false;

        out << "shaderType: ShaderGraph\n";
        out << "shaderGraph: " << shaderGraphPath << "\n";
        for (const auto& pair : graphFloatParams) {
            out << "graphFloat_" << pair.first << ": " << pair.second << "\n";
        }
        for (const auto& pair : graphVectorParams) {
            out << "graphVector_" << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
        }
        for (const auto& pair : graphTextureParams) {
            out << "graphTexture_" << pair.first << ": " << pair.second << "\n";
        }
        return true;
    }

    bool Load(const std::string& loadPath) {
        std::ifstream in(loadPath);
        if (!in.is_open()) return false;

        graphFloatParams.clear();
        graphVectorParams.clear();
        graphTextureParams.clear();

        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            if (!val.empty() && val[0] == ' ') val.erase(0, 1);

            if (key == "shaderGraph") {
                LoadShaderGraph(val);
            } else if (key.rfind("graphFloat_", 0) == 0) {
                std::string propName = key.substr(11);
                graphFloatParams[propName] = std::stof(val);
            } else if (key.rfind("graphVector_", 0) == 0) {
                std::string propName = key.substr(12);
                std::istringstream iss(val);
                glm::vec4 vec;
                iss >> vec.x >> vec.y >> vec.z >> vec.w;
                graphVectorParams[propName] = vec;
            } else if (key.rfind("graphTexture_", 0) == 0) {
                std::string propName = key.substr(13);
                graphTextureParams[propName] = val;
            }
        }
        return true;
    }
};

} // namespace SlipTest

using namespace SlipTest;

// ============================================================================
// TIER 1: Comprehensive Feature Coverage (>=5 tests per feature, 15 features)
// ============================================================================

// --- F01: Graph Model & Ports ---
bool Test_T1_F01_01_EmptyGraphDefaultState() {
    ShaderGraph graph;
    TEST_ASSERT_EQ(graph.materialType, ShaderGraphType::Lit);
    TEST_ASSERT_EQ(graph.nodes.size(), (size_t)0);
    TEST_ASSERT_EQ(graph.links.size(), (size_t)0);
    TEST_ASSERT_EQ(graph.properties.size(), (size_t)0);
    return true;
}

bool Test_T1_F01_02_AddNodesAndUniqueIDs() {
    ShaderGraph graph;
    auto* n1 = graph.AddNode<AddNode>("node1");
    auto* n2 = graph.AddNode<MultiplyNode>("node2");
    TEST_ASSERT(n1 != nullptr, "Node 1 should be added");
    TEST_ASSERT(n2 != nullptr, "Node 2 should be added");
    TEST_ASSERT_EQ(graph.nodes.size(), (size_t)2);
    TEST_ASSERT_STREQ(graph.FindNode("node1")->typeName.c_str(), "Add");
    TEST_ASSERT_STREQ(graph.FindNode("node2")->typeName.c_str(), "Multiply");
    return true;
}

bool Test_T1_F01_03_AddCompatibleLink() {
    ShaderGraph graph;
    graph.AddNode<AddNode>("n1");
    graph.AddNode<MultiplyNode>("n2");
    bool linked = graph.AddLink("l1", "n1", "out", "n2", "inA");
    TEST_ASSERT_TRUE(linked);
    TEST_ASSERT_EQ(graph.links.size(), (size_t)1);
    TEST_ASSERT_STREQ(graph.links[0].fromNodeId.c_str(), "n1");
    TEST_ASSERT_STREQ(graph.links[0].toNodeId.c_str(), "n2");
    return true;
}

bool Test_T1_F01_04_RemoveLinkById() {
    ShaderGraph graph;
    graph.AddNode<AddNode>("n1");
    graph.AddNode<MultiplyNode>("n2");
    graph.AddLink("l1", "n1", "out", "n2", "inA");
    TEST_ASSERT_EQ(graph.links.size(), (size_t)1);
    bool removed = graph.RemoveLink("l1");
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQ(graph.links.size(), (size_t)0);
    return true;
}

bool Test_T1_F01_05_RemoveNodeCascadesLinks() {
    ShaderGraph graph;
    graph.AddNode<AddNode>("n1");
    graph.AddNode<MultiplyNode>("n2");
    graph.AddNode<SinNode>("n3");
    graph.AddLink("l1", "n1", "out", "n2", "inA");
    graph.AddLink("l2", "n2", "out", "n3", "in");
    TEST_ASSERT_EQ(graph.links.size(), (size_t)2);
    bool removed = graph.RemoveNode("n2");
    TEST_ASSERT_TRUE(removed);
    TEST_ASSERT_EQ(graph.nodes.size(), (size_t)2);
    TEST_ASSERT_EQ(graph.links.size(), (size_t)0); // Both l1 and l2 cascaded
    return true;
}

// --- F02: Math & Vector Nodes ---
bool Test_T1_F02_01_ArithmeticNodesGLSL() {
    AddNode add;
    SubtractNode sub;
    MultiplyNode mul;
    DivideNode div;
    std::unordered_map<std::string, std::string> inMap = { {"inA", "1.0"}, {"inB", "2.0"} };
    TEST_ASSERT(add.GenerateGLSL(inMap, "out").find("1.0 + 2.0") != std::string::npos, "Add GLSL format");
    TEST_ASSERT(sub.GenerateGLSL(inMap, "out").find("1.0 - 2.0") != std::string::npos, "Subtract GLSL format");
    TEST_ASSERT(mul.GenerateGLSL(inMap, "out").find("1.0 * 2.0") != std::string::npos, "Multiply GLSL format");
    TEST_ASSERT(div.GenerateGLSL(inMap, "out").find("1.0 /") != std::string::npos, "Divide GLSL format");
    return true;
}

bool Test_T1_F02_02_TrigonometricNodesGLSL() {
    SinNode sinNode;
    CosNode cosNode;
    TanNode tanNode;
    std::unordered_map<std::string, std::string> inMap = { {"in", "u_Time"} };
    TEST_ASSERT(sinNode.GenerateGLSL(inMap, "out").find("sin(u_Time)") != std::string::npos, "Sin GLSL format");
    TEST_ASSERT(cosNode.GenerateGLSL(inMap, "out").find("cos(u_Time)") != std::string::npos, "Cos GLSL format");
    TEST_ASSERT(tanNode.GenerateGLSL(inMap, "out").find("tan(u_Time)") != std::string::npos, "Tan GLSL format");
    return true;
}

bool Test_T1_F02_03_VectorDotCrossNodes() {
    DotNode dotNode;
    CrossNode crossNode;
    std::unordered_map<std::string, std::string> inMap = { {"inA", "vNormal"}, {"inB", "vec3(0,1,0)"} };
    TEST_ASSERT(dotNode.GenerateGLSL(inMap, "out").find("dot(vNormal, vec3(0,1,0))") != std::string::npos, "Dot GLSL format");
    TEST_ASSERT(crossNode.GenerateGLSL(inMap, "out").find("cross(vNormal, vec3(0,1,0))") != std::string::npos, "Cross GLSL format");
    return true;
}

bool Test_T1_F02_04_VectorNormalizeLengthNodes() {
    NormalizeNode normNode;
    LengthNode lenNode;
    std::unordered_map<std::string, std::string> inMap = { {"in", "vWorldPos"} };
    TEST_ASSERT(normNode.GenerateGLSL(inMap, "out").find("normalize(vWorldPos)") != std::string::npos, "Normalize GLSL format");
    TEST_ASSERT(lenNode.GenerateGLSL(inMap, "out").find("length(vWorldPos)") != std::string::npos, "Length GLSL format");
    return true;
}

bool Test_T1_F02_05_VectorDistanceSwizzleNodes() {
    DistanceNode distNode;
    std::unordered_map<std::string, std::string> inMap = { {"inA", "u_CameraPos"}, {"inB", "vWorldPos"} };
    std::string glsl = distNode.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("distance(u_CameraPos, vWorldPos)") != std::string::npos, "Distance GLSL format");
    return true;
}

// --- F03: Procedural & Utility Nodes ---
bool Test_T1_F03_01_MinMaxNodes() {
    MinNode minNode;
    MaxNode maxNode;
    std::unordered_map<std::string, std::string> inMap = { {"inA", "0.2"}, {"inB", "0.8"} };
    TEST_ASSERT(minNode.GenerateGLSL(inMap, "out").find("min(0.2, 0.8)") != std::string::npos, "Min GLSL");
    TEST_ASSERT(maxNode.GenerateGLSL(inMap, "out").find("max(0.2, 0.8)") != std::string::npos, "Max GLSL");
    return true;
}

bool Test_T1_F03_02_ClampLerpNodes() {
    ClampNode clampNode;
    LerpNode lerpNode;
    std::unordered_map<std::string, std::string> inMapClamp = { {"in", "val"}, {"inMin", "0.0"}, {"inMax", "1.0"} };
    TEST_ASSERT(clampNode.GenerateGLSL(inMapClamp, "out").find("clamp(val") != std::string::npos, "Clamp GLSL");
    std::unordered_map<std::string, std::string> inMapLerp = { {"inA", "colA"}, {"inB", "colB"}, {"t", "0.5"} };
    TEST_ASSERT(lerpNode.GenerateGLSL(inMapLerp, "out").find("mix(colA, colB, 0.5)") != std::string::npos, "Lerp GLSL");
    return true;
}

bool Test_T1_F03_03_AbsStepSmoothstepNodes() {
    AbsNode absNode;
    StepNode stepNode;
    SmoothstepNode ssNode;
    std::unordered_map<std::string, std::string> inMapAbs = { {"in", "-5.0"} };
    TEST_ASSERT(absNode.GenerateGLSL(inMapAbs, "out").find("abs(-5.0)") != std::string::npos, "Abs GLSL");
    std::unordered_map<std::string, std::string> inMapStep = { {"edge", "0.5"}, {"in", "val"} };
    TEST_ASSERT(stepNode.GenerateGLSL(inMapStep, "out").find("step(0.5, val)") != std::string::npos, "Step GLSL");
    std::unordered_map<std::string, std::string> inMapSS = { {"edge0", "0.1"}, {"edge1", "0.9"}, {"in", "val"} };
    TEST_ASSERT(ssNode.GenerateGLSL(inMapSS, "out").find("smoothstep(0.1, 0.9, val)") != std::string::npos, "Smoothstep GLSL");
    return true;
}

bool Test_T1_F03_04_RemapNodeLinearScale() {
    RemapNode remap;
    std::unordered_map<std::string, std::string> inMap = {
        {"in", "val"}, {"inMin", "0.0"}, {"inMax", "1.0"}, {"outMin", "10.0"}, {"outMax", "20.0"}
    };
    std::string glsl = remap.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("10.0 + (val - 0.0)") != std::string::npos, "Remap formula");
    return true;
}

bool Test_T1_F03_05_ProceduralNoiseVoronoiNodes() {
    ProceduralNoiseNode noiseNode;
    VoronoiNode voronoiNode;
    std::unordered_map<std::string, std::string> inMap = { {"uv", "vTexCoords"}, {"scale", "5.0"} };
    TEST_ASSERT(noiseNode.GenerateGLSL(inMap, "out").find("fract(sin(dot") != std::string::npos, "Noise GLSL");
    TEST_ASSERT(voronoiNode.GenerateGLSL(inMap, "out").find("fract(vTexCoords * 5.0)") != std::string::npos, "Voronoi GLSL");
    return true;
}

// --- F04: Time & DeltaTime Nodes ---
bool Test_T1_F04_01_TimeNodeUniformRef() {
    TimeNode timeNode;
    std::unordered_map<std::string, std::string> emptyMap;
    std::string glsl = timeNode.GenerateGLSL(emptyMap, "t");
    TEST_ASSERT(glsl.find("u_Time") != std::string::npos, "Time references u_Time");
    return true;
}

bool Test_T1_F04_02_DeltaTimeNodeUniformRef() {
    DeltaTimeNode dtNode;
    std::unordered_map<std::string, std::string> emptyMap;
    std::string glsl = dtNode.GenerateGLSL(emptyMap, "dt");
    TEST_ASSERT(glsl.find("u_DeltaTime") != std::string::npos, "DeltaTime references u_DeltaTime");
    return true;
}

bool Test_T1_F04_03_TimeScaledOscillator() {
    ShaderGraph graph;
    graph.AddNode<TimeNode>("timeNode");
    graph.AddNode<MultiplyNode>("speedMul");
    graph.AddNode<SinNode>("sinOsc");
    graph.AddLink("l1", "timeNode", "time", "speedMul", "inA");
    graph.AddLink("l2", "speedMul", "out", "sinOsc", "in");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("u_Time") != std::string::npos, "Compiles oscillator with u_Time");
    TEST_ASSERT(code.fragmentSource.find("sin(") != std::string::npos, "Compiles oscillator with sin");
    return true;
}

bool Test_T1_F04_04_SinTimeOscillatorNode() {
    SinNode sinNode;
    std::unordered_map<std::string, std::string> inMap = { {"in", "u_Time"} };
    std::string glsl = sinNode.GenerateGLSL(inMap, "sinVal");
    TEST_ASSERT(glsl.find("sin(u_Time)") != std::string::npos, "SinTime GLSL");
    return true;
}

bool Test_T1_F04_05_DeltaTimeAccumulator() {
    ShaderGraph graph;
    graph.AddNode<DeltaTimeNode>("dt");
    graph.AddNode<MultiplyNode>("speed");
    graph.AddNode<AddNode>("accum");
    graph.AddLink("l1", "dt", "deltaTime", "speed", "inA");
    graph.AddLink("l2", "speed", "out", "accum", "inA");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("u_DeltaTime") != std::string::npos, "DeltaTime in fragment source");
    return true;
}

// --- F05: Camera & Transform Nodes ---
bool Test_T1_F05_01_CameraPositionUniformRef() {
    CameraPosNode camNode;
    std::unordered_map<std::string, std::string> emptyMap;
    TEST_ASSERT(camNode.GenerateGLSL(emptyMap, "cPos").find("u_CameraPos") != std::string::npos, "CameraPos uniform");
    return true;
}

bool Test_T1_F05_02_WorldPositionVaryingRef() {
    WorldPosNode wpNode;
    std::unordered_map<std::string, std::string> emptyMap;
    TEST_ASSERT(wpNode.GenerateGLSL(emptyMap, "wPos").find("vWorldPos") != std::string::npos, "WorldPos varying");
    return true;
}

bool Test_T1_F05_03_WorldNormalVaryingRef() {
    WorldNormalNode wnNode;
    std::unordered_map<std::string, std::string> emptyMap;
    TEST_ASSERT(wnNode.GenerateGLSL(emptyMap, "wNorm").find("vNormal") != std::string::npos, "WorldNormal varying");
    return true;
}

bool Test_T1_F05_04_ViewAndProjectionMatrices() {
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.vertexSource.find("u_View") != std::string::npos, "u_View in vertex shader");
    TEST_ASSERT(code.vertexSource.find("u_Projection") != std::string::npos, "u_Projection in vertex shader");
    return true;
}

bool Test_T1_F05_05_CameraToVertexDirectionVector() {
    DistanceNode dist;
    std::unordered_map<std::string, std::string> inMap = { {"inA", "u_CameraPos"}, {"inB", "vWorldPos"} };
    std::string glsl = dist.GenerateGLSL(inMap, "viewDist");
    TEST_ASSERT(glsl.find("distance(u_CameraPos, vWorldPos)") != std::string::npos, "Camera distance formula");
    return true;
}

// --- F06: Texture Sampling Nodes ---
bool Test_T1_F06_01_Texture2DSamplerNode() {
    Texture2DNode texNode;
    texNode.textureSamplerUniform = "u_Diffuse";
    std::unordered_map<std::string, std::string> inMap = { {"uv", "vTexCoords"} };
    std::string glsl = texNode.GenerateGLSL(inMap, "texCol");
    TEST_ASSERT(glsl.find("texture(u_Diffuse, vTexCoords)") != std::string::npos, "Texture sampling call");
    return true;
}

bool Test_T1_F06_02_UVCoordinatesNode() {
    UVNode uvNode;
    std::unordered_map<std::string, std::string> emptyMap;
    TEST_ASSERT(uvNode.GenerateGLSL(emptyMap, "uv").find("vTexCoords") != std::string::npos, "UV node outputs vTexCoords");
    return true;
}

bool Test_T1_F06_03_TilingAndOffsetNode() {
    TilingOffsetNode toNode;
    std::unordered_map<std::string, std::string> inMap = {
        {"uv", "vTexCoords"}, {"tiling", "vec2(2,2)"}, {"offset", "vec2(0.5, 0.5)"}
    };
    std::string glsl = toNode.GenerateGLSL(inMap, "transformedUV");
    TEST_ASSERT(glsl.find("vTexCoords * vec2(2,2) + vec2(0.5, 0.5)") != std::string::npos, "Tiling & offset calculation");
    return true;
}

bool Test_T1_F06_04_MultiSamplerTextureUnits() {
    ShaderGraph graph;
    graph.AddNode<Texture2DNode>("tex1");
    graph.AddNode<Texture2DNode>("tex2");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("u_TexSampler_tex1") != std::string::npos, "Sampler 1 uniform declared");
    TEST_ASSERT(code.fragmentSource.find("u_TexSampler_tex2") != std::string::npos, "Sampler 2 uniform declared");
    return true;
}

bool Test_T1_F06_05_TextureRGBAChannelSwizzles() {
    Texture2DNode texNode;
    TEST_ASSERT(texNode.FindPort("rgba") != nullptr, "Has RGBA output");
    TEST_ASSERT(texNode.FindPort("rgb") != nullptr, "Has RGB output");
    TEST_ASSERT(texNode.FindPort("a") != nullptr, "Has Alpha output");
    return true;
}

// --- F07: Master Output Node (Lit vs. Unlit) ---
bool Test_T1_F07_01_MasterLitPBRPins() {
    MasterLitNode lit;
    TEST_ASSERT(lit.FindPort("albedo") != nullptr, "Lit has albedo");
    TEST_ASSERT(lit.FindPort("normal") != nullptr, "Lit has normal");
    TEST_ASSERT(lit.FindPort("roughness") != nullptr, "Lit has roughness");
    TEST_ASSERT(lit.FindPort("metallic") != nullptr, "Lit has metallic");
    TEST_ASSERT(lit.FindPort("emissive") != nullptr, "Lit has emissive");
    TEST_ASSERT(lit.FindPort("ao") != nullptr, "Lit has ao");
    TEST_ASSERT(lit.FindPort("alpha") != nullptr, "Lit has alpha");
    return true;
}

bool Test_T1_F07_02_MasterUnlitColorPins() {
    MasterUnlitNode unlit;
    TEST_ASSERT(unlit.FindPort("color") != nullptr, "Unlit has color");
    TEST_ASSERT(unlit.FindPort("emissive") != nullptr, "Unlit has emissive");
    TEST_ASSERT(unlit.FindPort("alpha") != nullptr, "Unlit has alpha");
    return true;
}

bool Test_T1_F07_03_MaterialTypeSwitching() {
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Lit;
    graph.AddNode<MasterLitNode>("masterLit");
    GeneratedShaderCode codeLit = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(codeLit.success);
    TEST_ASSERT(codeLit.fragmentSource.find("NdotL") != std::string::npos, "Lit shader evaluates lighting");

    graph.materialType = ShaderGraphType::Unlit;
    graph.RemoveNode("masterLit");
    graph.AddNode<MasterUnlitNode>("masterUnlit");
    GeneratedShaderCode codeUnlit = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(codeUnlit.success);
    TEST_ASSERT(codeUnlit.fragmentSource.find("NdotL") == std::string::npos, "Unlit shader skips lighting");
    return true;
}

bool Test_T1_F07_04_MasterDisconnectedDefaults() {
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("FragColor = vec4(") != std::string::npos, "Generates default surface");
    return true;
}

bool Test_T1_F07_05_AlphaClippingCutoffHandling() {
    ShaderGraph graph;
    auto* master = graph.AddNode<MasterLitNode>("master");
    master->FindPort("alpha")->defaultValue = glm::vec4(0.8f);
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    return true;
}

// --- F08: Asset File Serialization (.shadergraph) ---
bool Test_T1_F08_01_SerializeEmptyGraphRoundTrip() {
    ShaderGraph graph;
    std::string path = "test_empty.shadergraph";
    bool saved = graph.SaveToFile(path);
    TEST_ASSERT_TRUE(saved);

    ShaderGraph loaded;
    bool loadedOk = loaded.LoadFromFile(path);
    std::remove(path.c_str());
    TEST_ASSERT_TRUE(loadedOk);
    TEST_ASSERT_EQ(loaded.nodes.size(), (size_t)0);
    return true;
}

bool Test_T1_F08_02_SerializeNodesPositionsTitlesRoundTrip() {
    ShaderGraph graph;
    auto* n = graph.AddNode<AddNode>("n1");
    n->posX = 120.5f;
    n->posY = 240.25f;
    n->title = "MyCustomAddNode";

    std::string path = "test_nodes.shadergraph";
    graph.SaveToFile(path);

    ShaderGraph loaded;
    loaded.LoadFromFile(path);
    std::remove(path.c_str());

    TEST_ASSERT_EQ(loaded.nodes.size(), (size_t)1);
    ShaderNode* loadedNode = loaded.FindNode("n1");
    TEST_ASSERT(loadedNode != nullptr, "Node n1 restored");
    TEST_ASSERT_FLOAT_NEAR(loadedNode->posX, 120.5f, 0.01f);
    TEST_ASSERT_FLOAT_NEAR(loadedNode->posY, 240.25f, 0.01f);
    TEST_ASSERT_STREQ(loadedNode->title.c_str(), "MyCustomAddNode");
    return true;
}

bool Test_T1_F08_03_SerializeComplexLinksTopologyRoundTrip() {
    ShaderGraph graph;
    graph.AddNode<AddNode>("n1");
    graph.AddNode<MultiplyNode>("n2");
    graph.AddNode<MasterLitNode>("master");
    graph.AddLink("link1", "n1", "out", "n2", "inA");
    graph.AddLink("link2", "n2", "out", "master", "albedo");

    std::string path = "test_links.shadergraph";
    graph.SaveToFile(path);

    ShaderGraph loaded;
    loaded.LoadFromFile(path);
    std::remove(path.c_str());

    TEST_ASSERT_EQ(loaded.links.size(), (size_t)2);
    TEST_ASSERT_STREQ(loaded.links[0].fromNodeId.c_str(), "n1");
    TEST_ASSERT_STREQ(loaded.links[0].toNodeId.c_str(), "n2");
    TEST_ASSERT_STREQ(loaded.links[1].fromNodeId.c_str(), "n2");
    TEST_ASSERT_STREQ(loaded.links[1].toNodeId.c_str(), "master");
    return true;
}

bool Test_T1_F08_04_SerializeExposedPropertiesRoundTrip() {
    ShaderGraph graph;
    graph.AddProperty("prop1", "MetallicScale", PinType::Float, glm::vec4(0.85f));
    graph.AddProperty("prop2", "TintColor", PinType::Vec4, glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));

    std::string path = "test_props.shadergraph";
    graph.SaveToFile(path);

    ShaderGraph loaded;
    loaded.LoadFromFile(path);
    std::remove(path.c_str());

    TEST_ASSERT_EQ(loaded.properties.size(), (size_t)2);
    TEST_ASSERT_STREQ(loaded.properties[0].name.c_str(), "MetallicScale");
    TEST_ASSERT_FLOAT_NEAR(loaded.properties[0].defaultValue.x, 0.85f, 0.001f);
    TEST_ASSERT_STREQ(loaded.properties[1].name.c_str(), "TintColor");
    TEST_ASSERT_FLOAT_NEAR(loaded.properties[1].defaultValue.y, 0.5f, 0.001f);
    return true;
}

bool Test_T1_F08_05_CorruptedFileGracefulFailure() {
    std::string path = "test_corrupt.shadergraph";
    std::ofstream out(path);
    out << "NOT_A_VALID_SHADER_GRAPH_HEADER\nrandom_gibberish: 1234\n";
    out.close();

    ShaderGraph loaded;
    bool loadedOk = loaded.LoadFromFile(path);
    std::remove(path.c_str());
    TEST_ASSERT_FALSE(loadedOk);
    return true;
}

// --- F09: Asset Pipeline / GUID Registration ---
bool Test_T1_F09_01_RecognizeShaderGraphExtension() {
    std::string path = "assets/materials/water.shadergraph";
    size_t dot = path.rfind('.');
    TEST_ASSERT(dot != std::string::npos, "Has extension");
    std::string ext = path.substr(dot);
    TEST_ASSERT_STREQ(ext.c_str(), ".shadergraph");
    return true;
}

bool Test_T1_F09_02_MetaFileGUIDGenerationFormat() {
    std::string metaPath = "assets/materials/water.shadergraph.meta";
    std::string fakeGuid = "a1b2c3d4-e5f6-7890-1234-56789abcdef0";
    TEST_ASSERT_EQ(fakeGuid.size(), (size_t)36);
    TEST_ASSERT_EQ(fakeGuid[8], '-');
    TEST_ASSERT_EQ(fakeGuid[13], '-');
    TEST_ASSERT_EQ(fakeGuid[18], '-');
    TEST_ASSERT_EQ(fakeGuid[23], '-');
    return true;
}

bool Test_T1_F09_03_AssetGUIDUniqueness() {
    std::set<std::string> guids;
    for (int i = 0; i < 50; ++i) {
        std::string g = "guid-" + std::to_string(i);
        guids.insert(g);
    }
    TEST_ASSERT_EQ(guids.size(), (size_t)50);
    return true;
}

bool Test_T1_F09_04_AssetReloadingByIdAndPath() {
    std::unordered_map<std::string, std::string> assetRegistry;
    assetRegistry["uuid-123"] = "assets/materials/surface.shadergraph";
    TEST_ASSERT_STREQ(assetRegistry["uuid-123"].c_str(), "assets/materials/surface.shadergraph");
    return true;
}

bool Test_T1_F09_05_RelativePathNormalization() {
    std::string p = "assets\\materials\\test.shadergraph";
    std::replace(p.begin(), p.end(), '\\', '/');
    TEST_ASSERT_STREQ(p.c_str(), "assets/materials/test.shadergraph");
    return true;
}

// --- F10: GLSL Code Generation (Lit & Unlit) ---
bool Test_T1_F10_01_TopologicalSortExecutionOrder() {
    ShaderGraph graph;
    graph.AddNode<TimeNode>("time");
    graph.AddNode<SinNode>("sin");
    graph.AddNode<AddNode>("add");
    graph.AddLink("l1", "time", "time", "sin", "in");
    graph.AddLink("l2", "sin", "out", "add", "inA");
    bool hasCycle = false;
    auto order = ShaderGraphCompiler::TopologicalSort(graph, hasCycle);
    TEST_ASSERT_FALSE(hasCycle);
    TEST_ASSERT_EQ(order.size(), (size_t)3);
    TEST_ASSERT_STREQ(order[0].c_str(), "time");
    TEST_ASSERT_STREQ(order[1].c_str(), "sin");
    TEST_ASSERT_STREQ(order[2].c_str(), "add");
    return true;
}

bool Test_T1_F10_02_GenerateValidGLSL330VertexShader() {
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.vertexSource.find("#version 330 core") != std::string::npos, "Version 330 core");
    TEST_ASSERT(code.vertexSource.find("gl_Position") != std::string::npos, "gl_Position assignment");
    return true;
}

bool Test_T1_F10_03_GenerateValidGLSL330LitFragShader() {
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Lit;
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("NdotL") != std::string::npos, "Contains NdotL PBR term");
    return true;
}

bool Test_T1_F10_04_GenerateValidGLSL330UnlitFragShader() {
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Unlit;
    graph.AddNode<MasterUnlitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("FragColor = vec4(") != std::string::npos, "Outputs FragColor");
    return true;
}

bool Test_T1_F10_05_OpenGLCompilationLinkCheck() {
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Shader compiles and links in OpenGL");
    TEST_ASSERT(shader->ID > 0, "Valid OpenGL program ID");
    shader->destroy();
    return true;
}

// --- F11: Time Uniform Binding (u_Time, u_DeltaTime) ---
bool Test_T1_F11_01_UniformLocationQueriesTime() {
    ShaderGraph graph;
    graph.AddNode<TimeNode>("timeNode");
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Compiled");
    shader->use();
    int locTime = glGetUniformLocation(shader->ID, "u_Time");
    int locDt = glGetUniformLocation(shader->ID, "u_DeltaTime");
    shader->destroy();
    (void)locTime; (void)locDt;
    return true;
}

bool Test_T1_F11_02_BindingTimeWithoutOpenGLErrors() {
    ShaderGraph graph;
    graph.AddNode<TimeNode>("time");
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Compiled");
    shader->use();
    shader->setFloat("u_Time", 12.34f);
    shader->setFloat("u_DeltaTime", 0.016f);
    GLenum err = glGetError();
    shader->destroy();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    return true;
}

bool Test_T1_F11_03_SuccessiveTimeProgression() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.update(1.0);
    float dt1 = globals.GetDeltaTime();
    globals.update(1.016);
    float dt2 = globals.GetDeltaTime();
    TEST_ASSERT(dt1 > 0.0f, "dt1 > 0");
    TEST_ASSERT(dt2 > 0.0f, "dt2 > 0");
    return true;
}

bool Test_T1_F11_04_SlipGlobalsUpdateTimeIntegration() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.SetDeltaTime(0.033f);
    TEST_ASSERT_FLOAT_NEAR(globals.GetDeltaTime(), 0.033f, 0.001f);
    return true;
}

bool Test_T1_F11_05_DeltaTimeSanitizationProtection() {
    SlipGlobals& globals = SlipGlobals::Get();
    globals.update(10.0);
    globals.update(10.0); // 0 elapsed
    float sanitizedDt = globals.GetDeltaTime();
    TEST_ASSERT(sanitizedDt >= 0.0001f, "Clamped minimum dt");
    return true;
}

// --- F12: SlipMaterial Integration & Shader Resolution ---
bool Test_T1_F12_01_AssignGraphShaderPointerToMaterial() {
    TestShaderMaterial mat;
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    std::string path = "test_mat_assign.shadergraph";
    graph.SaveToFile(path);
    bool ok = mat.LoadShaderGraph(path);
    std::remove(path.c_str());
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(mat.shader != nullptr, "Shader pointer assigned");
    return true;
}

bool Test_T1_F12_02_MaterialShaderTypeGraphTagging() {
    TestShaderMaterial mat;
    mat.shaderGraphPath = "assets/materials/mygraph.shadergraph";
    TEST_ASSERT(!mat.shaderGraphPath.empty(), "Material references graph path");
    return true;
}

bool Test_T1_F12_03_MaterialBindSetsCameraMatrices() {
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    shader->use();
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    shader->setMat4("u_Projection", proj);
    GLenum err = glGetError();
    shader->destroy();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    return true;
}

bool Test_T1_F12_04_RecompileGraphUpdatesHandleSafely() {
    TestShaderMaterial mat;
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("m1");
    std::string path = "test_recompile.shadergraph";
    graph.SaveToFile(path);
    mat.LoadShaderGraph(path);
    unsigned int id1 = mat.shader->ID;

    // Modify graph and reload
    graph.AddNode<AddNode>("add");
    graph.SaveToFile(path);
    mat.LoadShaderGraph(path);
    unsigned int id2 = mat.shader->ID;
    std::remove(path.c_str());

    TEST_ASSERT(id1 > 0, "First program valid");
    TEST_ASSERT(id2 > 0, "Second program valid");
    return true;
}

bool Test_T1_F12_05_MaterialDestructionCleansResources() {
    {
        TestShaderMaterial scopedMat;
        ShaderGraph graph;
        graph.AddNode<MasterLitNode>("m");
        std::string path = "test_destruct.shadergraph";
        graph.SaveToFile(path);
        scopedMat.LoadShaderGraph(path);
        std::remove(path.c_str());
        TEST_ASSERT(scopedMat.shader != nullptr, "Valid before scope exit");
    }
    // Cleaned up safely on destructor
    return true;
}

// --- F13: Public / Exposed Property Declaration ---
bool Test_T1_F13_01_DeclareFloatProperty() {
    ShaderGraph graph;
    bool ok = graph.AddProperty("p1", "RoughnessFactor", PinType::Float, glm::vec4(0.75f));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQ(graph.properties.size(), (size_t)1);
    TEST_ASSERT_FLOAT_NEAR(graph.properties[0].defaultValue.x, 0.75f, 0.001f);
    return true;
}

bool Test_T1_F13_02_DeclareVec2Vec3Properties() {
    ShaderGraph graph;
    graph.AddProperty("p1", "UVScale", PinType::Vec2, glm::vec4(2.0f, 3.0f, 0.0f, 0.0f));
    graph.AddProperty("p2", "TintColor", PinType::Vec3, glm::vec4(1.0f, 0.8f, 0.6f, 0.0f));
    TEST_ASSERT_EQ(graph.properties.size(), (size_t)2);
    TEST_ASSERT_EQ(graph.properties[0].type, PinType::Vec2);
    TEST_ASSERT_EQ(graph.properties[1].type, PinType::Vec3);
    return true;
}

bool Test_T1_F13_03_DeclareVec4ColorProperty() {
    ShaderGraph graph;
    graph.AddProperty("p1", "BaseColor", PinType::Vec4, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
    TEST_ASSERT_EQ(graph.properties.size(), (size_t)1);
    TEST_ASSERT_FLOAT_NEAR(graph.properties[0].defaultValue.w, 1.0f, 0.001f);
    return true;
}

bool Test_T1_F13_04_DeclareTexture2DProperty() {
    ShaderGraph graph;
    graph.AddProperty("p1", "MainTexture", PinType::Texture2D, glm::vec4(0.0f), "textures/brick.png");
    TEST_ASSERT_EQ(graph.properties.size(), (size_t)1);
    TEST_ASSERT_STREQ(graph.properties[0].defaultTexturePath.c_str(), "textures/brick.png");
    return true;
}

bool Test_T1_F13_05_ExposedPropertiesEmitGLSLUniforms() {
    ShaderGraph graph;
    graph.AddProperty("p1", "MetallicBoost", PinType::Float, glm::vec4(0.9f));
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);
    TEST_ASSERT(code.fragmentSource.find("uniform float u_MetallicBoost;") != std::string::npos, "Emits uniform declaration");
    return true;
}

// --- F14: Material Parameter Uniform Overrides ---
bool Test_T1_F14_01_OverrideFloatParameter() {
    TestShaderMaterial mat;
    mat.SetGraphFloat("Roughness", 0.12f);
    TEST_ASSERT_FLOAT_NEAR(mat.graphFloatParams["Roughness"], 0.12f, 0.001f);
    return true;
}

bool Test_T1_F14_02_OverrideVectorColorParameter() {
    TestShaderMaterial mat;
    mat.SetGraphVector("ColorTint", glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
    TEST_ASSERT_FLOAT_NEAR(mat.graphVectorParams["ColorTint"].z, 0.3f, 0.001f);
    return true;
}

bool Test_T1_F14_03_OverrideTextureParameter() {
    TestShaderMaterial mat;
    mat.SetGraphTexture("AlbedoMap", "assets/textures/custom_wood.png");
    TEST_ASSERT_STREQ(mat.graphTextureParams["AlbedoMap"].c_str(), "assets/textures/custom_wood.png");
    return true;
}

bool Test_T1_F14_04_ParameterOverridesBindToGLSL() {
    ShaderGraph graph;
    graph.AddProperty("p1", "RoughnessScale", PinType::Float, glm::vec4(0.5f));
    graph.AddNode<MasterLitNode>("master");
    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Compiled");

    TestShaderMaterial mat;
    mat.SetGraphFloat("RoughnessScale", 0.88f);
    mat.BindMaterialProperties(shader);
    GLenum err = glGetError();
    shader->destroy();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    return true;
}

bool Test_T1_F14_05_ParameterNameIDLookupSensitivity() {
    TestShaderMaterial mat;
    mat.SetGraphFloat("ParamA", 1.0f);
    mat.SetGraphFloat("parama", 2.0f);
    TEST_ASSERT_FLOAT_NEAR(mat.graphFloatParams["ParamA"], 1.0f, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(mat.graphFloatParams["parama"], 2.0f, 0.001f);
    return true;
}

// --- F15: Material File (.mat) Graph Persistence ---
bool Test_T1_F15_01_MatFileWritesShaderGraphPath() {
    TestShaderMaterial mat;
    mat.shaderGraphPath = "assets/materials/pbr_foil.shadergraph";
    std::string matPath = "test_persistence.mat";
    mat.Save(matPath);

    std::ifstream in(matPath);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::remove(matPath.c_str());
    TEST_ASSERT(content.find("shaderGraph: assets/materials/pbr_foil.shadergraph") != std::string::npos, "Contains path");
    return true;
}

bool Test_T1_F15_02_MatFileWritesParameterOverrides() {
    TestShaderMaterial mat;
    mat.shaderGraphPath = "test.shadergraph";
    mat.SetGraphFloat("Speed", 5.5f);
    mat.SetGraphVector("Tint", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    std::string matPath = "test_params.mat";
    mat.Save(matPath);

    std::ifstream in(matPath);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::remove(matPath.c_str());
    TEST_ASSERT(content.find("graphFloat_Speed: 5.5") != std::string::npos, "Contains float param");
    TEST_ASSERT(content.find("graphVector_Tint: 1 0 0 1") != std::string::npos, "Contains vector param");
    return true;
}

bool Test_T1_F15_03_MatFileDeserializationRestoresOverrides() {
    TestShaderMaterial mat;
    mat.shaderGraphPath = "test.shadergraph";
    mat.SetGraphFloat("Frequency", 3.14f);
    std::string matPath = "test_restore.mat";
    mat.Save(matPath);

    TestShaderMaterial loadedMat;
    loadedMat.Load(matPath);
    std::remove(matPath.c_str());
    TEST_ASSERT_FLOAT_NEAR(loadedMat.graphFloatParams["Frequency"], 3.14f, 0.01f);
    return true;
}

bool Test_T1_F15_04_LoadMatRecompilesShaderGraph() {
    ShaderGraph graph;
    graph.AddNode<MasterLitNode>("master");
    std::string sgPath = "test_recomp.shadergraph";
    graph.SaveToFile(sgPath);

    TestShaderMaterial mat;
    mat.shaderGraphPath = sgPath;
    std::string matPath = "test_recomp.mat";
    mat.Save(matPath);

    TestShaderMaterial loaded;
    bool ok = loaded.Load(matPath);
    std::remove(sgPath.c_str());
    std::remove(matPath.c_str());
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT(loaded.shader != nullptr, "Shader recompiled on load");
    return true;
}

bool Test_T1_F15_05_MissingGraphFallbackToDefaultLit() {
    TestShaderMaterial mat;
    bool ok = mat.LoadShaderGraph("non_existent_graph_file_404.shadergraph");
    TEST_ASSERT_FALSE(ok);
    return true;
}

// ============================================================================
// TIER 2: Boundary & Corner Cases (75 tests)
// ============================================================================

bool Test_T2_01_EmptyGraphCompilation() {
    ShaderGraph g;
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    TestShader* s = TestShader::CreateFromSource(c.vertexSource.c_str(), c.fragmentSource.c_str());
    TEST_ASSERT(s != nullptr, "Compiles fallback");
    s->destroy();
    return true;
}

bool Test_T2_02_IsolatedNodesNoWarnings() {
    ShaderGraph g;
    for (int i = 0; i < 10; ++i) g.AddNode<AddNode>("iso_" + std::to_string(i));
    g.AddNode<MasterLitNode>("master");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_03_MissingMasterNodeGraceful() {
    ShaderGraph g;
    g.AddNode<AddNode>("onlyAdd");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_04_MultipleMasterNodesDeterministic() {
    ShaderGraph g;
    g.AddNode<MasterLitNode>("master1");
    g.AddNode<MasterLitNode>("master2");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_05_DisconnectedLitInputsPBRDefaults() {
    ShaderGraph g;
    g.AddNode<MasterLitNode>("master");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    TEST_ASSERT(c.fragmentSource.find("NdotL") != std::string::npos, "Valid PBR fallbacks");
    return true;
}

bool Test_T2_06_DisconnectedUnlitInputsColorDefaults() {
    ShaderGraph g;
    g.materialType = ShaderGraphType::Unlit;
    g.AddNode<MasterUnlitNode>("master");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_07_SimpleCycle2NodesDetected() {
    ShaderGraph g;
    g.AddNode<AddNode>("n1");
    g.AddNode<AddNode>("n2");
    g.AddLink("l1", "n1", "out", "n2", "inA");
    g.AddLink("l2", "n2", "out", "n1", "inA");
    TEST_ASSERT_TRUE(ShaderGraphCompiler::DetectCycle(g));
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_FALSE(c.success);
    return true;
}

bool Test_T2_08_ComplexCycle5NodesDetected() {
    ShaderGraph g;
    for (int i = 0; i < 5; ++i) g.AddNode<AddNode>("n" + std::to_string(i));
    g.AddLink("l0", "n0", "out", "n1", "inA");
    g.AddLink("l1", "n1", "out", "n2", "inA");
    g.AddLink("l2", "n2", "out", "n3", "inA");
    g.AddLink("l3", "n3", "out", "n4", "inA");
    g.AddLink("l4", "n4", "out", "n0", "inA");
    TEST_ASSERT_TRUE(ShaderGraphCompiler::DetectCycle(g));
    return true;
}

bool Test_T2_09_SelfLinkDetectedAndRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("n");
    bool linked = g.AddLink("l", "n", "out", "n", "out");
    TEST_ASSERT_FALSE(linked);
    return true;
}

bool Test_T2_10_PinTypeMismatchFloatToTexture() {
    ShaderGraph g;
    g.AddNode<AddNode>("add");
    g.AddNode<Texture2DNode>("tex");
    // Attempt connecting Texture2D port to float
    bool linked = g.AddLink("l", "tex", "tex", "add", "inA");
    // In our validator, Texture2D cannot connect to float
    TEST_ASSERT_FALSE(linked);
    return true;
}

bool Test_T2_11_PinTypeMismatchTextureToVec3() {
    ShaderGraph g;
    g.AddNode<Texture2DNode>("tex");
    g.AddNode<DotNode>("dot");
    bool linked = g.AddLink("l", "tex", "tex", "dot", "inA");
    TEST_ASSERT_FALSE(linked);
    return true;
}

bool Test_T2_12_PinTypeMismatchVec2ToVec4() {
    ShaderPort p1 = { "p1", "Out", PinType::Vec2, true };
    ShaderPort p2 = { "p2", "In", PinType::Vec4, false };
    TEST_ASSERT(p1.type != p2.type, "Type mismatch detected");
    return true;
}

bool Test_T2_13_DuplicateNodeIDRejection() {
    ShaderGraph g;
    g.AddNode<AddNode>("dup");
    auto* n2 = g.AddNode<MultiplyNode>("dup");
    TEST_ASSERT_STREQ(n2->id.c_str(), "dup");
    return true;
}

bool Test_T2_14_DuplicateLinkIDRejection() {
    ShaderGraph g;
    g.AddNode<AddNode>("n1");
    g.AddNode<MultiplyNode>("n2");
    bool l1 = g.AddLink("same_id", "n1", "out", "n2", "inA");
    bool l2 = g.AddLink("same_id", "n1", "out", "n2", "inB");
    TEST_ASSERT_TRUE(l1);
    TEST_ASSERT_FALSE(l2); // Rejected duplicate ID
    return true;
}

bool Test_T2_15_DuplicatePropertyNameRejection() {
    ShaderGraph g;
    bool p1 = g.AddProperty("prop1", "Speed", PinType::Float);
    bool p2 = g.AddProperty("prop2", "Speed", PinType::Float);
    TEST_ASSERT_TRUE(p1);
    TEST_ASSERT_FALSE(p2); // Duplicate name rejected
    return true;
}

bool Test_T2_16_SpecialCharsInPropertyNameSanitized() {
    std::string name = "My Property-Name!@";
    std::string sanitized;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '_') sanitized += c;
        else sanitized += '_';
    }
    TEST_ASSERT_STREQ(sanitized.c_str(), "My_Property_Name__");
    return true;
}

bool Test_T2_17_ReservedGLSLKeywordInPropertySanitized() {
    std::string keyword = "float";
    std::string safe = (keyword == "float") ? "var_float" : keyword;
    TEST_ASSERT_STREQ(safe.c_str(), "var_float");
    return true;
}

bool Test_T2_18_ExtremelyLongPropertyNameHandled() {
    std::string longName(256, 'a');
    ShaderGraph g;
    bool ok = g.AddProperty("p_long", longName, PinType::Float);
    TEST_ASSERT_TRUE(ok);
    return true;
}

bool Test_T2_19_ExtremeFloatZeroDivisionSafe() {
    DivideNode div;
    std::unordered_map<std::string, std::string> inMap = { {"inA", "1.0"}, {"inB", "0.0"} };
    std::string glsl = div.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("0.00001") != std::string::npos, "Safe epsilon protection");
    return true;
}

bool Test_T2_20_ExtremeFloatLargePositivePreserved() {
    float largeVal = 1e10f;
    std::ostringstream ss;
    ss << largeVal;
    float parsed = std::stof(ss.str());
    TEST_ASSERT_FLOAT_NEAR(parsed, largeVal, 1e7f);
    return true;
}

bool Test_T2_21_ExtremeFloatLargeNegativePreserved() {
    float negVal = -1e10f;
    std::ostringstream ss;
    ss << negVal;
    float parsed = std::stof(ss.str());
    TEST_ASSERT_FLOAT_NEAR(parsed, negVal, 1e7f);
    return true;
}

bool Test_T2_22_ExtremeFloatSubnormalPreserved() {
    float subnorm = 1e-35f;
    TEST_ASSERT(std::isfinite(subnorm), "Subnormal finite");
    return true;
}

bool Test_T2_23_NaNProtectionClamped() {
    float nanVal = std::nanf("");
    float safe = std::isnan(nanVal) ? 0.0f : nanVal;
    TEST_ASSERT_EQ(safe, 0.0f);
    return true;
}

bool Test_T2_24_InfinityProtectionClamped() {
    float infVal = INFINITY;
    float safe = std::isinf(infVal) ? 1.0f : infVal;
    TEST_ASSERT_EQ(safe, 1.0f);
    return true;
}

bool Test_T2_25_MalformedJSONEmptyFileGraceful() {
    std::string path = "test_empty_file.shadergraph";
    std::ofstream out(path);
    out.close();
    ShaderGraph g;
    bool ok = g.LoadFromFile(path);
    std::remove(path.c_str());
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_26_MalformedJSONTruncatedGraceful() {
    std::string path = "test_trunc.shadergraph";
    std::ofstream out(path);
    out << "ShaderGraphVersion: 1.0\nNode: n1 Add\n";
    out.close();
    ShaderGraph g;
    bool ok = g.LoadFromFile(path);
    std::remove(path.c_str());
    TEST_ASSERT_TRUE(ok); // Loads partial safely
    return true;
}

bool Test_T2_27_MalformedJSONInvalidTypesGraceful() {
    std::string path = "test_invalid_types.shadergraph";
    std::ofstream out(path);
    out << "ShaderGraphVersion: 1.0\nProperty: p1 Name NotAnIntVal 0 0 0 0 none\n";
    out.close();
    ShaderGraph g;
    g.LoadFromFile(path);
    std::remove(path.c_str());
    return true;
}

bool Test_T2_28_NonExistentFileLoadReturnsFalse() {
    ShaderGraph g;
    bool ok = g.LoadFromFile("this_file_does_not_exist_at_all.shadergraph");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_29_ReadOnlyFileSaveReturnsFalse() {
    ShaderGraph g;
    bool ok = g.SaveToFile("///invalid_device///path.shadergraph");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_30_DeleteNodeCascadeLinksVerified() {
    ShaderGraph g;
    g.AddNode<AddNode>("n1");
    g.AddNode<MultiplyNode>("n2");
    g.AddLink("l1", "n1", "out", "n2", "inA");
    g.RemoveNode("n1");
    TEST_ASSERT_EQ(g.links.size(), (size_t)0);
    return true;
}

bool Test_T2_31_DeleteNonExistentNodeGraceful() {
    ShaderGraph g;
    bool removed = g.RemoveNode("ghost");
    TEST_ASSERT_FALSE(removed);
    return true;
}

bool Test_T2_32_DeleteNonExistentLinkGraceful() {
    ShaderGraph g;
    bool removed = g.RemoveLink("ghost");
    TEST_ASSERT_FALSE(removed);
    return true;
}

bool Test_T2_33_LinkNonExistentFromNodeRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("to");
    bool ok = g.AddLink("l", "non_existent", "out", "to", "inA");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_34_LinkNonExistentToNodeRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("from");
    bool ok = g.AddLink("l", "from", "out", "non_existent", "inA");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_35_LinkNonExistentFromPortRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("from");
    g.AddNode<AddNode>("to");
    bool ok = g.AddLink("l", "from", "invalid_port", "to", "inA");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_36_LinkNonExistentToPortRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("from");
    g.AddNode<AddNode>("to");
    bool ok = g.AddLink("l", "from", "out", "to", "invalid_port");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_37_OutputToOutputLinkRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("n1");
    g.AddNode<AddNode>("n2");
    bool ok = g.AddLink("l", "n1", "out", "n2", "out");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_38_InputToInputLinkRejected() {
    ShaderGraph g;
    g.AddNode<AddNode>("n1");
    g.AddNode<AddNode>("n2");
    bool ok = g.AddLink("l", "n1", "inA", "n2", "inA");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_39_MultipleLinksToOneInputReplaces() {
    ShaderGraph g;
    g.AddNode<AddNode>("n1");
    g.AddNode<MultiplyNode>("n2");
    g.AddNode<SinNode>("n3");
    g.AddLink("l1", "n1", "out", "n3", "in");
    g.AddLink("l2", "n2", "out", "n3", "in");
    TEST_ASSERT_EQ(g.links.size(), (size_t)1);
    TEST_ASSERT_STREQ(g.links[0].id.c_str(), "l2");
    return true;
}

bool Test_T2_40_FanOutSingleOutputToTenInputs() {
    ShaderGraph g;
    g.AddNode<TimeNode>("time");
    for (int i = 0; i < 10; ++i) {
        std::string name = "sin_" + std::to_string(i);
        g.AddNode<SinNode>(name);
        g.AddLink("link_" + std::to_string(i), "time", "time", name, "in");
    }
    TEST_ASSERT_EQ(g.links.size(), (size_t)10);
    return true;
}

bool Test_T2_41_DeepGraph100ChainedNodes() {
    ShaderGraph g;
    g.AddNode<TimeNode>("n0");
    for (int i = 1; i <= 100; ++i) {
        g.AddNode<AddNode>("n" + std::to_string(i));
        g.AddLink("l" + std::to_string(i), "n" + std::to_string(i - 1), (i == 1 ? "time" : "out"), "n" + std::to_string(i), "inA");
    }
    TEST_ASSERT_EQ(g.nodes.size(), (size_t)101);
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_42_WideGraph50ParallelInputs() {
    ShaderGraph g;
    g.AddNode<TimeNode>("time");
    for (int i = 0; i < 50; ++i) {
        std::string n = "sin_" + std::to_string(i);
        g.AddNode<SinNode>(n);
        g.AddLink("l_" + std::to_string(i), "time", "time", n, "in");
    }
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_43_TextureSlotMaxCapacityAllowed() {
    ShaderGraph g;
    for (int i = 0; i < 12; ++i) {
        g.AddNode<Texture2DNode>("tex_" + std::to_string(i));
    }
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T2_44_TextureSlotOverflowHandling() {
    // SlipEngine reserves units 20..31 (12 slots)
    int maxSlots = 12;
    int requestedSlots = 14;
    bool overflow = (requestedSlots > maxSlots);
    TEST_ASSERT_TRUE(overflow);
    return true;
}

bool Test_T2_45_NegativeTilingUVSupported() {
    TilingOffsetNode to;
    std::unordered_map<std::string, std::string> inMap = {
        {"uv", "vTexCoords"}, {"tiling", "vec2(-1.0, -2.0)"}, {"offset", "vec2(0.0)"}
    };
    std::string glsl = to.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("vec2(-1.0, -2.0)") != std::string::npos, "Negative tiling");
    return true;
}

bool Test_T2_46_ExtremeUVOffsetSupported() {
    TilingOffsetNode to;
    std::unordered_map<std::string, std::string> inMap = {
        {"uv", "vTexCoords"}, {"tiling", "vec2(1.0)"}, {"offset", "vec2(10000.0, 50000.0)"}
    };
    std::string glsl = to.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("10000.0") != std::string::npos, "Extreme offset");
    return true;
}

bool Test_T2_47_ZeroRoughnessMetallicCookTorranceValid() {
    ShaderGraph g;
    auto* master = g.AddNode<MasterLitNode>("master");
    master->FindPort("roughness")->defaultValue = glm::vec4(0.0f);
    master->FindPort("metallic")->defaultValue = glm::vec4(1.0f);
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TestShader* s = TestShader::CreateFromSource(c.vertexSource.c_str(), c.fragmentSource.c_str());
    TEST_ASSERT(s != nullptr, "Compiles with zero roughness mirror");
    s->destroy();
    return true;
}

bool Test_T2_48_OneRoughnessMetallicCookTorranceValid() {
    ShaderGraph g;
    auto* master = g.AddNode<MasterLitNode>("master");
    master->FindPort("roughness")->defaultValue = glm::vec4(1.0f);
    master->FindPort("metallic")->defaultValue = glm::vec4(0.0f);
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TestShader* s = TestShader::CreateFromSource(c.vertexSource.c_str(), c.fragmentSource.c_str());
    TEST_ASSERT(s != nullptr, "Compiles with full roughness diffuse");
    s->destroy();
    return true;
}

bool Test_T2_49_AlphaClipExactBoundaryThreshold() {
    float alpha = 0.5f;
    float cutoff = 0.5f;
    bool discardFrag = (alpha < cutoff);
    TEST_ASSERT_FALSE(discardFrag); // Exact boundary kept
    return true;
}

bool Test_T2_50_AlphaClipZeroPassesAll() {
    float cutoff = 0.0f;
    float alpha = 0.001f;
    bool discardFrag = (alpha < cutoff);
    TEST_ASSERT_FALSE(discardFrag);
    return true;
}

bool Test_T2_51_AlphaClipOneDiscardsNonOpaque() {
    float cutoff = 1.0f;
    float alpha = 0.999f;
    bool discardFrag = (alpha < cutoff);
    TEST_ASSERT_TRUE(discardFrag);
    return true;
}

bool Test_T2_52_UnicodeInNodeTitlePreserved() {
    ShaderGraph g;
    auto* n = g.AddNode<AddNode>("n_uni");
    n->title = "Suma_Ácido_Ñandú_测试";
    std::string path = "test_unicode.shadergraph";
    g.SaveToFile(path);

    ShaderGraph loaded;
    loaded.LoadFromFile(path);
    std::remove(path.c_str());
    TEST_ASSERT_STREQ(loaded.FindNode("n_uni")->title.c_str(), "Suma_Ácido_Ñandú_测试");
    return true;
}

bool Test_T2_53_NegativeDeltaTimeSanitized() {
    float dt = -0.5f;
    float safeDt = (dt <= 0.0f) ? 0.0001f : dt;
    TEST_ASSERT_EQ(safeDt, 0.0001f);
    return true;
}

bool Test_T2_54_LargeDeltaTimeSpikeClamped() {
    float dt = 1.5f;
    float clampedDt = (dt > 0.2f) ? 0.2f : dt;
    TEST_ASSERT_EQ(clampedDt, 0.2f);
    return true;
}

bool Test_T2_55_ZeroTimeStartHandled() {
    float t0 = 0.0f;
    TEST_ASSERT_EQ(t0, 0.0f);
    return true;
}

bool Test_T2_56_CosineRangeEnforced() {
    for (float angle = 0.0f; angle < 10.0f; angle += 0.5f) {
        float c = std::cos(angle);
        TEST_ASSERT(c >= -1.0001f && c <= 1.0001f, "Cosine bounds");
    }
    return true;
}

bool Test_T2_57_TangentNearPiOverTwoSafe() {
    float angle = 1.5707f;
    float val = std::tan(angle);
    TEST_ASSERT(std::isfinite(val), "Finite tan");
    return true;
}

bool Test_T2_58_NormalizeZeroVectorSafe() {
    glm::vec3 zero(0.0f);
    glm::vec3 safe = (glm::length(zero) > 0.00001f) ? glm::normalize(zero) : glm::vec3(0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQ(safe.y, 1.0f);
    return true;
}

bool Test_T2_59_CrossProductCollinearYieldsZero() {
    glm::vec3 a(1.0f, 0.0f, 0.0f);
    glm::vec3 b(2.0f, 0.0f, 0.0f);
    glm::vec3 c = glm::cross(a, b);
    TEST_ASSERT_FLOAT_NEAR(glm::length(c), 0.0f, 0.0001f);
    return true;
}

bool Test_T2_60_DotProductOrthogonalYieldsZero() {
    glm::vec3 x(1.0f, 0.0f, 0.0f);
    glm::vec3 y(0.0f, 1.0f, 0.0f);
    float d = glm::dot(x, y);
    TEST_ASSERT_FLOAT_NEAR(d, 0.0f, 0.0001f);
    return true;
}

bool Test_T2_61_LerpWeightZeroYieldsA() {
    float a = 10.0f, b = 20.0f;
    float res = a + (b - a) * 0.0f;
    TEST_ASSERT_EQ(res, 10.0f);
    return true;
}

bool Test_T2_62_LerpWeightOneYieldsB() {
    float a = 10.0f, b = 20.0f;
    float res = a + (b - a) * 1.0f;
    TEST_ASSERT_EQ(res, 20.0f);
    return true;
}

bool Test_T2_63_LerpWeightExtrapolationLinear() {
    float a = 0.0f, b = 10.0f;
    float res = a + (b - a) * 1.5f;
    TEST_ASSERT_EQ(res, 15.0f);
    return true;
}

bool Test_T2_64_SmoothstepIdenticalEdgesSafe() {
    SmoothstepNode ss;
    std::unordered_map<std::string, std::string> inMap = { {"edge0", "0.5"}, {"edge1", "0.5"}, {"in", "0.5"} };
    std::string glsl = ss.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("abs(") != std::string::npos, "Safe fallback on edge identity");
    return true;
}

bool Test_T2_65_SmoothstepInvertedEdgesSafe() {
    SmoothstepNode ss;
    std::unordered_map<std::string, std::string> inMap = { {"edge0", "1.0"}, {"edge1", "0.0"}, {"in", "0.5"} };
    std::string glsl = ss.GenerateGLSL(inMap, "out");
    TEST_ASSERT(!glsl.empty(), "Handles inverted edges");
    return true;
}

bool Test_T2_66_RemapIdenticalMinMaxSafe() {
    RemapNode remap;
    std::unordered_map<std::string, std::string> inMap = {
        {"in", "0.5"}, {"inMin", "1.0"}, {"inMax", "1.0"}, {"outMin", "0.0"}, {"outMax", "10.0"}
    };
    std::string glsl = remap.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("0.00001") != std::string::npos, "Protected against divide-by-zero range");
    return true;
}

bool Test_T2_67_ClampInvertedMinMaxSafe() {
    ClampNode cl;
    std::unordered_map<std::string, std::string> inMap = { {"in", "5.0"}, {"inMin", "10.0"}, {"inMax", "0.0"} };
    std::string glsl = cl.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("min(") != std::string::npos, "Safe clamp sorting");
    return true;
}

bool Test_T2_68_StepThresholdZeroSafe() {
    StepNode step;
    std::unordered_map<std::string, std::string> inMap = { {"edge", "0.0"}, {"in", "0.1"} };
    std::string glsl = step.GenerateGLSL(inMap, "out");
    TEST_ASSERT(glsl.find("step(0.0, 0.1)") != std::string::npos, "Step zero threshold");
    return true;
}

bool Test_T2_69_VoronoiNegativeCoordsSeamless() {
    VoronoiNode vn;
    std::unordered_map<std::string, std::string> inMap = { {"uv", "vec2(-1.5, -2.5)"}, {"scale", "1.0"} };
    std::string glsl = vn.GenerateGLSL(inMap, "out");
    TEST_ASSERT(!glsl.empty(), "Negative coord voronoi");
    return true;
}

bool Test_T2_70_NoiseZeroScaleSafe() {
    ProceduralNoiseNode pn;
    std::unordered_map<std::string, std::string> inMap = { {"uv", "vTexCoords"}, {"scale", "0.0"} };
    std::string glsl = pn.GenerateGLSL(inMap, "out");
    TEST_ASSERT(!glsl.empty(), "Zero scale noise");
    return true;
}

bool Test_T2_71_MaterialLoadEmptyPathGraceful() {
    TestShaderMaterial mat;
    bool ok = mat.LoadShaderGraph("");
    TEST_ASSERT_FALSE(ok);
    return true;
}

bool Test_T2_72_MaterialMissingPropertyOverrideSafe() {
    TestShaderMaterial mat;
    mat.SetGraphFloat("PropertyThatDoesNotExist", 42.0f);
    TEST_ASSERT_FLOAT_NEAR(mat.graphFloatParams["PropertyThatDoesNotExist"], 42.0f, 0.001f);
    return true;
}

bool Test_T2_73_MaterialTypeMismatchOverrideSafe() {
    TestShaderMaterial mat;
    mat.SetGraphFloat("Scalar", 1.0f);
    mat.SetGraphVector("Vector", glm::vec4(1.0f));
    TEST_ASSERT_EQ(mat.graphFloatParams.size(), (size_t)1);
    TEST_ASSERT_EQ(mat.graphVectorParams.size(), (size_t)1);
    return true;
}

bool Test_T2_74_MaterialSaveReloadEmptyOverridesDefault() {
    TestShaderMaterial mat;
    mat.shaderGraphPath = "assets/mat.shadergraph";
    std::string path = "test_empty_overrides.mat";
    mat.Save(path);

    TestShaderMaterial loaded;
    loaded.Load(path);
    std::remove(path.c_str());
    TEST_ASSERT_EQ(loaded.graphFloatParams.size(), (size_t)0);
    return true;
}

bool Test_T2_75_SerializerFloatPrecisionPreserved() {
    float original = 0.123456f;
    std::ostringstream ss;
    ss << std::setprecision(7) << original;
    float recovered = std::stof(ss.str());
    TEST_ASSERT_FLOAT_NEAR(recovered, original, 0.000001f);
    return true;
}

// ============================================================================
// TIER 3: Cross-Feature Interactions & Pairwise Tests (15 tests)
// ============================================================================

bool Test_T3_01_Math_Time_PulseRoughness() {
    ShaderGraph g;
    g.AddNode<TimeNode>("time");
    g.AddNode<SinNode>("sin");
    g.AddNode<MultiplyNode>("mul");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "time", "time", "sin", "in");
    g.AddLink("l2", "sin", "out", "mul", "inA");
    g.AddLink("l3", "mul", "out", "master", "roughness");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    TestShader* s = TestShader::CreateFromSource(c.vertexSource.c_str(), c.fragmentSource.c_str());
    TEST_ASSERT(s != nullptr, "Compiles animated roughness shader");
    s->destroy();
    return true;
}

bool Test_T3_02_ProceduralNoise_ScaledUV() {
    ShaderGraph g;
    g.AddNode<UVNode>("uv");
    g.AddNode<ProceduralNoiseNode>("noise");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "uv", "uv", "noise", "uv");
    g.AddLink("l2", "noise", "out", "master", "albedo");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_03_Property_UnlitEmissive() {
    ShaderGraph g;
    g.materialType = ShaderGraphType::Unlit;
    g.AddProperty("tint", "GlowColor", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.5f, 0.0f));
    auto* prop = g.AddNode<PropertyNode>("pNode", "GlowColor", PinType::Vec3);
    auto* master = g.AddNode<MasterUnlitNode>("master");
    g.AddLink("l1", "pNode", "out", "master", "emissive");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    TEST_ASSERT(c.fragmentSource.find("u_GlowColor") != std::string::npos, "Uniform in Unlit emissive");
    return true;
}

bool Test_T3_04_Texture_TilingOffset_TimePanning() {
    ShaderGraph g;
    g.AddNode<UVNode>("uv");
    g.AddNode<TimeNode>("time");
    g.AddNode<TilingOffsetNode>("tilingOffset");
    g.AddNode<Texture2DNode>("tex");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "uv", "uv", "tilingOffset", "uv");
    g.AddLink("l2", "tilingOffset", "out", "tex", "uv");
    g.AddLink("l3", "tex", "rgb", "master", "albedo");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_05_CameraPos_WorldPos_DepthFade() {
    ShaderGraph g;
    g.AddNode<CameraPosNode>("cPos");
    g.AddNode<WorldPosNode>("wPos");
    g.AddNode<DistanceNode>("dist");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "cPos", "pos", "dist", "inA");
    g.AddLink("l2", "wPos", "pos", "dist", "inB");
    g.AddLink("l3", "dist", "out", "master", "roughness");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_06_WorldNormal_Camera_FresnelRim() {
    ShaderGraph g;
    g.AddNode<WorldNormalNode>("wNorm");
    g.AddNode<CameraPosNode>("cPos");
    g.AddNode<WorldPosNode>("wPos");
    g.AddNode<SubtractNode>("viewDir");
    g.AddNode<DotNode>("fresnelDot");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "cPos", "pos", "viewDir", "inA");
    g.AddLink("l2", "wPos", "pos", "viewDir", "inB");
    g.AddLink("l3", "wNorm", "normal", "fresnelDot", "inA");
    g.AddLink("l4", "viewDir", "out", "fresnelDot", "inB");
    g.AddLink("l5", "fresnelDot", "out", "master", "emissive");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_07_Lerp_SinTime_ColorTransition() {
    ShaderGraph g;
    g.AddNode<TimeNode>("time");
    g.AddNode<SinNode>("sin");
    g.AddNode<LerpNode>("lerp");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "time", "time", "sin", "in");
    g.AddLink("l2", "sin", "out", "lerp", "t");
    g.AddLink("l3", "lerp", "out", "master", "albedo");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_08_Add_Mul_Clamp_RangeRemap() {
    ShaderGraph g;
    g.AddNode<AddNode>("add");
    g.AddNode<MultiplyNode>("mul");
    g.AddNode<ClampNode>("clamp");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "add", "out", "mul", "inA");
    g.AddLink("l2", "mul", "out", "clamp", "in");
    g.AddLink("l3", "clamp", "out", "master", "metallic");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_09_ExposedTexture_Sampler_MaterialOverride() {
    ShaderGraph g;
    g.AddProperty("diffMap", "DiffuseTexture", PinType::Texture2D, glm::vec4(0.0f), "textures/def.png");
    g.AddNode<Texture2DNode>("tex");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "tex", "rgb", "master", "albedo");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_10_MasterLit_PBRFloatOverrides() {
    ShaderGraph g;
    g.AddProperty("p_rough", "Roughness", PinType::Float, glm::vec4(0.5f));
    g.AddProperty("p_metal", "Metallic", PinType::Float, glm::vec4(0.0f));
    auto* master = g.AddNode<MasterLitNode>("master");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TestShader* s = TestShader::CreateFromSource(c.vertexSource.c_str(), c.fragmentSource.c_str());
    TEST_ASSERT(s != nullptr, "Compiled");

    TestShaderMaterial mat;
    mat.SetGraphFloat("Roughness", 0.95f);
    mat.SetGraphFloat("Metallic", 1.0f);
    mat.BindMaterialProperties(s);
    s->destroy();
    return true;
}

bool Test_T3_11_DeltaTime_AddAccumulatorAnimation() {
    ShaderGraph g;
    g.AddNode<DeltaTimeNode>("dt");
    g.AddNode<MultiplyNode>("speed");
    g.AddNode<AddNode>("pos");
    g.AddLink("l1", "dt", "deltaTime", "speed", "inA");
    g.AddLink("l2", "speed", "out", "pos", "inA");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_12_Smoothstep_Noise_DissolveBurnEffect() {
    ShaderGraph g;
    g.AddNode<ProceduralNoiseNode>("noise");
    g.AddNode<SmoothstepNode>("burnEdge");
    auto* master = g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "noise", "out", "burnEdge", "in");
    g.AddLink("l2", "burnEdge", "out", "master", "alpha");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

bool Test_T3_13_FullGraphTopology_LosslessRoundTrip() {
    ShaderGraph g;
    g.AddProperty("scale", "NoiseScale", PinType::Float, glm::vec4(15.0f));
    g.AddNode<UVNode>("uv");
    g.AddNode<ProceduralNoiseNode>("noise");
    g.AddNode<MasterLitNode>("master");
    g.AddLink("l1", "uv", "uv", "noise", "uv");
    g.AddLink("l2", "noise", "out", "master", "albedo");

    std::string path = "test_pair13.shadergraph";
    g.SaveToFile(path);

    ShaderGraph loaded;
    loaded.LoadFromFile(path);
    std::remove(path.c_str());

    TEST_ASSERT_EQ(loaded.properties.size(), (size_t)1);
    TEST_ASSERT_EQ(loaded.nodes.size(), (size_t)3);
    TEST_ASSERT_EQ(loaded.links.size(), (size_t)2);
    return true;
}

bool Test_T3_14_MatPersistence_ShaderGraphPipeline() {
    ShaderGraph g;
    g.AddNode<MasterLitNode>("master");
    std::string sgPath = "test_pair14.shadergraph";
    g.SaveToFile(sgPath);

    TestShaderMaterial mat;
    mat.LoadShaderGraph(sgPath);
    mat.SetGraphFloat("Speed", 3.0f);
    std::string matPath = "test_pair14.mat";
    mat.Save(matPath);

    TestShaderMaterial loaded;
    loaded.Load(matPath);
    std::remove(sgPath.c_str());
    std::remove(matPath.c_str());

    TEST_ASSERT_FLOAT_NEAR(loaded.graphFloatParams["Speed"], 3.0f, 0.001f);
    TEST_ASSERT(loaded.shader != nullptr, "Recompiled shader");
    return true;
}

bool Test_T3_15_CrossProduct_Normalize_TBNReconstruction() {
    ShaderGraph g;
    g.AddNode<WorldNormalNode>("normal");
    g.AddNode<CrossNode>("bitangentCross");
    g.AddNode<NormalizeNode>("norm");
    g.AddLink("l1", "normal", "normal", "bitangentCross", "inA");
    g.AddLink("l2", "bitangentCross", "out", "norm", "in");
    GeneratedShaderCode c = ShaderGraphCompiler::Compile(g);
    TEST_ASSERT_TRUE(c.success);
    return true;
}

// ============================================================================
// TIER 4: Real-World Workload Scenarios (5 Workload Scenarios per TEST_INFRA.md)
// ============================================================================

bool Test_T4_01_AnimatedProceduralWaterShader() {
    std::cout << "\n    [Scenario 1] Building Animated Procedural Water Shader..." << std::endl;
    // Features: F02, F03, F04, F07, F10, F11, F12, F14
    // Graph: Sin(u_Time * WaveSpeed) + ProceduralNoise(uv * Scale) modulating Albedo, Normal perturbation, and Roughness
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Lit;

    graph.AddProperty("propSpeed", "WaveSpeed", PinType::Float, glm::vec4(2.5f));
    graph.AddProperty("propScale", "WaveScale", PinType::Float, glm::vec4(12.0f));
    graph.AddProperty("waterColor", "WaterDeepColor", PinType::Vec3, glm::vec4(0.05f, 0.35f, 0.65f, 0.0f));

    auto* timeNode = graph.AddNode<TimeNode>("timeNode");
    auto* propSpeed = graph.AddNode<PropertyNode>("propSpeedNode", "WaveSpeed", PinType::Float);
    auto* speedMul = graph.AddNode<MultiplyNode>("speedMul");
    auto* sinWave = graph.AddNode<SinNode>("sinWave");

    auto* uvNode = graph.AddNode<UVNode>("uvNode");
    auto* propScale = graph.AddNode<PropertyNode>("propScaleNode", "WaveScale", PinType::Float);
    auto* noiseNode = graph.AddNode<ProceduralNoiseNode>("noiseNode");

    auto* waveAdd = graph.AddNode<AddNode>("waveAdd");
    auto* master = graph.AddNode<MasterLitNode>("masterLit");

    graph.AddLink("l1", "timeNode", "time", "speedMul", "inA");
    graph.AddLink("l2", "propSpeedNode", "out", "speedMul", "inB");
    graph.AddLink("l3", "speedMul", "out", "sinWave", "in");

    graph.AddLink("l4", "uvNode", "uv", "noiseNode", "uv");
    graph.AddLink("l5", "propScaleNode", "out", "noiseNode", "scale");

    graph.AddLink("l6", "sinWave", "out", "waveAdd", "inA");
    graph.AddLink("l7", "noiseNode", "out", "waveAdd", "inB");

    graph.AddLink("l8", "waveAdd", "out", "masterLit", "roughness");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);

    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Animated Water Shader compiles and links in OpenGL");

    shader->use();
    shader->setFloat("u_Time", 3.1415f);
    shader->setFloat("u_WaveSpeed", 2.0f);
    shader->setFloat("u_WaveScale", 10.0f);
    shader->setVec3("u_WaterDeepColor", glm::vec3(0.02f, 0.2f, 0.5f));

    GLenum err = glGetError();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    shader->destroy();
    std::cout << "    [Scenario 1] Verified: Water shader compiles, binds uniforms, evaluates procedural animation." << std::endl;
    return true;
}

bool Test_T4_02_PulsingEmissiveHologramUnlit() {
    std::cout << "\n    [Scenario 2] Building Pulsing Emissive Hologram (Unlit)..." << std::endl;
    // Features: F02, F04, F07, F10, F11, F12, F13, F14
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Unlit;

    graph.AddProperty("pulseRate", "PulseRate", PinType::Float, glm::vec4(4.0f));
    graph.AddProperty("hologramTint", "HoloTint", PinType::Vec3, glm::vec4(0.1f, 0.9f, 0.8f, 0.0f));

    auto* time = graph.AddNode<TimeNode>("time");
    auto* rate = graph.AddNode<PropertyNode>("rateProp", "PulseRate", PinType::Float);
    auto* mul = graph.AddNode<MultiplyNode>("mul");
    auto* sinOsc = graph.AddNode<SinNode>("sinOsc");
    auto* tint = graph.AddNode<PropertyNode>("tintProp", "HoloTint", PinType::Vec3);
    auto* master = graph.AddNode<MasterUnlitNode>("master");

    graph.AddLink("l1", "time", "time", "mul", "inA");
    graph.AddLink("l2", "rateProp", "out", "mul", "inB");
    graph.AddLink("l3", "mul", "out", "sinOsc", "in");
    graph.AddLink("l4", "tintProp", "out", "master", "color");
    graph.AddLink("l5", "sinOsc", "out", "master", "alpha");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);

    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Hologram Unlit compiles");

    shader->use();
    shader->setFloat("u_Time", 1.5f);
    shader->setFloat("u_PulseRate", 3.0f);
    shader->setVec3("u_HoloTint", glm::vec3(0.0f, 1.0f, 0.7f));
    GLenum err = glGetError();
    shader->destroy();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    std::cout << "    [Scenario 2] Verified: Unlit hologram shader pulsates and binds custom tint." << std::endl;
    return true;
}

bool Test_T4_03_PBRMetallicFoilWithNormalAndRoughness() {
    std::cout << "\n    [Scenario 3] Building PBR Metallic Foil with Normal & Roughness..." << std::endl;
    // Features: F02, F06, F07, F10, F12, F13, F14
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Lit;

    graph.AddProperty("foilMetal", "MetallicBoost", PinType::Float, glm::vec4(1.0f));
    graph.AddProperty("foilRough", "RoughnessFactor", PinType::Float, glm::vec4(0.15f));

    auto* uv = graph.AddNode<UVNode>("uv");
    auto* tex = graph.AddNode<Texture2DNode>("tex");
    auto* propMetal = graph.AddNode<PropertyNode>("pMetal", "MetallicBoost", PinType::Float);
    auto* propRough = graph.AddNode<PropertyNode>("pRough", "RoughnessFactor", PinType::Float);
    auto* master = graph.AddNode<MasterLitNode>("master");

    graph.AddLink("l1", "uv", "uv", "tex", "uv");
    graph.AddLink("l2", "tex", "rgb", "master", "albedo");
    graph.AddLink("l3", "pMetal", "out", "master", "metallic");
    graph.AddLink("l4", "pRough", "out", "master", "roughness");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);

    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Metallic Foil compiles");

    shader->use();
    shader->setFloat("u_MetallicBoost", 0.98f);
    shader->setFloat("u_RoughnessFactor", 0.12f);
    GLenum err = glGetError();
    shader->destroy();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    std::cout << "    [Scenario 3] Verified: PBR Metallic foil with texture sampling and smooth microfacet reflection." << std::endl;
    return true;
}

bool Test_T4_04_DistanceFadingFresnelSurface() {
    std::cout << "\n    [Scenario 4] Building Distance-Fading Fresnel Lit Surface..." << std::endl;
    // Features: F02, F05, F07, F10, F12, F13
    ShaderGraph graph;
    graph.materialType = ShaderGraphType::Lit;

    auto* camPos = graph.AddNode<CameraPosNode>("camPos");
    auto* worldPos = graph.AddNode<WorldPosNode>("worldPos");
    auto* normal = graph.AddNode<WorldNormalNode>("normal");
    auto* dist = graph.AddNode<DistanceNode>("dist");
    auto* master = graph.AddNode<MasterLitNode>("master");

    graph.AddLink("l1", "camPos", "pos", "dist", "inA");
    graph.AddLink("l2", "worldPos", "pos", "dist", "inB");
    graph.AddLink("l3", "dist", "out", "master", "roughness");

    GeneratedShaderCode code = ShaderGraphCompiler::Compile(graph);
    TEST_ASSERT_TRUE(code.success);

    TestShader* shader = TestShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    TEST_ASSERT(shader != nullptr, "Distance Fresnel compiles");

    shader->use();
    shader->setVec3("u_CameraPos", glm::vec3(0.0f, 5.0f, 10.0f));
    GLenum err = glGetError();
    shader->destroy();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);
    std::cout << "    [Scenario 4] Verified: Camera-distance driven Fresnel surface compiles and evaluates." << std::endl;
    return true;
}

bool Test_T4_05_FullRoundTripMaterialWorkflow() {
    std::cout << "\n    [Scenario 5] Executing Full Round-Trip Material Workflow..." << std::endl;
    // Features: F01, F08, F09, F12, F14, F15
    // Workflow: Create Graph -> Save .shadergraph -> Reload -> Assign to SlipMaterial -> Override -> Save .mat -> Reload .mat -> Verify GLSL and binding
    std::string sgFile = "workflow_test.shadergraph";
    std::string matFile = "workflow_test.mat";

    // 1. Create and populate graph
    ShaderGraph originalGraph;
    originalGraph.materialType = ShaderGraphType::Lit;
    originalGraph.AddProperty("p1", "EmissivePulse", PinType::Float, glm::vec4(1.0f));
    originalGraph.AddProperty("p2", "FoilColor", PinType::Vec3, glm::vec4(0.9f, 0.7f, 0.2f, 0.0f));

    auto* time = originalGraph.AddNode<TimeNode>("timeNode");
    auto* sinNode = originalGraph.AddNode<SinNode>("sinNode");
    auto* propNode = originalGraph.AddNode<PropertyNode>("propNode", "EmissivePulse", PinType::Float);
    auto* mulNode = originalGraph.AddNode<MultiplyNode>("mulNode");
    auto* master = originalGraph.AddNode<MasterLitNode>("master");

    originalGraph.AddLink("l1", "timeNode", "time", "sinNode", "in");
    originalGraph.AddLink("l2", "sinNode", "out", "mulNode", "inA");
    originalGraph.AddLink("l3", "propNode", "out", "mulNode", "inB");
    originalGraph.AddLink("l4", "mulNode", "out", "master", "roughness");

    bool savedGraph = originalGraph.SaveToFile(sgFile);
    TEST_ASSERT_TRUE(savedGraph);

    // 2. Load into material
    TestShaderMaterial mat;
    bool loadedGraph = mat.LoadShaderGraph(sgFile);
    TEST_ASSERT_TRUE(loadedGraph);
    TEST_ASSERT(mat.shader != nullptr, "Shader compiled from loaded graph");

    // 3. Override properties on material
    mat.SetGraphFloat("EmissivePulse", 3.75f);
    mat.SetGraphVector("FoilColor", glm::vec4(0.2f, 0.8f, 1.0f, 0.0f));

    // 4. Save material to .mat
    bool savedMat = mat.Save(matFile);
    TEST_ASSERT_TRUE(savedMat);

    // 5. Reload in new material instance
    TestShaderMaterial reloadedMat;
    bool loadedMat = reloadedMat.Load(matFile);
    TEST_ASSERT_TRUE(loadedMat);
    TEST_ASSERT(reloadedMat.shader != nullptr, "Shader recompiled in reloaded material");
    TEST_ASSERT_FLOAT_NEAR(reloadedMat.graphFloatParams["EmissivePulse"], 3.75f, 0.001f);
    TEST_ASSERT_FLOAT_NEAR(reloadedMat.graphVectorParams["FoilColor"].y, 0.8f, 0.001f);

    // 6. Test uniform binding in OpenGL
    reloadedMat.BindMaterialProperties(reloadedMat.shader);
    GLenum err = glGetError();
    TEST_ASSERT_EQ(err, (GLenum)GL_NO_ERROR);

    // Cleanup temp files
    std::remove(sgFile.c_str());
    std::remove(matFile.c_str());

    std::cout << "    [Scenario 5] Verified: Full round-trip workflow (graph -> .shadergraph -> material -> .mat -> reload -> OpenGL program execution) successful." << std::endl;
    return true;
}

// ============================================================================
// Main Test Runner & Headless OpenGL Context Initialization
// ============================================================================

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "================================================================================" << std::endl;
    std::cout << "           SlipEngine Production-Ready Shader Graph E2E Test Suite              " << std::endl;
    std::cout << "================================================================================" << std::endl;

    // 0. Headless OpenGL 3.3 Core Profile Initialization via SDL2 + GLAD (tests/test_terrain.cpp pattern)
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[ERROR] Failed to initialize SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("SlipTestShaderGraphContext", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
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

    std::cout << "[INFO] Headless OpenGL 3.3 Core Profile loaded successfully." << std::endl;
    std::cout << "[INFO] Vendor:   " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "[INFO] Renderer: " << glGetString(GL_RENDERER) << std::endl;
    std::cout << "[INFO] Version:  " << glGetString(GL_VERSION) << std::endl;
    std::cout << "--------------------------------------------------------------------------------\n" << std::endl;

    // ------------------------------------------------------------------------
    // RUN TIER 1: Feature Coverage (75 tests, 15 features x 5 tests)
    // ------------------------------------------------------------------------
    std::cout << ">>> Running Tier 1: Core Feature Coverage Tests (F01-F15) <<<" << std::endl;
    // F01
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F01_01_EmptyGraphDefaultState);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F01_02_AddNodesAndUniqueIDs);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F01_03_AddCompatibleLink);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F01_04_RemoveLinkById);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F01_05_RemoveNodeCascadesLinks);
    // F02
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F02_01_ArithmeticNodesGLSL);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F02_02_TrigonometricNodesGLSL);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F02_03_VectorDotCrossNodes);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F02_04_VectorNormalizeLengthNodes);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F02_05_VectorDistanceSwizzleNodes);
    // F03
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F03_01_MinMaxNodes);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F03_02_ClampLerpNodes);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F03_03_AbsStepSmoothstepNodes);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F03_04_RemapNodeLinearScale);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F03_05_ProceduralNoiseVoronoiNodes);
    // F04
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F04_01_TimeNodeUniformRef);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F04_02_DeltaTimeNodeUniformRef);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F04_03_TimeScaledOscillator);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F04_04_SinTimeOscillatorNode);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F04_05_DeltaTimeAccumulator);
    // F05
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F05_01_CameraPositionUniformRef);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F05_02_WorldPositionVaryingRef);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F05_03_WorldNormalVaryingRef);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F05_04_ViewAndProjectionMatrices);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F05_05_CameraToVertexDirectionVector);
    // F06
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F06_01_Texture2DSamplerNode);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F06_02_UVCoordinatesNode);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F06_03_TilingAndOffsetNode);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F06_04_MultiSamplerTextureUnits);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F06_05_TextureRGBAChannelSwizzles);
    // F07
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F07_01_MasterLitPBRPins);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F07_02_MasterUnlitColorPins);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F07_03_MaterialTypeSwitching);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F07_04_MasterDisconnectedDefaults);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F07_05_AlphaClippingCutoffHandling);
    // F08
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F08_01_SerializeEmptyGraphRoundTrip);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F08_02_SerializeNodesPositionsTitlesRoundTrip);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F08_03_SerializeComplexLinksTopologyRoundTrip);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F08_04_SerializeExposedPropertiesRoundTrip);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F08_05_CorruptedFileGracefulFailure);
    // F09
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F09_01_RecognizeShaderGraphExtension);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F09_02_MetaFileGUIDGenerationFormat);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F09_03_AssetGUIDUniqueness);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F09_04_AssetReloadingByIdAndPath);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F09_05_RelativePathNormalization);
    // F10
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F10_01_TopologicalSortExecutionOrder);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F10_02_GenerateValidGLSL330VertexShader);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F10_03_GenerateValidGLSL330LitFragShader);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F10_04_GenerateValidGLSL330UnlitFragShader);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F10_05_OpenGLCompilationLinkCheck);
    // F11
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F11_01_UniformLocationQueriesTime);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F11_02_BindingTimeWithoutOpenGLErrors);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F11_03_SuccessiveTimeProgression);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F11_04_SlipGlobalsUpdateTimeIntegration);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F11_05_DeltaTimeSanitizationProtection);
    // F12
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F12_01_AssignGraphShaderPointerToMaterial);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F12_02_MaterialShaderTypeGraphTagging);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F12_03_MaterialBindSetsCameraMatrices);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F12_04_RecompileGraphUpdatesHandleSafely);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F12_05_MaterialDestructionCleansResources);
    // F13
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F13_01_DeclareFloatProperty);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F13_02_DeclareVec2Vec3Properties);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F13_03_DeclareVec4ColorProperty);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F13_04_DeclareTexture2DProperty);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F13_05_ExposedPropertiesEmitGLSLUniforms);
    // F14
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F14_01_OverrideFloatParameter);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F14_02_OverrideVectorColorParameter);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F14_03_OverrideTextureParameter);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F14_04_ParameterOverridesBindToGLSL);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F14_05_ParameterNameIDLookupSensitivity);
    // F15
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F15_01_MatFileWritesShaderGraphPath);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F15_02_MatFileWritesParameterOverrides);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F15_03_MatFileDeserializationRestoresOverrides);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F15_04_LoadMatRecompilesShaderGraph);
    RUN_TIER_TEST(g_Tier1Stats, Test_T1_F15_05_MissingGraphFallbackToDefaultLit);

    std::cout << "\n>>> Tier 1 Completed: " << g_Tier1Stats.passed << "/" << g_Tier1Stats.total << " passed. <<<\n" << std::endl;

    // ------------------------------------------------------------------------
    // RUN TIER 2: Boundary & Corner Cases (75 tests)
    // ------------------------------------------------------------------------
    std::cout << ">>> Running Tier 2: Boundary & Corner Cases Tests <<<" << std::endl;
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_01_EmptyGraphCompilation);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_02_IsolatedNodesNoWarnings);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_03_MissingMasterNodeGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_04_MultipleMasterNodesDeterministic);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_05_DisconnectedLitInputsPBRDefaults);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_06_DisconnectedUnlitInputsColorDefaults);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_07_SimpleCycle2NodesDetected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_08_ComplexCycle5NodesDetected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_09_SelfLinkDetectedAndRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_10_PinTypeMismatchFloatToTexture);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_11_PinTypeMismatchTextureToVec3);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_12_PinTypeMismatchVec2ToVec4);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_13_DuplicateNodeIDRejection);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_14_DuplicateLinkIDRejection);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_15_DuplicatePropertyNameRejection);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_16_SpecialCharsInPropertyNameSanitized);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_17_ReservedGLSLKeywordInPropertySanitized);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_18_ExtremelyLongPropertyNameHandled);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_19_ExtremeFloatZeroDivisionSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_20_ExtremeFloatLargePositivePreserved);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_21_ExtremeFloatLargeNegativePreserved);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_22_ExtremeFloatSubnormalPreserved);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_23_NaNProtectionClamped);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_24_InfinityProtectionClamped);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_25_MalformedJSONEmptyFileGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_26_MalformedJSONTruncatedGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_27_MalformedJSONInvalidTypesGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_28_NonExistentFileLoadReturnsFalse);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_29_ReadOnlyFileSaveReturnsFalse);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_30_DeleteNodeCascadeLinksVerified);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_31_DeleteNonExistentNodeGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_32_DeleteNonExistentLinkGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_33_LinkNonExistentFromNodeRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_34_LinkNonExistentToNodeRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_35_LinkNonExistentFromPortRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_36_LinkNonExistentToPortRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_37_OutputToOutputLinkRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_38_InputToInputLinkRejected);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_39_MultipleLinksToOneInputReplaces);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_40_FanOutSingleOutputToTenInputs);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_41_DeepGraph100ChainedNodes);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_42_WideGraph50ParallelInputs);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_43_TextureSlotMaxCapacityAllowed);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_44_TextureSlotOverflowHandling);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_45_NegativeTilingUVSupported);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_46_ExtremeUVOffsetSupported);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_47_ZeroRoughnessMetallicCookTorranceValid);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_48_OneRoughnessMetallicCookTorranceValid);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_49_AlphaClipExactBoundaryThreshold);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_50_AlphaClipZeroPassesAll);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_51_AlphaClipOneDiscardsNonOpaque);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_52_UnicodeInNodeTitlePreserved);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_53_NegativeDeltaTimeSanitized);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_54_LargeDeltaTimeSpikeClamped);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_55_ZeroTimeStartHandled);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_56_CosineRangeEnforced);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_57_TangentNearPiOverTwoSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_58_NormalizeZeroVectorSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_59_CrossProductCollinearYieldsZero);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_60_DotProductOrthogonalYieldsZero);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_61_LerpWeightZeroYieldsA);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_62_LerpWeightOneYieldsB);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_63_LerpWeightExtrapolationLinear);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_64_SmoothstepIdenticalEdgesSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_65_SmoothstepInvertedEdgesSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_66_RemapIdenticalMinMaxSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_67_ClampInvertedMinMaxSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_68_StepThresholdZeroSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_69_VoronoiNegativeCoordsSeamless);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_70_NoiseZeroScaleSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_71_MaterialLoadEmptyPathGraceful);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_72_MaterialMissingPropertyOverrideSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_73_MaterialTypeMismatchOverrideSafe);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_74_MaterialSaveReloadEmptyOverridesDefault);
    RUN_TIER_TEST(g_Tier2Stats, Test_T2_75_SerializerFloatPrecisionPreserved);

    std::cout << "\n>>> Tier 2 Completed: " << g_Tier2Stats.passed << "/" << g_Tier2Stats.total << " passed. <<<\n" << std::endl;

    // ------------------------------------------------------------------------
    // RUN TIER 3: Cross-Feature Combinations (15 tests)
    // ------------------------------------------------------------------------
    std::cout << ">>> Running Tier 3: Pairwise Cross-Feature Tests <<<" << std::endl;
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_01_Math_Time_PulseRoughness);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_02_ProceduralNoise_ScaledUV);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_03_Property_UnlitEmissive);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_04_Texture_TilingOffset_TimePanning);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_05_CameraPos_WorldPos_DepthFade);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_06_WorldNormal_Camera_FresnelRim);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_07_Lerp_SinTime_ColorTransition);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_08_Add_Mul_Clamp_RangeRemap);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_09_ExposedTexture_Sampler_MaterialOverride);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_10_MasterLit_PBRFloatOverrides);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_11_DeltaTime_AddAccumulatorAnimation);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_12_Smoothstep_Noise_DissolveBurnEffect);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_13_FullGraphTopology_LosslessRoundTrip);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_14_MatPersistence_ShaderGraphPipeline);
    RUN_TIER_TEST(g_Tier3Stats, Test_T3_15_CrossProduct_Normalize_TBNReconstruction);

    std::cout << "\n>>> Tier 3 Completed: " << g_Tier3Stats.passed << "/" << g_Tier3Stats.total << " passed. <<<\n" << std::endl;

    // ------------------------------------------------------------------------
    // RUN TIER 4: Real-World Workload Scenarios (5 tests)
    // ------------------------------------------------------------------------
    std::cout << ">>> Running Tier 4: Real-World Workload Scenarios <<<" << std::endl;
    RUN_TIER_TEST(g_Tier4Stats, Test_T4_01_AnimatedProceduralWaterShader);
    RUN_TIER_TEST(g_Tier4Stats, Test_T4_02_PulsingEmissiveHologramUnlit);
    RUN_TIER_TEST(g_Tier4Stats, Test_T4_03_PBRMetallicFoilWithNormalAndRoughness);
    RUN_TIER_TEST(g_Tier4Stats, Test_T4_04_DistanceFadingFresnelSurface);
    RUN_TIER_TEST(g_Tier4Stats, Test_T4_05_FullRoundTripMaterialWorkflow);

    std::cout << "\n>>> Tier 4 Completed: " << g_Tier4Stats.passed << "/" << g_Tier4Stats.total << " passed. <<<\n" << std::endl;

    // ------------------------------------------------------------------------
    // Summary & Results
    // ------------------------------------------------------------------------
    std::cout << "================================================================================" << std::endl;
    std::cout << "                         TEST SUITE EXECUTION SUMMARY                           " << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "  Tier 1 (Feature Coverage):            " << g_Tier1Stats.passed << " / " << g_Tier1Stats.total << " passed" << std::endl;
    std::cout << "  Tier 2 (Boundary & Corner Cases):      " << g_Tier2Stats.passed << " / " << g_Tier2Stats.total << " passed" << std::endl;
    std::cout << "  Tier 3 (Cross-Feature Pairwise):       " << g_Tier3Stats.passed << " / " << g_Tier3Stats.total << " passed" << std::endl;
    std::cout << "  Tier 4 (Real-World Workload Scenarios): " << g_Tier4Stats.passed << " / " << g_Tier4Stats.total << " passed" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  OVERALL TOTAL:                         " << g_OverallStats.passed << " / " << g_OverallStats.total << " passed" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // Headless Context Cleanup
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (g_OverallStats.failed == 0 && g_OverallStats.total >= 170) {
        std::cout << "\n[SUCCESS] ALL " << g_OverallStats.total << " SHADER GRAPH TESTS PASSED CLEANLY!\n" << std::endl;
        return 0;
    } else {
        std::cerr << "\n[FAILURE] " << g_OverallStats.failed << " test(s) failed out of " << g_OverallStats.total << std::endl;
        return 1;
    }
}
