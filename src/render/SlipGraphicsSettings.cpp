#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <fstream>
#include <sstream>
#include <algorithm>

SlipGraphicsSettings& SlipGraphicsSettings::Get() {
    static SlipGraphicsSettings s_Instance;
    return s_Instance;
}

SlipGraphicsSettings::SlipGraphicsSettings() {
    ResetToDefaults();
}

void SlipGraphicsSettings::ResetToDefaults() {
    m_Data.shadowsEnabled = true;
    m_Data.shadowResolution = 2048;
    m_Data.cascadeCount = 4;
    m_Data.shadowDistance = 100.0f;
    m_Data.shadowBias = 0.002f;
    m_Data.pcfQuality = 1;
    m_Data.cascadeLambda = 0.85f;
    m_Data.fxAAEnabled = true;
    m_Data.fxAAQuality = 1;
    m_Data.ddgiEnabled = true;
    m_Data.ddgiCameraVolume = true;
    m_Data.ddgiSpacing = 2.0f;
    m_Data.ddgiProbesPerFrame = 256;
    m_Data.ddgiRaysPerProbe = 64;
    m_Data.ddgiHysteresis = 0.78f;
    m_Data.ddgiIntensity = 1.0f;
    m_Data.ddgiShowProbes = false;
    m_Data.ssrEnabled = true;
    m_Data.ssrQuality = 2;
    m_Data.ssrRoughnessCutoff = 0.75f;
    m_Data.ssrThickness = 0.45f;
    m_Data.ssrEdgeFade = 0.08f;
    m_Data.skyEnabled = true;
    m_Data.skyZenithColor = glm::vec3(0.22f, 0.48f, 0.92f);
    m_Data.skyHorizonColor = glm::vec3(0.72f, 0.82f, 0.95f);
    m_Data.skyGroundColor = glm::vec3(0.25f, 0.22f, 0.20f);
    m_Data.skyHorizonSharpness = 2.5f;
    m_Data.sunDiscEnabled = true;
    m_Data.sunSize = 0.045f;
    m_Data.sunGlow = 0.18f;
    m_Data.sunIntensity = 1.8f;
    m_Data.sunColor = glm::vec3(1.0f, 0.96f, 0.88f);
    m_Data.reflectionProbeEnabled = true;
    m_Data.reflectionProbeResolution = 256;
    m_Data.reflectionIncludeDynamic = true;
    m_Data.reflectionIntensity = 1.0f;
    m_Data.vSyncEnabled = false;
    m_Data.frustumCullingEnabled = true;
    m_Data.skinningQuality = 4;
    m_PendingRebuild = true;
}

static inline std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool SlipGraphicsSettings::LoadFromFile(const std::string& filePath) {
    if (filePath.empty() || !SlipFileSystem::Exists(filePath)) {
        return false;
    }

    std::string content;
    if (!SlipFileSystem::ReadAllText(filePath, content) || content.empty()) {
        return false;
    }

    std::stringstream ss(content);
    std::string line;

    int prevRes = m_Data.shadowResolution;
    int prevCascades = m_Data.cascadeCount;

    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) eqPos = line.find(':');
        if (eqPos == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eqPos));
        std::string val = Trim(line.substr(eqPos + 1));

        if (key == "ShadowsEnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.shadowsEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "ShadowResolution") {
            try { m_Data.shadowResolution = std::stoi(val); } catch (...) {}
        } else if (key == "CascadeCount") {
            try { m_Data.cascadeCount = std::clamp(std::stoi(val), 1, 4); } catch (...) {}
        } else if (key == "ShadowDistance") {
            try { m_Data.shadowDistance = std::stof(val); } catch (...) {}
        } else if (key == "ShadowBias") {
            try { m_Data.shadowBias = std::stof(val); } catch (...) {}
        } else if (key == "PCFQuality") {
            try { m_Data.pcfQuality = std::clamp(std::stoi(val), 0, 2); } catch (...) {}
        } else if (key == "CascadeLambda") {
            try { m_Data.cascadeLambda = std::clamp(std::stof(val), 0.0f, 1.0f); } catch (...) {}
        } else if (key == "DDGIEnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.ddgiEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "DDGICameraVolume") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.ddgiCameraVolume = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "DDGISpacing") {
            try { m_Data.ddgiSpacing = std::clamp(std::stof(val), 0.5f, 16.0f); } catch (...) {}
        } else if (key == "DDGIProbesPerFrame") {
            try { m_Data.ddgiProbesPerFrame = std::clamp(std::stoi(val), 1, 256); } catch (...) {}
        } else if (key == "DDGIRaysPerProbe") {
            try { m_Data.ddgiRaysPerProbe = std::clamp(std::stoi(val), 8, 128); } catch (...) {}
        } else if (key == "DDGIHysteresis") {
            try { m_Data.ddgiHysteresis = std::clamp(std::stof(val), 0.0f, 0.98f); } catch (...) {}
        } else if (key == "DDGIIntensity") {
            try { m_Data.ddgiIntensity = std::clamp(std::stof(val), 0.0f, 8.0f); } catch (...) {}
        } else if (key == "DDGIShowProbes") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.ddgiShowProbes = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "SSREnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.ssrEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "SSRQuality") {
            try { m_Data.ssrQuality = std::clamp(std::stoi(val), 0, 2); } catch (...) {}
        } else if (key == "FXAAEnabled" || key == "AntiAliasing") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.fxAAEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "FXAAQuality") {
            try { m_Data.fxAAQuality = std::clamp(std::stoi(val), 0, 2); } catch (...) {}
        } else if (key == "SkyEnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.skyEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "SunDiscEnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.sunDiscEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "ReflectionProbeEnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.reflectionProbeEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "ReflectionProbeResolution") {
            try { m_Data.reflectionProbeResolution = std::clamp(std::stoi(val), 64, 1024); } catch (...) {}
        } else if (key == "ReflectionIncludeDynamic") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.reflectionIncludeDynamic = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "ReflectionIntensity") {
            try { m_Data.reflectionIntensity = std::max(0.0f, std::stof(val)); } catch (...) {}
        } else if (key == "ReflectionRoughnessCutoff") {
            try { m_Data.reflectionRoughnessCutoff = std::clamp(std::stof(val), 0.05f, 1.0f); } catch (...) {}
        } else if (key == "VSync" || key == "VSyncEnabled") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.vSyncEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "FrustumCulling") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.frustumCullingEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "OcclusionCulling") {
            std::string lowerVal = val;
            std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
            m_Data.occlusionCullingEnabled = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
        } else if (key == "SkinningQuality") {
            try { m_Data.skinningQuality = std::clamp(std::stoi(val), 0, 4); } catch (...) {}
        } else if (key == "SSRRoughnessCutoff") {
            try { m_Data.ssrRoughnessCutoff = std::clamp(std::stof(val), 0.05f, 1.0f); } catch (...) {}
        } else if (key == "SSRThickness") {
            try { m_Data.ssrThickness = std::clamp(std::stof(val), 0.05f, 2.0f); } catch (...) {}
        } else if (key == "SSREdgeFade") {
            try { m_Data.ssrEdgeFade = std::clamp(std::stof(val), 0.01f, 0.5f); } catch (...) {}
        } else if (key == "SkyZenithColor") {
            std::stringstream vss(val); float r, g, b; if (vss >> r >> g >> b) m_Data.skyZenithColor = glm::vec3(r, g, b);
        } else if (key == "SkyHorizonColor") {
            std::stringstream vss(val); float r, g, b; if (vss >> r >> g >> b) m_Data.skyHorizonColor = glm::vec3(r, g, b);
        } else if (key == "SkyGroundColor") {
            std::stringstream vss(val); float r, g, b; if (vss >> r >> g >> b) m_Data.skyGroundColor = glm::vec3(r, g, b);
        } else if (key == "SkyHorizonSharpness") {
            try { m_Data.skyHorizonSharpness = std::clamp(std::stof(val), 0.1f, 8.0f); } catch (...) {}
        } else if (key == "SunSize") {
            try { m_Data.sunSize = std::clamp(std::stof(val), 0.01f, 0.35f); } catch (...) {}
        } else if (key == "SunGlow") {
            try { m_Data.sunGlow = std::clamp(std::stof(val), 0.01f, 1.0f); } catch (...) {}
        } else if (key == "SunIntensity") {
            try { m_Data.sunIntensity = std::clamp(std::stof(val), 0.0f, 10.0f); } catch (...) {}
        } else if (key == "SunColor") {
            std::stringstream vss(val); float r, g, b; if (vss >> r >> g >> b) m_Data.sunColor = glm::vec3(r, g, b);
        }
    }

    if (m_Data.shadowResolution != prevRes || m_Data.cascadeCount != prevCascades) {
        m_PendingRebuild = true;
    }

    return true;
}

bool SlipGraphicsSettings::SaveToFile(const std::string& filePath) {
    if (filePath.empty()) return false;

    std::string dir = SlipFileSystem::GetParentPath(filePath);
    if (!dir.empty() && !SlipFileSystem::Exists(dir)) {
        SlipFileSystem::CreateDirectoryPath(dir);
    }

    std::stringstream ss;
    ss << "; SlipEngine Graphics Settings\n";
    ss << "[Graphics]\n";
    ss << "ShadowsEnabled=" << (m_Data.shadowsEnabled ? "true" : "false") << "\n";
    ss << "ShadowResolution=" << m_Data.shadowResolution << "\n";
    ss << "CascadeCount=" << m_Data.cascadeCount << "\n";
    ss << "ShadowDistance=" << m_Data.shadowDistance << "\n";
    ss << "ShadowBias=" << m_Data.shadowBias << "\n";
    ss << "PCFQuality=" << m_Data.pcfQuality << "\n";
    ss << "CascadeLambda=" << m_Data.cascadeLambda << "\n";
    ss << "DDGIEnabled=" << (m_Data.ddgiEnabled ? "true" : "false") << "\n";
    ss << "DDGICameraVolume=" << (m_Data.ddgiCameraVolume ? "true" : "false") << "\n";
    ss << "DDGISpacing=" << m_Data.ddgiSpacing << "\n";
    ss << "DDGIProbesPerFrame=" << m_Data.ddgiProbesPerFrame << "\n";
    ss << "DDGIRaysPerProbe=" << m_Data.ddgiRaysPerProbe << "\n";
    ss << "DDGIHysteresis=" << m_Data.ddgiHysteresis << "\n";
    ss << "DDGIIntensity=" << m_Data.ddgiIntensity << "\n";
    ss << "DDGIShowProbes=" << (m_Data.ddgiShowProbes ? "true" : "false") << "\n";
    ss << "SSREnabled=" << (m_Data.ssrEnabled ? "true" : "false") << "\n";
    ss << "SSRQuality=" << m_Data.ssrQuality << "\n";
    ss << "SSRRoughnessCutoff=" << m_Data.ssrRoughnessCutoff << "\n";
    ss << "SSRThickness=" << m_Data.ssrThickness << "\n";
    ss << "SSREdgeFade=" << m_Data.ssrEdgeFade << "\n";
    ss << "FXAAEnabled=" << (m_Data.fxAAEnabled ? "true" : "false") << "\n";
    ss << "FXAAQuality=" << m_Data.fxAAQuality << "\n";
    ss << "SkyEnabled=" << (m_Data.skyEnabled ? "true" : "false") << "\n";
    ss << "SkyZenithColor=" << m_Data.skyZenithColor.r << " " << m_Data.skyZenithColor.g << " " << m_Data.skyZenithColor.b << "\n";
    ss << "SkyHorizonColor=" << m_Data.skyHorizonColor.r << " " << m_Data.skyHorizonColor.g << " " << m_Data.skyHorizonColor.b << "\n";
    ss << "SkyGroundColor=" << m_Data.skyGroundColor.r << " " << m_Data.skyGroundColor.g << " " << m_Data.skyGroundColor.b << "\n";
    ss << "SkyHorizonSharpness=" << m_Data.skyHorizonSharpness << "\n";
    ss << "SunDiscEnabled=" << (m_Data.sunDiscEnabled ? "true" : "false") << "\n";
    ss << "SunSize=" << m_Data.sunSize << "\n";
    ss << "SunGlow=" << m_Data.sunGlow << "\n";
    ss << "SunIntensity=" << m_Data.sunIntensity << "\n";
    ss << "SunColor=" << m_Data.sunColor.r << " " << m_Data.sunColor.g << " " << m_Data.sunColor.b << "\n";
    ss << "ReflectionProbeEnabled=" << (m_Data.reflectionProbeEnabled ? "true" : "false") << "\n";
    ss << "ReflectionProbeResolution=" << m_Data.reflectionProbeResolution << "\n";
    ss << "ReflectionIncludeDynamic=" << (m_Data.reflectionIncludeDynamic ? "true" : "false") << "\n";
    ss << "ReflectionIntensity=" << m_Data.reflectionIntensity << "\n";
    ss << "ReflectionRoughnessCutoff=" << m_Data.reflectionRoughnessCutoff << "\n";
    ss << "VSyncEnabled=" << (m_Data.vSyncEnabled ? "true" : "false") << "\n";
    ss << "FrustumCulling=" << (m_Data.frustumCullingEnabled ? "true" : "false") << "\n";
    ss << "OcclusionCulling=" << (m_Data.occlusionCullingEnabled ? "true" : "false") << "\n";
    ss << "SkinningQuality=" << m_Data.skinningQuality << "\n";

    return SlipFileSystem::WriteAllText(filePath, ss.str());
}
