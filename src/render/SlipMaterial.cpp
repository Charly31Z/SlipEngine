#include "SlipMaterial.h"
#include "SlipGlobals.h"
#include "SlipShadows.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include <sstream>
#include <algorithm>
#include <cstring>

// Bridge function exported by ShaderGraphCompiler.cpp
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

SlipMaterial SlipMaterial::generateMaterial(std::string code, const char* path)
{
    std::string vertPath = code + ".vert";
    std::string fragPath = code + ".frag";
    SlipShader s{ vertPath.c_str(), fragPath.c_str() };

    return SlipMaterial(path ? path : "");
}

SlipMaterial SlipMaterial::generateMaterial2(SlipShader shader, const char* path)
{
    return SlipMaterial(path ? path : "");
}

void SlipMaterial::loadMatFromFile(std::string matPath)
{
    if (matPath.empty()) return;

    std::vector<std::string> candidates = {
        matPath,
        matPath + ".mat",
    };

    std::string fileName = SlipFileSystem::GetFileName(matPath);

    std::string assets = SlipFileSystem::GetProjectAssetsPath();
    std::string root = SlipFileSystem::GetProjectRootPath();
    if (!assets.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(assets, matPath));
        candidates.push_back(SlipFileSystem::CombinePath(assets, matPath + ".mat"));
        candidates.push_back(SlipFileSystem::CombinePath(assets, "materials/" + matPath));
        candidates.push_back(SlipFileSystem::CombinePath(assets, "materials/" + matPath + ".mat"));

        if (!fileName.empty() && fileName != matPath) {
            candidates.push_back(SlipFileSystem::CombinePath(assets, fileName));
            candidates.push_back(SlipFileSystem::CombinePath(assets, fileName + ".mat"));
            candidates.push_back(SlipFileSystem::CombinePath(assets, "materials/" + fileName));
            candidates.push_back(SlipFileSystem::CombinePath(assets, "materials/" + fileName + ".mat"));
        }
    }
    if (!root.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(root, matPath));
        candidates.push_back(SlipFileSystem::CombinePath(root, matPath + ".mat"));
        candidates.push_back(SlipFileSystem::CombinePath(root, "assets/" + matPath));
        candidates.push_back(SlipFileSystem::CombinePath(root, "assets/materials/" + matPath));

        if (!fileName.empty() && fileName != matPath) {
            candidates.push_back(SlipFileSystem::CombinePath(root, fileName));
            candidates.push_back(SlipFileSystem::CombinePath(root, "assets/" + fileName));
            candidates.push_back(SlipFileSystem::CombinePath(root, "assets/materials/" + fileName));
        }
    }

    candidates.push_back("assets/materials/" + matPath);
    candidates.push_back("assets/materials/" + matPath + ".mat");
    candidates.push_back("assets/" + matPath);
    candidates.push_back("assets/" + matPath + ".mat");

    if (!fileName.empty() && fileName != matPath) {
        candidates.push_back("assets/materials/" + fileName);
        candidates.push_back("assets/materials/" + fileName + ".mat");
        candidates.push_back("assets/" + fileName);
    }

    std::string foundPath;
    for (const auto& c : candidates) {
        if (SlipFileSystem::Exists(c)) {
            foundPath = c;
            break;
        }
    }

    if (!foundPath.empty()) {
        this->path = foundPath;

        shaderGraphPath.clear();
        graphFloatParams.clear();
        graphVectorParams.clear();
        graphTextureParams.clear();

        std::string content;
        if (SlipFileSystem::ReadAllText(foundPath, content)) {
            bool isText = true;
            for (char ch : content.substr(0, std::min<size_t>(content.size(), 64))) {
                if (ch == 0) { isText = false; break; }
            }

            if (isText) {
                std::stringstream ss(content);
                std::string line;
                while (std::getline(ss, line)) {
                    size_t colon = line.find(':');
                    if (colon == std::string::npos) continue;

                    std::string key = line.substr(0, colon);
                    std::string val = line.substr(colon + 1);

                    key.erase(key.find_last_not_of(" \t\r\n") + 1);
                    key.erase(0, key.find_first_not_of(" \t\r\n"));
                    val.erase(val.find_last_not_of(" \t\r\n") + 1);
                    val.erase(0, val.find_first_not_of(" \t\r\n"));

                    if (key == "shader" || key == "shaderType") {
                        std::strncpy(shaderType, val.c_str(), sizeof(shaderType) - 1);
                        if (std::string(shaderType) == "default") std::strncpy(shaderType, "Lit", sizeof(shaderType) - 1);
                    } else if (key == "shaderGraph" || key == "shaderGraphPath") {
                        shaderGraphPath = val;
                    } else if (key.rfind("graphFloat_", 0) == 0) {
                        std::string pName = key.substr(11);
                        graphFloatParams[pName] = std::stof(val);
                    } else if (key.rfind("graphVector_", 0) == 0) {
                        std::string pName = key.substr(12);
                        std::stringstream vs(val);
                        glm::vec4 v(0.0f);
                        vs >> v.x >> v.y >> v.z >> v.w;
                        graphVectorParams[pName] = v;
                    } else if (key.rfind("graphTexture_", 0) == 0) {
                        std::string pName = key.substr(13);
                        graphTextureParams[pName] = val;
                    } else if (key.rfind("graphParam.float.", 0) == 0) {
                        std::string pName = key.substr(17);
                        graphFloatParams[pName] = std::stof(val);
                    } else if (key.rfind("graphParam.vec4.", 0) == 0) {
                        std::string pName = key.substr(16);
                        std::stringstream vs(val);
                        glm::vec4 v(0.0f);
                        vs >> v.x >> v.y >> v.z >> v.w;
                        graphVectorParams[pName] = v;
                    } else if (key.rfind("graphParam.texture.", 0) == 0) {
                        std::string pName = key.substr(19);
                        graphTextureParams[pName] = val;
                    } else if (key.rfind("graphParam.", 0) == 0) {
                        std::string pName = key.substr(11);
                        std::stringstream vs(val);
                        std::vector<float> floats;
                        float f;
                        while (vs >> f) floats.push_back(f);
                        if (floats.size() > 1) {
                            glm::vec4 v(0.0f);
                            if (floats.size() >= 1) v.x = floats[0];
                            if (floats.size() >= 2) v.y = floats[1];
                            if (floats.size() >= 3) v.z = floats[2];
                            if (floats.size() >= 4) v.w = floats[3];
                            graphVectorParams[pName] = v;
                        } else if (floats.size() == 1) {
                            graphFloatParams[pName] = floats[0];
                        } else {
                            graphTextureParams[pName] = val;
                        }
                    } else if (key == "color" || key == "albedo") {
                        std::stringstream vs(val);
                        vs >> color.r >> color.g >> color.b;
                    } else if (key == "roughness") {
                        roughness = std::stof(val);
                    } else if (key == "normalScale" || key == "normalStrength") {
                        normalScale = std::stof(val);
                    } else if (key == "metallic") {
                        metallic = std::stof(val);
                    } else if (key == "ao") {
                        ao = std::stof(val);
                    } else if (key == "emissive") {
                        std::stringstream vs(val);
                        vs >> emissiveColor.r >> emissiveColor.g >> emissiveColor.b;
                    } else if (key == "emissiveIntensity") {
                        emissiveIntensity = std::stof(val);
                    } else if (key == "ambient") {
                        std::stringstream vs(val);
                        vs >> ambient.r >> ambient.g >> ambient.b;
                    } else if (key == "specular") {
                        std::stringstream vs(val);
                        vs >> specular.r >> specular.g >> specular.b;
                    } else if (key == "shininess") {
                        shininess = std::stof(val);
                    } else if (key == "diffuseTexture" || key == "diffPath" || key == "albedoMap") {
                        std::strncpy(diffPath, val.c_str(), sizeof(diffPath) - 1);
                        diffPath[sizeof(diffPath) - 1] = '\0';
                    } else if (key == "normalTexture" || key == "normalPath" || key == "normalMap") {
                        std::strncpy(normalPath, val.c_str(), sizeof(normalPath) - 1);
                        normalPath[sizeof(normalPath) - 1] = '\0';
                    } else if (key == "roughnessTexture" || key == "roughnessPath" || key == "roughnessMap") {
                        std::strncpy(roughnessPath, val.c_str(), sizeof(roughnessPath) - 1);
                        roughnessPath[sizeof(roughnessPath) - 1] = '\0';
                    } else if (key == "metallicTexture" || key == "metallicPath" || key == "metallicMap") {
                        std::strncpy(metallicPath, val.c_str(), sizeof(metallicPath) - 1);
                        metallicPath[sizeof(metallicPath) - 1] = '\0';
                    } else if (key == "aoTexture" || key == "aoPath" || key == "aoMap") {
                        std::strncpy(aoPath, val.c_str(), sizeof(aoPath) - 1);
                        aoPath[sizeof(aoPath) - 1] = '\0';
                    } else if (key == "emissiveTexture" || key == "emissivePath" || key == "emissiveMap") {
                        std::strncpy(emissivePath, val.c_str(), sizeof(emissivePath) - 1);
                        emissivePath[sizeof(emissivePath) - 1] = '\0';
                    } else if (key == "opacityTexture" || key == "opacityPath" || key == "opacityMap") {
                        std::strncpy(opacityPath, val.c_str(), sizeof(opacityPath) - 1);
                        opacityPath[sizeof(opacityPath) - 1] = '\0';
                    } else if (key == "alphaClip") {
                        alphaClip = (val == "1" || val == "true" || val == "True");
                    } else if (key == "alphaCutoff") {
                        alphaCutoff = std::stof(val);
                    } else if (key == "invertRoughness") {
                        invertRoughness = (val == "1" || val == "true" || val == "True");
                    } else if (key == "doubleSided" || key == "twoSided") {
                        doubleSided = (val == "1" || val == "true" || val == "True");
                    }
                }
            }
        }

        reloadTextures();
    }

    init();
}

static SlipTexture* LoadMaterialSlotTexture(const char* rawSlotPath, const std::string& matFilePath)
{
    if (!rawSlotPath || std::strlen(rawSlotPath) == 0) return nullptr;

    std::string texPath = rawSlotPath;
    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
    std::string rootDir = SlipFileSystem::GetProjectRootPath();
    std::string matDir = SlipFileSystem::GetParentPath(matFilePath);
    std::string matParentDir = SlipFileSystem::GetParentPath(matDir);
    std::string texFileName = SlipFileSystem::GetFileName(texPath);

    std::vector<std::string> candidates = { texPath };
    if (!assetsDir.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(assetsDir, texPath));
    }
    if (!rootDir.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(rootDir, texPath));
        candidates.push_back(SlipFileSystem::CombinePath(rootDir, "assets/" + texPath));
    }
    if (!matDir.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(matDir, texPath));
        if (!texFileName.empty()) {
            candidates.push_back(SlipFileSystem::CombinePath(matDir, texFileName));
        }
    }
    if (!matParentDir.empty() && !texFileName.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(matParentDir, "Textures/" + texFileName));
        candidates.push_back(SlipFileSystem::CombinePath(matParentDir, "textures/" + texFileName));
        candidates.push_back(SlipFileSystem::CombinePath(matParentDir, texFileName));
    }

    for (const auto& c : candidates) {
        if (SlipFileSystem::Exists(c)) {
            return SlipTexture::load(c.c_str());
        }
    }
    return SlipTexture::load(texPath.c_str());
}

void SlipMaterial::reloadTextures()
{
    diffuseTexture   = LoadMaterialSlotTexture(diffPath, path);
    normalTexture    = LoadMaterialSlotTexture(normalPath, path);
    roughnessTexture = LoadMaterialSlotTexture(roughnessPath, path);
    metallicTexture  = LoadMaterialSlotTexture(metallicPath, path);
    aoTexture        = LoadMaterialSlotTexture(aoPath, path);
    emissiveTexture  = LoadMaterialSlotTexture(emissivePath, path);
    opacityTexture   = LoadMaterialSlotTexture(opacityPath, path);
}

bool SlipMaterial::Save(const std::string& savePath)
{
    std::string target = savePath.empty() ? path : savePath;
    if (target.empty()) {
        target = "assets/materials/NewMaterial.mat";
    }
    if (SlipFileSystem::GetFileExtension(target) != ".mat") {
        target += ".mat";
    }

    std::stringstream ss;
    ss << "shader: " << shaderType << "\n";
    ss << "color: " << color.r << " " << color.g << " " << color.b << "\n";
    ss << "roughness: " << roughness << "\n";
    ss << "metallic: " << metallic << "\n";
    ss << "ao: " << ao << "\n";
    ss << "emissive: " << emissiveColor.r << " " << emissiveColor.g << " " << emissiveColor.b << "\n";
    ss << "emissiveIntensity: " << emissiveIntensity << "\n";
    ss << "ambient: " << ambient.r << " " << ambient.g << " " << ambient.b << "\n";
    ss << "specular: " << specular.r << " " << specular.g << " " << specular.b << "\n";
    ss << "shininess: " << shininess << "\n";
    ss << "diffuseTexture: " << diffPath << "\n";
    ss << "normalTexture: " << normalPath << "\n";
    ss << "normalScale: " << normalScale << "\n";
    ss << "roughnessTexture: " << roughnessPath << "\n";
    ss << "metallicTexture: " << metallicPath << "\n";
    ss << "aoTexture: " << aoPath << "\n";
    ss << "emissiveTexture: " << emissivePath << "\n";
    ss << "opacityTexture: " << opacityPath << "\n";
    ss << "alphaClip: " << (alphaClip ? 1 : 0) << "\n";
    ss << "alphaCutoff: " << alphaCutoff << "\n";
    ss << "invertRoughness: " << (invertRoughness ? 1 : 0) << "\n";
    ss << "doubleSided: " << (doubleSided ? 1 : 0) << "\n";

    if (!shaderGraphPath.empty()) {
        ss << "shaderGraph: " << shaderGraphPath << "\n";
        for (const auto& pair : graphFloatParams) {
            ss << "graphFloat_" << pair.first << ": " << pair.second << "\n";
            ss << "graphParam." << pair.first << ": " << pair.second << "\n";
        }
        for (const auto& pair : graphVectorParams) {
            ss << "graphVector_" << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
            ss << "graphParam." << pair.first << ": " << pair.second.x << " " << pair.second.y << " " << pair.second.z << " " << pair.second.w << "\n";
        }
        for (const auto& pair : graphTextureParams) {
            ss << "graphTexture_" << pair.first << ": " << pair.second << "\n";
            ss << "graphParam." << pair.first << ": " << pair.second << "\n";
        }
    }

    path = target;
    return SlipFileSystem::WriteAllText(target, ss.str());
}

SlipMaterial::SlipMaterial(std::string path) : path(path)
{
    loadMatFromFile(path);
}

SlipMaterial::~SlipMaterial()
{
    // El shader es gestionado por SlipShaderLibrary y las texturas por el caché de SlipTexture
    m_CustomShader.reset();
    shader = nullptr;
    diffuseTexture = nullptr;
    normalTexture = nullptr;
    roughnessTexture = nullptr;
    metallicTexture = nullptr;
    aoTexture = nullptr;
    emissiveTexture = nullptr;
    opacityTexture = nullptr;
}

void SlipMaterial::init()
{
    if (!glad_glCreateProgram) return;

    if (!shaderGraphPath.empty()) {
        if (LoadShaderGraph(shaderGraphPath)) {
            return;
        }
    }

    m_CustomShader.reset();
    std::string sType = std::string(shaderType);
    if (sType.empty() || sType == "default" || sType == "ShaderGraph") sType = "Lit";
    shader = SlipShaderLibrary::LoadOrGet(sType);
}

bool SlipMaterial::LoadShaderGraph(const std::string& sgPath)
{
    if (sgPath.empty()) {
        shaderGraphPath = "";
        m_CustomShader.reset();
        shader = nullptr;
        init();
        return true;
    }

    std::string vertSrc, fragSrc, sTypeStr, errorMsg;
    std::vector<std::pair<std::string, float>> floatProps;
    std::vector<std::pair<std::string, glm::vec4>> vectorProps;
    std::vector<std::pair<std::string, std::string>> textureProps;

    if (!SlipCompileShaderGraphFile(sgPath, vertSrc, fragSrc, sTypeStr, &floatProps, &vectorProps, &textureProps, &errorMsg)) {
        return false;
    }

    SlipShader* newShader = SlipShader::CreateFromSource(vertSrc.c_str(), fragSrc.c_str());
    if (!newShader || newShader->ID == 0) {
        if (newShader) delete newShader;
        return false;
    }

    shaderGraphPath = sgPath;

    // Populate default parameters if not already configured in material instance
    for (const auto& fp : floatProps) {
        if (graphFloatParams.find(fp.first) == graphFloatParams.end()) {
            graphFloatParams[fp.first] = fp.second;
        }
    }
    for (const auto& vp : vectorProps) {
        if (graphVectorParams.find(vp.first) == graphVectorParams.end()) {
            graphVectorParams[vp.first] = vp.second;
        }
    }
    for (const auto& tp : textureProps) {
        if (graphTextureParams.find(tp.first) == graphTextureParams.end()) {
            graphTextureParams[tp.first] = tp.second;
        }
    }

    m_CustomShader = std::shared_ptr<SlipShader>(newShader, [](SlipShader* s) {
        if (s) {
            s->destroy();
            delete s;
        }
    });
    shader = m_CustomShader.get();

    std::strncpy(shaderType, sTypeStr.c_str(), sizeof(shaderType) - 1);
    shaderType[sizeof(shaderType) - 1] = '\0';

    return true;
}

void SlipMaterial::SetGraphFloat(const std::string& name, float value)
{
    graphFloatParams[name] = value;
}

void SlipMaterial::SetGraphVector(const std::string& name, const glm::vec4& value)
{
    graphVectorParams[name] = value;
}

void SlipMaterial::SetGraphTexture(const std::string& name, const std::string& texturePath)
{
    graphTextureParams[name] = texturePath;
}

float SlipMaterial::GetGraphFloat(const std::string& name, float fallback) const
{
    auto it = graphFloatParams.find(name);
    if (it != graphFloatParams.end()) return it->second;
    return fallback;
}

glm::vec4 SlipMaterial::GetGraphVector(const std::string& name, const glm::vec4& fallback) const
{
    auto it = graphVectorParams.find(name);
    if (it != graphVectorParams.end()) return it->second;
    return fallback;
}

std::string SlipMaterial::GetGraphTexture(const std::string& name, const std::string& fallback) const
{
    auto it = graphTextureParams.find(name);
    if (it != graphTextureParams.end()) return it->second;
    return fallback;
}

bool SlipMaterial::HasGraphFloat(const std::string& name) const
{
    return graphFloatParams.find(name) != graphFloatParams.end();
}

bool SlipMaterial::HasGraphVector(const std::string& name) const
{
    return graphVectorParams.find(name) != graphVectorParams.end();
}

bool SlipMaterial::HasGraphTexture(const std::string& name) const
{
    return graphTextureParams.find(name) != graphTextureParams.end();
}

void SlipMaterial::bindGraphParameters(SlipShader* targetShader)
{
    if (!targetShader) return;

    for (const auto& pair : graphFloatParams) {
        std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
        targetShader->setFloat(uName, pair.second);
    }
    for (const auto& pair : graphVectorParams) {
        std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
        targetShader->setVec4(uName, pair.second);
    }

    unsigned int customTexUnit = 20;
    for (const auto& pair : graphTextureParams) {
        if (pair.second.empty() || customTexUnit >= 32) continue;
        std::string uName = (pair.first.rfind("u_", 0) == 0) ? pair.first : ("u_" + pair.first);
        SlipTexture* tex = SlipTexture::load(pair.second.c_str());
        if (tex) {
            targetShader->setInt(uName, (int)customTexUnit);
            tex->bind(customTexUnit);
        }
        customTexUnit++;
    }
}

void SlipMaterial::bindMaterialProperties(SlipShader* targetShader)
{
    if (!targetShader) return;

    if (!shaderGraphPath.empty()) {
        bindGraphParameters(targetShader);
    }

    std::string sType = std::string(shaderType);
    if (sType == "Unlit") {
        targetShader->setVec3("u_Color", color);
        bool hasTex = (diffuseTexture != nullptr && std::strlen(diffPath) > 0);
        targetShader->setBool("u_HasTexture", hasTex);
        if (hasTex) {
            targetShader->setInt("u_MainTexture", 0);
            diffuseTexture->bind(0);
        }
        return;
    }

    // Lit / PBR
    targetShader->setVec3("u_Albedo", color);
    targetShader->setFloat("u_Roughness", roughness);
    targetShader->setFloat("u_Metallic", metallic);
    targetShader->setFloat("u_AO", ao);
    targetShader->setVec3("u_EmissiveColor", emissiveColor);
    targetShader->setFloat("u_EmissiveIntensity", emissiveIntensity);
    targetShader->setBool("u_AlphaClip", alphaClip);
    targetShader->setFloat("u_AlphaCutoff", alphaCutoff);
    targetShader->setBool("u_InvertRoughness", invertRoughness);

    // Compatibilidad previa
    targetShader->setVec3("material.color", color);
    targetShader->setVec3("material.ambient", ambient);
    targetShader->setVec3("material.specular", specular);
    targetShader->setFloat("material.shininess", shininess);

    // Unidad 0: Albedo / BaseColor
    bool hasAlbedo = (diffuseTexture != nullptr && std::strlen(diffPath) > 0);
    targetShader->setBool("u_HasAlbedoMap", hasAlbedo);
    if (hasAlbedo) {
        targetShader->setInt("u_AlbedoMap", 0);
        targetShader->setInt("texture_diffuse", 0);
        diffuseTexture->bind(0);
    }

    // Unidad 1: Normal Map
    bool hasNormal = (normalTexture != nullptr && std::strlen(normalPath) > 0);
    targetShader->setBool("u_HasNormalMap", hasNormal);
    targetShader->setFloat("u_NormalScale", normalScale);
    if (hasNormal) {
        targetShader->setInt("u_NormalMap", 1);
        normalTexture->bind(1);
    }

    // Unidad 2: Roughness Map
    bool hasRoughness = (roughnessTexture != nullptr && std::strlen(roughnessPath) > 0);
    targetShader->setBool("u_HasRoughnessMap", hasRoughness);
    if (hasRoughness) {
        targetShader->setInt("u_RoughnessMap", 2);
        roughnessTexture->bind(2);
    }

    // Unidad 4: Metallic Map (Unidad 3 reservada para u_ShadowMapArray CSM)
    bool hasMetallic = (metallicTexture != nullptr && std::strlen(metallicPath) > 0);
    targetShader->setBool("u_HasMetallicMap", hasMetallic);
    if (hasMetallic) {
        targetShader->setInt("u_MetallicMap", 4);
        metallicTexture->bind(4);
    }

    bool packedORM = (hasRoughness && hasMetallic && roughnessTexture == metallicTexture);
    targetShader->setBool("u_PackedORM", packedORM);

    // Unidad 15: AO Map (Unidades 5..14 reservadas para SlipDDGI)
    bool hasAO = (aoTexture != nullptr && std::strlen(aoPath) > 0);
    targetShader->setBool("u_HasAOMap", hasAO);
    if (hasAO) {
        targetShader->setInt("u_AOMap", 15);
        aoTexture->bind(15);
    }

    // Unidad 16: Emissive Map
    bool hasEmissive = (emissiveTexture != nullptr && std::strlen(emissivePath) > 0);
    targetShader->setBool("u_HasEmissiveMap", hasEmissive);
    if (hasEmissive) {
        targetShader->setInt("u_EmissiveMap", 16);
        emissiveTexture->bind(16);
    }

    // Unidad 17: Opacity Map
    bool hasOpacity = (opacityTexture != nullptr && std::strlen(opacityPath) > 0);
    targetShader->setBool("u_HasOpacityMap", hasOpacity);
    if (hasOpacity) {
        targetShader->setInt("u_OpacityMap", 17);
        opacityTexture->bind(17);
    }
}

void SlipMaterial::bind(glm::mat4 &model)
{
    if (!shader) {
        init();
    }
    if (!shader) return;

    shader->use();

    // Matrices
    shader->setMat4("u_Model", model);
    shader->setMat4("model", model);

    // Globals
    shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
    shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());

    bindMaterialProperties(shader);
}
