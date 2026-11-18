#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace SlipShaderGraph {

    // --- Helper Implementations for ShaderGraphTypes ---
    const char* PinTypeToString(PinType type) {
        switch (type) {
            case PinType::Float:     return "Float";
            case PinType::Vec2:      return "Vec2";
            case PinType::Vec3:      return "Vec3";
            case PinType::Vec4:      return "Vec4";
            case PinType::Texture2D: return "Texture2D";
            case PinType::Matrix4:   return "Matrix4";
            case PinType::Boolean:   return "Boolean";
            default:                 return "Float";
        }
    }

    PinType StringToPinType(const std::string& str) {
        if (str == "Float")     return PinType::Float;
        if (str == "Vec2")      return PinType::Vec2;
        if (str == "Vec3")      return PinType::Vec3;
        if (str == "Vec4")      return PinType::Vec4;
        if (str == "Texture2D") return PinType::Texture2D;
        if (str == "Matrix4")   return PinType::Matrix4;
        if (str == "Boolean")   return PinType::Boolean;
        return PinType::Float;
    }

    const char* GetPinTypeGLSLName(PinType type) {
        switch (type) {
            case PinType::Float:     return "float";
            case PinType::Vec2:      return "vec2";
            case PinType::Vec3:      return "vec3";
            case PinType::Vec4:      return "vec4";
            case PinType::Texture2D: return "sampler2D";
            case PinType::Matrix4:   return "mat4";
            case PinType::Boolean:   return "bool";
            default:                 return "float";
        }
    }

    int GetPinTypeDimension(PinType type) {
        switch (type) {
            case PinType::Float: return 1;
            case PinType::Vec2:  return 2;
            case PinType::Vec3:  return 3;
            case PinType::Vec4:  return 4;
            default:             return 0;
        }
    }

    bool ArePinsCompatible(PinType fromType, PinType toType) {
        if (fromType == toType) return true;
        if (fromType == PinType::Texture2D || toType == PinType::Texture2D) return false;
        if (fromType == PinType::Matrix4 || toType == PinType::Matrix4) return false;
        int d1 = GetPinTypeDimension(fromType);
        int d2 = GetPinTypeDimension(toType);
        return (d1 >= 1 && d1 <= 4 && d2 >= 1 && d2 <= 4);
    }

    std::string FormatGLSLValue(PinType type, const glm::vec4& val) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4);
        switch (type) {
            case PinType::Float:
                ss << val.x;
                return ss.str();
            case PinType::Vec2:
                ss << "vec2(" << val.x << ", " << val.y << ")";
                return ss.str();
            case PinType::Vec3:
                ss << "vec3(" << val.x << ", " << val.y << ", " << val.z << ")";
                return ss.str();
            case PinType::Vec4:
                ss << "vec4(" << val.x << ", " << val.y << ", " << val.z << ", " << val.w << ")";
                return ss.str();
            default:
                return "0.0";
        }
    }

    std::string ConvertGLSLExpression(PinType fromType, PinType toType, const std::string& expr) {
        if (fromType == toType || expr.empty()) return expr;
        int dFrom = GetPinTypeDimension(fromType);
        int dTo = GetPinTypeDimension(toType);
        if (dFrom == 0 || dTo == 0) return expr;

        if (dFrom == 1) {
            if (dTo == 2) return "vec2(" + expr + ")";
            if (dTo == 3) return "vec3(" + expr + ")";
            if (dTo == 4) return "vec4(" + expr + ")";
        } else if (dFrom == 2) {
            if (dTo == 1) return "(" + expr + ").x";
            if (dTo == 3) return "vec3(" + expr + ", 0.0)";
            if (dTo == 4) return "vec4(" + expr + ", 0.0, 1.0)";
        } else if (dFrom == 3) {
            if (dTo == 1) return "(" + expr + ").x";
            if (dTo == 2) return "(" + expr + ").xy";
            if (dTo == 4) return "vec4(" + expr + ", 1.0)";
        } else if (dFrom == 4) {
            if (dTo == 1) return "(" + expr + ").x";
            if (dTo == 2) return "(" + expr + ").xy";
            if (dTo == 3) return "(" + expr + ").xyz";
        }
        return expr;
    }

    const char* ShaderGraphTypeToString(ShaderGraphType type) {
        return (type == ShaderGraphType::Lit) ? "Lit" : "Unlit";
    }

    ShaderGraphType StringToShaderGraphType(const std::string& str) {
        return (str == "Unlit") ? ShaderGraphType::Unlit : ShaderGraphType::Lit;
    }

    // --- ShaderNode Base Methods ---
    const ShaderPort* ShaderNode::FindInput(const std::string& portId) const {
        for (const auto& port : inputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    ShaderPort* ShaderNode::FindInput(const std::string& portId) {
        for (auto& port : inputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    const ShaderPort* ShaderNode::FindOutput(const std::string& portId) const {
        for (const auto& port : outputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    ShaderPort* ShaderNode::FindOutput(const std::string& portId) {
        for (auto& port : outputs) {
            if (port.id == portId) return &port;
        }
        return nullptr;
    }

    ShaderPort& ShaderNode::AddInput(const std::string& portId, const std::string& name, PinType type, const glm::vec4& defaultVal) {
        inputs.emplace_back(portId, name, type, false, defaultVal);
        return inputs.back();
    }

    ShaderPort& ShaderNode::AddOutput(const std::string& portId, const std::string& name, PinType type) {
        outputs.emplace_back(portId, name, type, true, glm::vec4(0.0f));
        return outputs.back();
    }

    void ShaderNode::GenerateGLSL(
        const std::unordered_map<std::string, std::string>& inputExpressions,
        std::unordered_map<std::string, std::string>& outputExpressions,
        std::string& statements
    ) const {
        (void)inputExpressions;
        (void)outputExpressions;
        (void)statements;
    }

    // --- ShaderNodeRegistry Singleton ---
    ShaderNodeRegistry::ShaderNodeRegistry() {
        InitializeDefaultNodes();
    }

    ShaderNodeRegistry& ShaderNodeRegistry::Get() {
        static ShaderNodeRegistry instance;
        return instance;
    }

    std::unique_ptr<ShaderNode> ShaderNodeRegistry::CreateNode(const std::string& typeName) {
        return Get().Instantiate(typeName);
    }

    void ShaderNodeRegistry::Register(const NodeDescriptor& desc) {
        auto it = m_NameToIndex.find(desc.typeName);
        if (it != m_NameToIndex.end()) {
            m_Descriptors[it->second] = desc;
        } else {
            m_NameToIndex[desc.typeName] = m_Descriptors.size();
            m_Descriptors.push_back(desc);
        }
    }

    void ShaderNodeRegistry::RegisterAlias(const std::string& alias, const std::string& canonicalTypeName) {
        m_Aliases[alias] = canonicalTypeName;
    }

    std::unique_ptr<ShaderNode> ShaderNodeRegistry::Instantiate(const std::string& typeName) const {
        auto it = m_NameToIndex.find(typeName);
        if (it != m_NameToIndex.end()) {
            return m_Descriptors[it->second].factory();
        }

        auto aIt = m_Aliases.find(typeName);
        if (aIt != m_Aliases.end()) {
            auto canIt = m_NameToIndex.find(aIt->second);
            if (canIt != m_NameToIndex.end()) {
                return m_Descriptors[canIt->second].factory();
            }
        }

        return nullptr;
    }

    const std::vector<NodeDescriptor>& ShaderNodeRegistry::GetRegisteredNodes() const {
        return m_Descriptors;
    }

    const NodeDescriptor* ShaderNodeRegistry::FindDescriptor(const std::string& typeName) const {
        auto it = m_NameToIndex.find(typeName);
        if (it != m_NameToIndex.end()) {
            return &m_Descriptors[it->second];
        }

        auto aIt = m_Aliases.find(typeName);
        if (aIt != m_Aliases.end()) {
            auto canIt = m_NameToIndex.find(aIt->second);
            if (canIt != m_NameToIndex.end()) {
                return &m_Descriptors[canIt->second];
            }
        }

        return nullptr;
    }

    void ShaderNodeRegistry::InitializeDefaultNodes() {
        if (m_Initialized) return;
        m_Initialized = true;

        // Math
        RegisterNodeType<MathAddNode>("Math_Add", "Add", "Math", "Sum of two values (A + B)");
        RegisterAlias("Add", "Math_Add");
        RegisterAlias("MathAddNode", "Math_Add");

        RegisterNodeType<MathSubtractNode>("Math_Subtract", "Subtract", "Math", "Difference between two values (A - B)");
        RegisterAlias("Subtract", "Math_Subtract");
        RegisterAlias("MathSubtractNode", "Math_Subtract");

        RegisterNodeType<MathMultiplyNode>("Math_Multiply", "Multiply", "Math", "Product of two values (A * B)");
        RegisterAlias("Multiply", "Math_Multiply");
        RegisterAlias("MathMultiplyNode", "Math_Multiply");

        RegisterNodeType<MathDivideNode>("Math_Divide", "Divide", "Math", "Division of two values (A / B)");
        RegisterAlias("Divide", "Math_Divide");
        RegisterAlias("MathDivideNode", "Math_Divide");

        RegisterNodeType<MathSinNode>("Math_Sin", "Sin", "Math/Trigonometry", "Sine of input angle in radians");
        RegisterAlias("Sin", "Math_Sin");
        RegisterAlias("Sine", "Math_Sin");
        RegisterAlias("MathSinNode", "Math_Sin");

        RegisterNodeType<MathCosNode>("Math_Cos", "Cos", "Math/Trigonometry", "Cosine of input angle in radians");
        RegisterAlias("Cos", "Math_Cos");
        RegisterAlias("Cosine", "Math_Cos");
        RegisterAlias("MathCosNode", "Math_Cos");

        RegisterNodeType<MathTanNode>("Math_Tan", "Tan", "Math/Trigonometry", "Tangent of input angle in radians");
        RegisterAlias("Tan", "Math_Tan");
        RegisterAlias("Tangent", "Math_Tan");
        RegisterAlias("MathTanNode", "Math_Tan");

        RegisterNodeType<MathDotNode>("Math_Dot", "Dot Product", "Math/Vector", "Dot product of two 3D vectors");
        RegisterAlias("Dot", "Math_Dot");
        RegisterAlias("Dot Product", "Math_Dot");
        RegisterAlias("MathDotNode", "Math_Dot");

        RegisterNodeType<MathCrossNode>("Math_Cross", "Cross Product", "Math/Vector", "Cross product of two 3D vectors");
        RegisterAlias("Cross", "Math_Cross");
        RegisterAlias("Cross Product", "Math_Cross");
        RegisterAlias("MathCrossNode", "Math_Cross");

        RegisterNodeType<MathNormalizeNode>("Math_Normalize", "Normalize", "Math/Vector", "Vector normalized to unit length");
        RegisterAlias("Normalize", "Math_Normalize");
        RegisterAlias("MathNormalizeNode", "Math_Normalize");

        RegisterNodeType<MathLengthNode>("Math_Length", "Length", "Math/Vector", "Euclidean length of a vector");
        RegisterAlias("Length", "Math_Length");
        RegisterAlias("MathLengthNode", "Math_Length");

        RegisterNodeType<MathDistanceNode>("Math_Distance", "Distance", "Math/Vector", "Euclidean distance between two points");
        RegisterAlias("Distance", "Math_Distance");
        RegisterAlias("MathDistanceNode", "Math_Distance");

        // Procedural
        RegisterNodeType<ProceduralMinNode>("Procedural_Min", "Min", "Procedural", "Minimum of two values");
        RegisterAlias("Min", "Procedural_Min");
        RegisterAlias("ProceduralMinNode", "Procedural_Min");

        RegisterNodeType<ProceduralMaxNode>("Procedural_Max", "Max", "Procedural", "Maximum of two values");
        RegisterAlias("Max", "Procedural_Max");
        RegisterAlias("ProceduralMaxNode", "Procedural_Max");

        RegisterNodeType<ProceduralClampNode>("Procedural_Clamp", "Clamp", "Procedural", "Clamp input between min and max");
        RegisterAlias("Clamp", "Procedural_Clamp");
        RegisterAlias("ProceduralClampNode", "Procedural_Clamp");

        RegisterNodeType<ProceduralLerpNode>("Procedural_Lerp", "Lerp", "Procedural", "Linear interpolation between A and B by T");
        RegisterAlias("Lerp", "Procedural_Lerp");
        RegisterAlias("ProceduralLerpNode", "Procedural_Lerp");

        RegisterNodeType<ProceduralAbsNode>("Procedural_Abs", "Abs", "Procedural", "Absolute value");
        RegisterAlias("Abs", "Procedural_Abs");
        RegisterAlias("ProceduralAbsNode", "Procedural_Abs");

        RegisterNodeType<ProceduralStepNode>("Procedural_Step", "Step", "Procedural", "Step function (0 if In < Edge else 1)");
        RegisterAlias("Step", "Procedural_Step");
        RegisterAlias("ProceduralStepNode", "Procedural_Step");

        RegisterNodeType<ProceduralSmoothstepNode>("Procedural_Smoothstep", "Smoothstep", "Procedural", "Hermite smooth interpolation");
        RegisterAlias("Smoothstep", "Procedural_Smoothstep");
        RegisterAlias("ProceduralSmoothstepNode", "Procedural_Smoothstep");

        RegisterNodeType<ProceduralRemapNode>("Procedural_Remap", "Remap", "Procedural", "Remap value from in-range to out-range");
        RegisterAlias("Remap", "Procedural_Remap");
        RegisterAlias("ProceduralRemapNode", "Procedural_Remap");

        RegisterNodeType<ProceduralNoiseNode>("Procedural_Noise", "Noise 2D", "Procedural", "Procedural 2D gradient noise");
        RegisterAlias("ProceduralNoise", "Procedural_Noise");
        RegisterAlias("ProceduralNoiseNode", "Procedural_Noise");
        RegisterAlias("Noise 2D", "Procedural_Noise");
        RegisterAlias("Noise", "Procedural_Noise");

        RegisterNodeType<ProceduralVoronoiNode>("Procedural_Voronoi", "Voronoi", "Procedural", "Procedural cellular Voronoi diagram");
        RegisterAlias("Voronoi", "Procedural_Voronoi");
        RegisterAlias("VoronoiNoise", "Procedural_Voronoi");
        RegisterAlias("ProceduralVoronoiNode", "Procedural_Voronoi");

        // Time
        RegisterNodeType<TimeNode>("Time_Time", "Time", "Time", "Global time and delta time uniforms");
        RegisterAlias("Time", "Time_Time");
        RegisterAlias("TimeNode", "Time_Time");

        RegisterNodeType<DeltaTimeNode>("Time_DeltaTime", "Delta Time", "Time", "Frame delta time uniform");
        RegisterAlias("DeltaTime", "Time_DeltaTime");
        RegisterAlias("Delta Time", "Time_DeltaTime");
        RegisterAlias("DeltaTimeNode", "Time_DeltaTime");

        // Camera / Transform
        RegisterNodeType<CameraPositionNode>("Transform_CameraPosition", "Camera Position", "Transform", "World position of camera");
        RegisterAlias("CameraPosition", "Transform_CameraPosition");
        RegisterAlias("Camera Position", "Transform_CameraPosition");
        RegisterAlias("CameraPositionNode", "Transform_CameraPosition");

        RegisterNodeType<ViewMatrixNode>("Transform_ViewMatrix", "View Matrix", "Transform", "View matrix");
        RegisterAlias("ViewMatrix", "Transform_ViewMatrix");
        RegisterAlias("View Matrix", "Transform_ViewMatrix");
        RegisterAlias("ViewMatrixNode", "Transform_ViewMatrix");

        RegisterNodeType<ProjectionMatrixNode>("Transform_ProjectionMatrix", "Projection Matrix", "Transform", "Projection matrix");
        RegisterAlias("ProjectionMatrix", "Transform_ProjectionMatrix");
        RegisterAlias("Projection Matrix", "Transform_ProjectionMatrix");
        RegisterAlias("ProjectionMatrixNode", "Transform_ProjectionMatrix");

        RegisterNodeType<WorldPositionNode>("Transform_WorldPosition", "World Position", "Transform", "Interpolated world position");
        RegisterAlias("WorldPosition", "Transform_WorldPosition");
        RegisterAlias("World Position", "Transform_WorldPosition");
        RegisterAlias("WorldPositionNode", "Transform_WorldPosition");

        RegisterNodeType<WorldNormalNode>("Transform_WorldNormal", "World Normal", "Transform", "Interpolated world normal");
        RegisterAlias("WorldNormal", "Transform_WorldNormal");
        RegisterAlias("World Normal", "Transform_WorldNormal");
        RegisterAlias("WorldNormalNode", "Transform_WorldNormal");

        // Texture
        RegisterNodeType<TextureSamplerNode>("Texture_Sampler", "Sample Texture 2D", "Texture", "Sample 2D texture with UV");
        RegisterAlias("Texture2D", "Texture_Sampler");
        RegisterAlias("TextureSamplerNode", "Texture_Sampler");
        RegisterAlias("Sample Texture 2D", "Texture_Sampler");
        RegisterAlias("Texture", "Texture_Sampler");

        RegisterNodeType<UVNode>("Texture_UV", "UV Coordinates", "Texture", "Mesh vertex UV coordinates");
        RegisterAlias("UV", "Texture_UV");
        RegisterAlias("UVCoordinates", "Texture_UV");
        RegisterAlias("UV Coordinates", "Texture_UV");
        RegisterAlias("UVNode", "Texture_UV");

        RegisterNodeType<TilingAndOffsetNode>("Texture_TilingAndOffset", "Tiling And Offset", "Texture", "UV tiling and offset");
        RegisterAlias("TilingAndOffset", "Texture_TilingAndOffset");
        RegisterAlias("Tiling And Offset", "Texture_TilingAndOffset");
        RegisterAlias("TilingAndOffsetNode", "Texture_TilingAndOffset");

        // Inputs / Constants / Vectors
        RegisterNodeType<PropertyNode>("Input_Property", "Property", "Input", "Blackboard material parameter");
        RegisterAlias("Property", "Input_Property");
        RegisterAlias("PropertyNode", "Input_Property");

        RegisterNodeType<FloatConstantNode>("Input_Float", "Float", "Input", "Constant float value");
        RegisterAlias("Float", "Input_Float");
        RegisterAlias("FloatConstantNode", "Input_Float");

        RegisterNodeType<Vec2ConstantNode>("Input_Vec2", "Vector2", "Input", "Constant 2D vector");
        RegisterAlias("Vec2", "Input_Vec2");
        RegisterAlias("Vector2", "Input_Vec2");
        RegisterAlias("Vec2ConstantNode", "Input_Vec2");

        RegisterNodeType<Vec3ConstantNode>("Input_Vec3", "Vector3", "Input", "Constant 3D vector");
        RegisterAlias("Vec3", "Input_Vec3");
        RegisterAlias("Vector3", "Input_Vec3");
        RegisterAlias("Vec3ConstantNode", "Input_Vec3");

        RegisterNodeType<Vec4ConstantNode>("Input_Vec4", "Vector4", "Input", "Constant 4D vector / Color");
        RegisterAlias("Vec4", "Input_Vec4");
        RegisterAlias("Vector4", "Input_Vec4");
        RegisterAlias("Color", "Input_Vec4");
        RegisterAlias("Vec4ConstantNode", "Input_Vec4");

        RegisterNodeType<SplitVectorNode>("Vector_Split", "Split", "Vector", "Split vector into components");
        RegisterAlias("Split", "Vector_Split");
        RegisterAlias("SplitVectorNode", "Vector_Split");

        RegisterNodeType<CombineVectorNode>("Vector_Combine", "Combine", "Vector", "Combine components into vectors");
        RegisterAlias("Combine", "Vector_Combine");
        RegisterAlias("CombineVectorNode", "Vector_Combine");

        // Master Output
        RegisterNodeType<MasterNode>("MasterOutput", "Master Output", "Master", "Final material output surface terminal");
        RegisterAlias("Master", "MasterOutput");
        RegisterAlias("MasterNode", "MasterOutput");
        RegisterAlias("Master (Lit)", "MasterOutput");
        RegisterAlias("Master (Unlit)", "MasterOutput");
    }

    // --- MasterNode Implementation ---
    MasterNode::MasterNode() {
        typeName = "MasterOutput";
        title = "Master (Lit)";
        category = "Master";
        SetMaterialType(ShaderGraphType::Lit);
    }

    std::unique_ptr<ShaderNode> MasterNode::Clone() const {
        return std::make_unique<MasterNode>(*this);
    }

    void MasterNode::SetMaterialType(ShaderGraphType type) {
        materialType = type;
        title = (materialType == ShaderGraphType::Lit) ? "Master (Lit)" : "Master (Unlit)";
        customProperties["materialType"] = ShaderGraphTypeToString(materialType);
        UpdatePorts();
    }

    void MasterNode::OnCustomPropertiesUpdated() {
        auto it = customProperties.find("materialType");
        if (it != customProperties.end()) {
            materialType = StringToShaderGraphType(it->second);
            title = (materialType == ShaderGraphType::Lit) ? "Master (Lit)" : "Master (Unlit)";
            UpdatePorts();
        }
    }

    void MasterNode::UpdatePorts() {
        inputs.clear();
        outputs.clear();

        if (materialType == ShaderGraphType::Lit) {
            AddInput("Albedo", "Albedo", PinType::Vec3, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            AddInput("Normal", "Normal", PinType::Vec3, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
            AddInput("Roughness", "Roughness", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
            AddInput("Metallic", "Metallic", PinType::Float, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            AddInput("Emissive", "Emissive", PinType::Vec3, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            AddInput("AO", "AO", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            AddInput("Alpha", "Alpha", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            AddInput("AlphaClipThreshold", "Alpha Clip", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
        } else {
            AddInput("Color", "Color", PinType::Vec3, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            AddInput("Emissive", "Emissive", PinType::Vec3, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
            AddInput("Alpha", "Alpha", PinType::Float, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
            AddInput("AlphaClipThreshold", "Alpha Clip", PinType::Float, glm::vec4(0.5f, 0.0f, 0.0f, 0.0f));
        }
    }

    void MasterNode::GenerateGLSL(
        const std::unordered_map<std::string, std::string>& inExpr,
        std::unordered_map<std::string, std::string>& outExpr,
        std::string& stmts
    ) const {
        (void)stmts;
        if (materialType == ShaderGraphType::Lit) {
            outExpr["Albedo"] = inExpr.count("Albedo") ? inExpr.at("Albedo") : "vec3(1.0)";
            outExpr["Normal"] = inExpr.count("Normal") ? inExpr.at("Normal") : "vNormal";
            outExpr["Roughness"] = inExpr.count("Roughness") ? inExpr.at("Roughness") : "0.5";
            outExpr["Metallic"] = inExpr.count("Metallic") ? inExpr.at("Metallic") : "0.0";
            outExpr["Emissive"] = inExpr.count("Emissive") ? inExpr.at("Emissive") : "vec3(0.0)";
            outExpr["AO"] = inExpr.count("AO") ? inExpr.at("AO") : "1.0";
            outExpr["Alpha"] = inExpr.count("Alpha") ? inExpr.at("Alpha") : "1.0";
            outExpr["AlphaClipThreshold"] = inExpr.count("AlphaClipThreshold") ? inExpr.at("AlphaClipThreshold") : "0.5";
        } else {
            outExpr["Color"] = inExpr.count("Color") ? inExpr.at("Color") : (inExpr.count("Emissive") ? inExpr.at("Emissive") : "vec3(1.0)");
            outExpr["Emissive"] = inExpr.count("Emissive") ? inExpr.at("Emissive") : "vec3(0.0)";
            outExpr["Alpha"] = inExpr.count("Alpha") ? inExpr.at("Alpha") : "1.0";
            outExpr["AlphaClipThreshold"] = inExpr.count("AlphaClipThreshold") ? inExpr.at("AlphaClipThreshold") : "0.5";
        }
    }

    // --- Math Nodes Implementation ---
    MathAddNode::MathAddNode() {
        typeName = "Math_Add";
        title = "Add";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathAddNode::Clone() const {
        return std::make_unique<MathAddNode>(*this);
    }
    void MathAddNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "(" + a + " + " + b + ")";
    }

    MathSubtractNode::MathSubtractNode() {
        typeName = "Math_Subtract";
        title = "Subtract";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathSubtractNode::Clone() const {
        return std::make_unique<MathSubtractNode>(*this);
    }
    void MathSubtractNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "(" + a + " - " + b + ")";
    }

    MathMultiplyNode::MathMultiplyNode() {
        typeName = "Math_Multiply";
        title = "Multiply";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(1.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathMultiplyNode::Clone() const {
        return std::make_unique<MathMultiplyNode>(*this);
    }
    void MathMultiplyNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "1.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "1.0";
        outExpr["Out"] = "(" + a + " * " + b + ")";
    }

    MathDivideNode::MathDivideNode() {
        typeName = "Math_Divide";
        title = "Divide";
        category = "Math";
        AddInput("A", "A", PinType::Float, glm::vec4(1.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathDivideNode::Clone() const {
        return std::make_unique<MathDivideNode>(*this);
    }
    void MathDivideNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "1.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "1.0";
        outExpr["Out"] = "(" + a + " / (" + b + " != 0.0 ? " + b + " : 0.00001))";
    }

    MathSinNode::MathSinNode() {
        typeName = "Math_Sin";
        title = "Sin";
        category = "Math/Trigonometry";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathSinNode::Clone() const {
        return std::make_unique<MathSinNode>(*this);
    }
    void MathSinNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : (inExpr.count("in_x") ? inExpr.at("in_x") : "0.0");
        outExpr["Out"] = "sin(" + inVal + ")";
    }

    MathCosNode::MathCosNode() {
        typeName = "Math_Cos";
        title = "Cos";
        category = "Math/Trigonometry";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathCosNode::Clone() const {
        return std::make_unique<MathCosNode>(*this);
    }
    void MathCosNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "cos(" + inVal + ")";
    }

    MathTanNode::MathTanNode() {
        typeName = "Math_Tan";
        title = "Tan";
        category = "Math/Trigonometry";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathTanNode::Clone() const {
        return std::make_unique<MathTanNode>(*this);
    }
    void MathTanNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "tan(" + inVal + ")";
    }

    MathDotNode::MathDotNode() {
        typeName = "Math_Dot";
        title = "Dot Product";
        category = "Math/Vector";
        AddInput("A", "A", PinType::Vec3, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Vec3, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathDotNode::Clone() const {
        return std::make_unique<MathDotNode>(*this);
    }
    void MathDotNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "vec3(0.0)";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "vec3(0.0)";
        outExpr["Out"] = "dot(" + a + ", " + b + ")";
    }

    MathCrossNode::MathCrossNode() {
        typeName = "Math_Cross";
        title = "Cross Product";
        category = "Math/Vector";
        AddInput("A", "A", PinType::Vec3, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
        AddInput("B", "B", PinType::Vec3, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Vec3);
    }
    std::unique_ptr<ShaderNode> MathCrossNode::Clone() const {
        return std::make_unique<MathCrossNode>(*this);
    }
    void MathCrossNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "vec3(0.0, 0.0, 1.0)";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "vec3(1.0, 0.0, 0.0)";
        outExpr["Out"] = "cross(" + a + ", " + b + ")";
    }

    MathNormalizeNode::MathNormalizeNode() {
        typeName = "Math_Normalize";
        title = "Normalize";
        category = "Math/Vector";
        AddInput("In", "In", PinType::Vec3, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Vec3);
    }
    std::unique_ptr<ShaderNode> MathNormalizeNode::Clone() const {
        return std::make_unique<MathNormalizeNode>(*this);
    }
    void MathNormalizeNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "vec3(0.0, 1.0, 0.0)";
        outExpr["Out"] = "normalize(" + inVal + ")";
    }

    MathLengthNode::MathLengthNode() {
        typeName = "Math_Length";
        title = "Length";
        category = "Math/Vector";
        AddInput("In", "In", PinType::Vec3, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathLengthNode::Clone() const {
        return std::make_unique<MathLengthNode>(*this);
    }
    void MathLengthNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "vec3(0.0)";
        outExpr["Out"] = "length(" + inVal + ")";
    }

    MathDistanceNode::MathDistanceNode() {
        typeName = "Math_Distance";
        title = "Distance";
        category = "Math/Vector";
        AddInput("A", "A", PinType::Vec3, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Vec3, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> MathDistanceNode::Clone() const {
        return std::make_unique<MathDistanceNode>(*this);
    }
    void MathDistanceNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "vec3(0.0)";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "vec3(0.0)";
        outExpr["Out"] = "distance(" + a + ", " + b + ")";
    }

    // --- Procedural Nodes Implementation ---
    ProceduralMinNode::ProceduralMinNode() {
        typeName = "Procedural_Min";
        title = "Min";
        category = "Procedural";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralMinNode::Clone() const {
        return std::make_unique<ProceduralMinNode>(*this);
    }
    void ProceduralMinNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "min(" + a + ", " + b + ")";
    }

    ProceduralMaxNode::ProceduralMaxNode() {
        typeName = "Procedural_Max";
        title = "Max";
        category = "Procedural";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralMaxNode::Clone() const {
        return std::make_unique<ProceduralMaxNode>(*this);
    }
    void ProceduralMaxNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "0.0";
        outExpr["Out"] = "max(" + a + ", " + b + ")";
    }

    ProceduralClampNode::ProceduralClampNode() {
        typeName = "Procedural_Clamp";
        title = "Clamp";
        category = "Procedural";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddInput("Min", "Min", PinType::Float, glm::vec4(0.0f));
        AddInput("Max", "Max", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralClampNode::Clone() const {
        return std::make_unique<ProceduralClampNode>(*this);
    }
    void ProceduralClampNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        std::string minVal = inExpr.count("Min") ? inExpr.at("Min") : "0.0";
        std::string maxVal = inExpr.count("Max") ? inExpr.at("Max") : "1.0";
        outExpr["Out"] = "clamp(" + inVal + ", " + minVal + ", " + maxVal + ")";
    }

    ProceduralLerpNode::ProceduralLerpNode() {
        typeName = "Procedural_Lerp";
        title = "Lerp";
        category = "Procedural";
        AddInput("A", "A", PinType::Float, glm::vec4(0.0f));
        AddInput("B", "B", PinType::Float, glm::vec4(1.0f));
        AddInput("T", "T", PinType::Float, glm::vec4(0.5f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralLerpNode::Clone() const {
        return std::make_unique<ProceduralLerpNode>(*this);
    }
    void ProceduralLerpNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string a = inExpr.count("A") ? inExpr.at("A") : "0.0";
        std::string b = inExpr.count("B") ? inExpr.at("B") : "1.0";
        std::string t = inExpr.count("T") ? inExpr.at("T") : "0.5";
        outExpr["Out"] = "mix(" + a + ", " + b + ", " + t + ")";
    }

    ProceduralAbsNode::ProceduralAbsNode() {
        typeName = "Procedural_Abs";
        title = "Abs";
        category = "Procedural";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralAbsNode::Clone() const {
        return std::make_unique<ProceduralAbsNode>(*this);
    }
    void ProceduralAbsNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "abs(" + inVal + ")";
    }

    ProceduralStepNode::ProceduralStepNode() {
        typeName = "Procedural_Step";
        title = "Step";
        category = "Procedural";
        AddInput("Edge", "Edge", PinType::Float, glm::vec4(0.5f));
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralStepNode::Clone() const {
        return std::make_unique<ProceduralStepNode>(*this);
    }
    void ProceduralStepNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string edge = inExpr.count("Edge") ? inExpr.at("Edge") : "0.5";
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.0";
        outExpr["Out"] = "step(" + edge + ", " + inVal + ")";
    }

    ProceduralSmoothstepNode::ProceduralSmoothstepNode() {
        typeName = "Procedural_Smoothstep";
        title = "Smoothstep";
        category = "Procedural";
        AddInput("Edge1", "Edge1", PinType::Float, glm::vec4(0.0f));
        AddInput("Edge2", "Edge2", PinType::Float, glm::vec4(1.0f));
        AddInput("In", "In", PinType::Float, glm::vec4(0.5f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralSmoothstepNode::Clone() const {
        return std::make_unique<ProceduralSmoothstepNode>(*this);
    }
    void ProceduralSmoothstepNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string e1 = inExpr.count("Edge1") ? inExpr.at("Edge1") : "0.0";
        std::string e2 = inExpr.count("Edge2") ? inExpr.at("Edge2") : "1.0";
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "0.5";
        outExpr["Out"] = "smoothstep(" + e1 + ", " + e2 + ", " + inVal + ")";
    }

    ProceduralRemapNode::ProceduralRemapNode() {
        typeName = "Procedural_Remap";
        title = "Remap";
        category = "Procedural";
        AddInput("In", "In", PinType::Float, glm::vec4(0.0f));
        AddInput("InMin", "In Min", PinType::Float, glm::vec4(0.0f));
        AddInput("InMax", "In Max", PinType::Float, glm::vec4(1.0f));
        AddInput("OutMin", "Out Min", PinType::Float, glm::vec4(0.0f));
        AddInput("OutMax", "Out Max", PinType::Float, glm::vec4(1.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralRemapNode::Clone() const {
        return std::make_unique<ProceduralRemapNode>(*this);
    }
    void ProceduralRemapNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string val = inExpr.count("In") ? inExpr.at("In") : "0.0";
        std::string inMin = inExpr.count("InMin") ? inExpr.at("InMin") : "0.0";
        std::string inMax = inExpr.count("InMax") ? inExpr.at("InMax") : "1.0";
        std::string outMin = inExpr.count("OutMin") ? inExpr.at("OutMin") : "0.0";
        std::string outMax = inExpr.count("OutMax") ? inExpr.at("OutMax") : "1.0";
        outExpr["Out"] = "(" + outMin + " + ((" + val + " - " + inMin + ") / max(" + inMax + " - " + inMin + ", 0.00001)) * (" + outMax + " - " + outMin + "))";
    }

    ProceduralNoiseNode::ProceduralNoiseNode() {
        typeName = "Procedural_Noise";
        title = "Noise 2D";
        category = "Procedural";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f)).defaultToMeshUV = true;
        AddInput("Scale", "Scale", PinType::Float, glm::vec4(5.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralNoiseNode::Clone() const {
        return std::make_unique<ProceduralNoiseNode>(*this);
    }
    void ProceduralNoiseNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string scale = inExpr.count("Scale") ? inExpr.at("Scale") : "5.0";
        outExpr["Out"] = "slip_gradient_noise(" + uv + " * " + scale + ")";
    }

    ProceduralVoronoiNode::ProceduralVoronoiNode() {
        typeName = "Procedural_Voronoi";
        title = "Voronoi";
        category = "Procedural";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f)).defaultToMeshUV = true;
        AddInput("Scale", "Scale", PinType::Float, glm::vec4(5.0f, 0.0f, 0.0f, 0.0f));
        AddInput("AngleOffset", "Angle Offset", PinType::Float, glm::vec4(0.0f));
        AddOutput("Out", "Out", PinType::Float);
        AddOutput("Cells", "Cells", PinType::Float);
    }
    std::unique_ptr<ShaderNode> ProceduralVoronoiNode::Clone() const {
        return std::make_unique<ProceduralVoronoiNode>(*this);
    }
    void ProceduralVoronoiNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string scale = inExpr.count("Scale") ? inExpr.at("Scale") : "5.0";
        std::string angle = inExpr.count("AngleOffset") ? inExpr.at("AngleOffset") : "0.0";
        std::string varName = "voronoi_" + id;
        stmts += "    vec2 " + varName + " = slip_voronoi(" + uv + ", " + angle + ", " + scale + ");\n";
        outExpr["Out"] = varName + ".x";
        outExpr["Cells"] = varName + ".y";
    }

    // --- Time Nodes Implementation ---
    TimeNode::TimeNode() {
        typeName = "Time_Time";
        title = "Time";
        category = "Time";
        AddOutput("Time", "Time", PinType::Float);
        AddOutput("SinTime", "Sin Time", PinType::Float);
        AddOutput("CosTime", "Cos Time", PinType::Float);
        AddOutput("DeltaTime", "Delta Time", PinType::Float);
    }
    std::unique_ptr<ShaderNode> TimeNode::Clone() const {
        return std::make_unique<TimeNode>(*this);
    }
    void TimeNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Time"] = "u_Time";
        outExpr["SinTime"] = "sin(u_Time)";
        outExpr["CosTime"] = "cos(u_Time)";
        outExpr["DeltaTime"] = "u_DeltaTime";
    }

    DeltaTimeNode::DeltaTimeNode() {
        typeName = "Time_DeltaTime";
        title = "Delta Time";
        category = "Time";
        AddOutput("DeltaTime", "Delta Time", PinType::Float);
    }
    std::unique_ptr<ShaderNode> DeltaTimeNode::Clone() const {
        return std::make_unique<DeltaTimeNode>(*this);
    }
    void DeltaTimeNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["DeltaTime"] = "u_DeltaTime";
    }

    // --- Camera / Transform Nodes Implementation ---
    CameraPositionNode::CameraPositionNode() {
        typeName = "Transform_CameraPosition";
        title = "Camera Position";
        category = "Transform";
        AddOutput("Position", "Position", PinType::Vec3);
    }
    std::unique_ptr<ShaderNode> CameraPositionNode::Clone() const {
        return std::make_unique<CameraPositionNode>(*this);
    }
    void CameraPositionNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Position"] = "u_CameraPos";
    }

    ViewMatrixNode::ViewMatrixNode() {
        typeName = "Transform_ViewMatrix";
        title = "View Matrix";
        category = "Transform";
        AddOutput("View", "View", PinType::Matrix4);
    }
    std::unique_ptr<ShaderNode> ViewMatrixNode::Clone() const {
        return std::make_unique<ViewMatrixNode>(*this);
    }
    void ViewMatrixNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["View"] = "u_View";
    }

    ProjectionMatrixNode::ProjectionMatrixNode() {
        typeName = "Transform_ProjectionMatrix";
        title = "Projection Matrix";
        category = "Transform";
        AddOutput("Projection", "Projection", PinType::Matrix4);
    }
    std::unique_ptr<ShaderNode> ProjectionMatrixNode::Clone() const {
        return std::make_unique<ProjectionMatrixNode>(*this);
    }
    void ProjectionMatrixNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Projection"] = "u_Projection";
    }

    WorldPositionNode::WorldPositionNode() {
        typeName = "Transform_WorldPosition";
        title = "World Position";
        category = "Transform";
        AddOutput("Position", "Position", PinType::Vec3);
    }
    std::unique_ptr<ShaderNode> WorldPositionNode::Clone() const {
        return std::make_unique<WorldPositionNode>(*this);
    }
    void WorldPositionNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Position"] = "vWorldPos";
    }

    WorldNormalNode::WorldNormalNode() {
        typeName = "Transform_WorldNormal";
        title = "World Normal";
        category = "Transform";
        AddOutput("Normal", "Normal", PinType::Vec3);
    }
    std::unique_ptr<ShaderNode> WorldNormalNode::Clone() const {
        return std::make_unique<WorldNormalNode>(*this);
    }
    void WorldNormalNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Normal"] = "vNormal";
    }

    // --- Texture Nodes Implementation ---
    TextureSamplerNode::TextureSamplerNode() {
        typeName = "Texture_Sampler";
        title = "Sample Texture 2D";
        category = "Texture";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f)).defaultToMeshUV = true;
        AddInput("Sampler", "Sampler", PinType::Texture2D);
        AddOutput("RGBA", "RGBA", PinType::Vec4);
        AddOutput("RGB", "RGB", PinType::Vec3);
        AddOutput("R", "R", PinType::Float);
        AddOutput("G", "G", PinType::Float);
        AddOutput("B", "B", PinType::Float);
        AddOutput("A", "A", PinType::Float);
    }
    std::unique_ptr<ShaderNode> TextureSamplerNode::Clone() const {
        return std::make_unique<TextureSamplerNode>(*this);
    }
    void TextureSamplerNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string sampler = inExpr.count("Sampler") ? inExpr.at("Sampler") : "u_MainTexture";
        std::string sampleVar = "texSample_" + id;
        stmts += "    vec4 " + sampleVar + " = texture(" + sampler + ", " + uv + ");\n";
        outExpr["RGBA"] = sampleVar;
        outExpr["RGB"] = sampleVar + ".rgb";
        outExpr["R"] = sampleVar + ".r";
        outExpr["G"] = sampleVar + ".g";
        outExpr["B"] = sampleVar + ".b";
        outExpr["A"] = sampleVar + ".a";
    }

    UVNode::UVNode() {
        typeName = "Texture_UV";
        title = "UV Coordinates";
        category = "Texture";
        AddOutput("UV", "UV", PinType::Vec2);
    }
    std::unique_ptr<ShaderNode> UVNode::Clone() const {
        return std::make_unique<UVNode>(*this);
    }
    void UVNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["UV"] = "vTexCoords";
    }

    TilingAndOffsetNode::TilingAndOffsetNode() {
        typeName = "Texture_TilingAndOffset";
        title = "Tiling And Offset";
        category = "Texture";
        AddInput("UV", "UV", PinType::Vec2, glm::vec4(0.0f)).defaultToMeshUV = true;
        AddInput("Tiling", "Tiling", PinType::Vec2, glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
        AddInput("Offset", "Offset", PinType::Vec2, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        AddOutput("Out", "Out", PinType::Vec2);
    }
    std::unique_ptr<ShaderNode> TilingAndOffsetNode::Clone() const {
        return std::make_unique<TilingAndOffsetNode>(*this);
    }
    void TilingAndOffsetNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string uv = inExpr.count("UV") ? inExpr.at("UV") : "vTexCoords";
        std::string tiling = inExpr.count("Tiling") ? inExpr.at("Tiling") : "vec2(1.0, 1.0)";
        std::string offset = inExpr.count("Offset") ? inExpr.at("Offset") : "vec2(0.0, 0.0)";
        outExpr["Out"] = "(" + uv + " * " + tiling + " + " + offset + ")";
    }

    // --- Property & Constant Nodes Implementation ---
    PropertyNode::PropertyNode() {
        typeName = "Input_Property";
        title = "Property";
        category = "Input";
        UpdatePorts();
    }
    std::unique_ptr<ShaderNode> PropertyNode::Clone() const {
        return std::make_unique<PropertyNode>(*this);
    }
    void PropertyNode::SetProperty(const ShaderProperty& prop) {
        propertyId = prop.id;
        propertyName = prop.name;
        propertyType = prop.type;
        title = prop.displayName.empty() ? prop.name : prop.displayName;
        customProperties["propertyId"] = propertyId;
        customProperties["propertyName"] = propertyName;
        customProperties["propertyType"] = PinTypeToString(propertyType);
        UpdatePorts();
    }
    void PropertyNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("propertyId")) propertyId = customProperties["propertyId"];
        if (customProperties.count("propertyName")) propertyName = customProperties["propertyName"];
        if (customProperties.count("propertyType")) propertyType = StringToPinType(customProperties["propertyType"]);
        title = propertyName.empty() ? "Property" : propertyName;
        UpdatePorts();
    }
    void PropertyNode::UpdatePorts() {
        outputs.clear();
        AddOutput("Out", "Out", propertyType);
    }
    void PropertyNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Out"] = "u_" + propertyName;
    }

    FloatConstantNode::FloatConstantNode() {
        typeName = "Input_Float";
        title = "Float";
        category = "Input";
        AddOutput("Out", "Out", PinType::Float);
    }
    std::unique_ptr<ShaderNode> FloatConstantNode::Clone() const {
        return std::make_unique<FloatConstantNode>(*this);
    }
    void FloatConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("value")) {
            value = std::stof(customProperties["value"]);
        }
    }
    void FloatConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << value;
        outExpr["Out"] = ss.str();
    }

    Vec2ConstantNode::Vec2ConstantNode() {
        typeName = "Input_Vec2";
        title = "Vector2";
        category = "Input";
        AddOutput("Out", "Out", PinType::Vec2);
    }
    std::unique_ptr<ShaderNode> Vec2ConstantNode::Clone() const {
        return std::make_unique<Vec2ConstantNode>(*this);
    }
    void Vec2ConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("x")) value.x = std::stof(customProperties["x"]);
        if (customProperties.count("y")) value.y = std::stof(customProperties["y"]);
    }
    void Vec2ConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Out"] = FormatGLSLValue(PinType::Vec2, glm::vec4(value, 0.0f, 0.0f));
    }

    Vec3ConstantNode::Vec3ConstantNode() {
        typeName = "Input_Vec3";
        title = "Vector3";
        category = "Input";
        AddOutput("Out", "Out", PinType::Vec3);
    }
    std::unique_ptr<ShaderNode> Vec3ConstantNode::Clone() const {
        return std::make_unique<Vec3ConstantNode>(*this);
    }
    void Vec3ConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("x")) value.x = std::stof(customProperties["x"]);
        if (customProperties.count("y")) value.y = std::stof(customProperties["y"]);
        if (customProperties.count("z")) value.z = std::stof(customProperties["z"]);
    }
    void Vec3ConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Out"] = FormatGLSLValue(PinType::Vec3, glm::vec4(value, 0.0f));
    }

    Vec4ConstantNode::Vec4ConstantNode() {
        typeName = "Input_Vec4";
        title = "Vector4";
        category = "Input";
        AddOutput("Out", "Out", PinType::Vec4);
    }
    std::unique_ptr<ShaderNode> Vec4ConstantNode::Clone() const {
        return std::make_unique<Vec4ConstantNode>(*this);
    }
    void Vec4ConstantNode::OnCustomPropertiesUpdated() {
        if (customProperties.count("x")) value.x = std::stof(customProperties["x"]);
        if (customProperties.count("y")) value.y = std::stof(customProperties["y"]);
        if (customProperties.count("z")) value.z = std::stof(customProperties["z"]);
        if (customProperties.count("w")) value.w = std::stof(customProperties["w"]);
    }
    void Vec4ConstantNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)inExpr;
        (void)stmts;
        outExpr["Out"] = FormatGLSLValue(PinType::Vec4, value);
    }

    SplitVectorNode::SplitVectorNode() {
        typeName = "Vector_Split";
        title = "Split";
        category = "Vector";
        AddInput("In", "In", PinType::Vec4, glm::vec4(0.0f));
        AddOutput("X", "X", PinType::Float);
        AddOutput("Y", "Y", PinType::Float);
        AddOutput("Z", "Z", PinType::Float);
        AddOutput("W", "W", PinType::Float);
    }
    std::unique_ptr<ShaderNode> SplitVectorNode::Clone() const {
        return std::make_unique<SplitVectorNode>(*this);
    }
    void SplitVectorNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string inVal = inExpr.count("In") ? inExpr.at("In") : "vec4(0.0)";
        outExpr["X"] = "(" + inVal + ").x";
        outExpr["Y"] = "(" + inVal + ").y";
        outExpr["Z"] = "(" + inVal + ").z";
        outExpr["W"] = "(" + inVal + ").w";
    }

    CombineVectorNode::CombineVectorNode() {
        typeName = "Vector_Combine";
        title = "Combine";
        category = "Vector";
        AddInput("X", "X", PinType::Float, glm::vec4(0.0f));
        AddInput("Y", "Y", PinType::Float, glm::vec4(0.0f));
        AddInput("Z", "Z", PinType::Float, glm::vec4(0.0f));
        AddInput("W", "W", PinType::Float, glm::vec4(1.0f));
        AddOutput("Vec4", "Vec4", PinType::Vec4);
        AddOutput("Vec3", "Vec3", PinType::Vec3);
        AddOutput("Vec2", "Vec2", PinType::Vec2);
    }
    std::unique_ptr<ShaderNode> CombineVectorNode::Clone() const {
        return std::make_unique<CombineVectorNode>(*this);
    }
    void CombineVectorNode::GenerateGLSL(const std::unordered_map<std::string, std::string>& inExpr, std::unordered_map<std::string, std::string>& outExpr, std::string& stmts) const {
        (void)stmts;
        std::string x = inExpr.count("X") ? inExpr.at("X") : "0.0";
        std::string y = inExpr.count("Y") ? inExpr.at("Y") : "0.0";
        std::string z = inExpr.count("Z") ? inExpr.at("Z") : "0.0";
        std::string w = inExpr.count("W") ? inExpr.at("W") : "1.0";
        outExpr["Vec4"] = "vec4(" + x + ", " + y + ", " + z + ", " + w + ")";
        outExpr["Vec3"] = "vec3(" + x + ", " + y + ", " + z + ")";
        outExpr["Vec2"] = "vec2(" + x + ", " + y + ")";
    }

} // namespace SlipShaderGraph
