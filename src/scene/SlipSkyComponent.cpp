#include "SlipEngine/scene/SlipSkyComponent.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/render/SlipTexture.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
#else
    #include <glad/glad.h>
#endif

#if defined(SLIP_EDITOR)
    #include <imgui.h>
#endif

#include <cmath>
#include <vector>
#include <sstream>
#include <algorithm>

// ============================================================================
// Constructor y Destructor
// ============================================================================
SlipSkyComponent::SlipSkyComponent()
{
    m_Name = "SlipSkyComponent";
}

SlipSkyComponent::~SlipSkyComponent()
{
    ReleaseDefaultTextures();
}

void SlipSkyComponent::OnInit()
{
    if (!cloudNoisePath.empty() && SlipFileSystem::Exists(cloudNoisePath)) {
        cloudNoiseTexture = SlipTexture::load(cloudNoisePath.c_str());
    }
    if (!blueNoisePath.empty() && SlipFileSystem::Exists(blueNoisePath)) {
        blueNoiseTexture = SlipTexture::load(blueNoisePath.c_str());
    }

    EnsureDefaultTextures();
}

void SlipSkyComponent::OnUpdate(float deltaTime)
{
    m_CurrentTime += deltaTime;
}

// ============================================================================
// Generación Dinámica de Texturas Procedurales (Sin depender de assets externos)
// ============================================================================
void SlipSkyComponent::EnsureDefaultTextures()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) return;
#endif
    if (m_DefaultNoiseTex == 0 || m_DefaultBlueNoiseTex == 0) {
        GenerateDefaultTextures();
    }
}

void SlipSkyComponent::ReleaseDefaultTextures()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glDeleteTextures) {
        m_DefaultNoiseTex = 0;
        m_DefaultBlueNoiseTex = 0;
        return;
    }
#endif
    if (m_DefaultNoiseTex != 0) {
        glDeleteTextures(1, &m_DefaultNoiseTex);
        m_DefaultNoiseTex = 0;
    }
    if (m_DefaultBlueNoiseTex != 0) {
        glDeleteTextures(1, &m_DefaultBlueNoiseTex);
        m_DefaultBlueNoiseTex = 0;
    }
}

void SlipSkyComponent::GenerateDefaultTextures()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) return;
#endif
    // 1. Textura 2D Perlin-Worley tileable de 512x512 para morfología y desperfectos de nubes
    const int noiseSize = 512;
    std::vector<unsigned char> noiseData(noiseSize * noiseSize * 4);

    auto hash22 = [](float x, float y) -> glm::vec2 {
        float n = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
        float nx = n - std::floor(n);
        n = std::sin((x + 13.1f) * 12.9898f + (y + 47.7f) * 78.233f) * 43758.5453f;
        float ny = n - std::floor(n);
        return glm::vec2(nx, ny);
    };

    auto sampleGradNoise = [&](float u, float v, int freq) -> float {
        float fx = u * static_cast<float>(freq);
        float fy = v * static_cast<float>(freq);
        int x0 = static_cast<int>(std::floor(fx));
        int y0 = static_cast<int>(std::floor(fy));
        int x1 = x0 + 1;
        int y1 = y0 + 1;

        float tx = fx - static_cast<float>(x0);
        float ty = fy - static_cast<float>(y0);
        // Hermite quíntico para C2-continuidad suave sin polígonos
        float sx = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
        float sy = ty * ty * ty * (ty * (ty * 6.0f - 15.0f) + 10.0f);

        auto dotGrad = [&](int ix, int iy, float rx, float ry) {
            int cx = (ix % freq + freq) % freq;
            int cy = (iy % freq + freq) % freq;
            glm::vec2 g = hash22(static_cast<float>(cx) * 1.37f + 0.5f, static_cast<float>(cy) * 2.81f + 0.5f) * 2.0f - 1.0f;
            float len = glm::length(g);
            if (len > 0.001f) g /= len;
            return g.x * rx + g.y * ry;
        };

        float n00 = dotGrad(x0, y0, tx, ty);
        float n10 = dotGrad(x1, y0, tx - 1.0f, ty);
        float n01 = dotGrad(x0, y1, tx, ty - 1.0f);
        float n11 = dotGrad(x1, y1, tx - 1.0f, ty - 1.0f);

        float nx0 = glm::mix(n00, n10, sx);
        float nx1 = glm::mix(n01, n11, sx);
        return glm::clamp(glm::mix(nx0, nx1, sy) * 0.5f + 0.5f, 0.0f, 1.0f);
    };

    auto sampleWorley = [&](float u, float v, int freq) -> float {
        float fx = u * static_cast<float>(freq);
        float fy = v * static_cast<float>(freq);
        int ix = static_cast<int>(std::floor(fx));
        int iy = static_cast<int>(std::floor(fy));
        float minDist = 1.0f;

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int cellX = (ix + dx + freq * 10) % freq;
                int cellY = (iy + dy + freq * 10) % freq;
                glm::vec2 p = hash22(static_cast<float>(cellX), static_cast<float>(cellY));
                glm::vec2 feat = glm::vec2(ix + dx, iy + dy) + p;
                float d = glm::length(glm::vec2(fx, fy) - feat);
                if (d < minDist) minDist = d;
            }
        }
        return std::clamp(minDist, 0.0f, 1.0f);
    };

    for (int y = 0; y < noiseSize; ++y) {
        for (int x = 0; x < noiseSize; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(noiseSize);
            float v = static_cast<float>(y) / static_cast<float>(noiseSize);

            // 1. Perlin FBM multi-octava periódico y suave
            float p1 = sampleGradNoise(u, v, 4);
            float p2 = sampleGradNoise(u, v, 8);
            float p3 = sampleGradNoise(u, v, 16);
            float perlinFBM = p1 * 0.55f + p2 * 0.30f + p3 * 0.15f;

            // 2. Worley FBM multi-escala periódico
            float w1 = 1.0f - sampleWorley(u, v, 4);
            float w2 = 1.0f - sampleWorley(u, v, 8);
            float w3 = 1.0f - sampleWorley(u, v, 16);
            float w4 = 1.0f - sampleWorley(u, v, 32);
            float worleyFBM = w1 * 0.50f + w2 * 0.30f + w3 * 0.20f;

            // 3. Fusión Perlin-Worley clásica (Guerilla Games / Frostbite):
            // La base Perlin aporta la forma esponjosa/nubosa orgánica (sin polígonos),
            // y Worley erosiona los bordes creando mechones y girones naturales
            float pw = std::clamp((perlinFBM - (1.0f - worleyFBM) * 0.38f) / 0.62f, 0.0f, 1.0f);

            int idx = (y * noiseSize + x) * 4;
            noiseData[idx + 0] = static_cast<unsigned char>(pw * 255.0f);
            noiseData[idx + 1] = static_cast<unsigned char>((1.0f - w2) * 255.0f);
            noiseData[idx + 2] = static_cast<unsigned char>((1.0f - w3) * 255.0f);
            noiseData[idx + 3] = static_cast<unsigned char>((1.0f - w4) * 255.0f);
        }
    }

    if (m_DefaultNoiseTex == 0) {
        glGenTextures(1, &m_DefaultNoiseTex);
        glBindTexture(GL_TEXTURE_2D, m_DefaultNoiseTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, noiseSize, noiseSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, noiseData.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // 2. Textura Blue Noise estocástica de 64x64 para dispersión de jittering
    const int blueSize = 64;
    std::vector<unsigned char> blueData(blueSize * blueSize);

    // Matriz de dispersión decorrelacionada (dithering de baja discrepancia)
    for (int y = 0; y < blueSize; ++y) {
        for (int x = 0; x < blueSize; ++x) {
            // Generador pseudo-estocástico tipo R2 sequence
            float g = 1.324717957244746f;
            float a1 = 1.0f / g;
            float a2 = 1.0f / (g * g);
            float val = std::fmod(0.5f + a1 * static_cast<float>(x) + a2 * static_cast<float>(y), 1.0f);
            blueData[y * blueSize + x] = static_cast<unsigned char>(val * 255.0f);
        }
    }

    if (m_DefaultBlueNoiseTex == 0) {
        glGenTextures(1, &m_DefaultBlueNoiseTex);
        glBindTexture(GL_TEXTURE_2D, m_DefaultBlueNoiseTex);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, blueSize, blueSize, 0, GL_RED, GL_UNSIGNED_BYTE, blueData.data());
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, blueSize, blueSize, 0, GL_RED, GL_UNSIGNED_BYTE, blueData.data());
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// ============================================================================
// Atmósfera Dinámica basada en el Ángulo Solar (Día / Atardecer / Noche)
// ============================================================================
void SlipSkyComponent::UpdateAtmosphereFromSun()
{
    if (!params.dynamicAtmosphere) return;

    glm::vec3 sDir = glm::length(params.sunDirection) > 0.0001f
                     ? glm::normalize(params.sunDirection)
                     : glm::vec3(0.0f, 1.0f, 0.0f);

    float h = sDir.y; // Altura solar sobre el horizonte (-1.0 a 1.0)

    // Factores de transición suave
    float dayFactor = std::clamp((h - 0.04f) / 0.30f, 0.0f, 1.0f);
    float sunsetFactor = std::clamp(1.0f - std::abs(h - 0.03f) / 0.20f, 0.0f, 1.0f);
    float nightFactor = std::clamp((-h - 0.02f) / 0.22f, 0.0f, 1.0f);

    // 1. Color Cenit (Zenith)
    glm::vec3 dayZenith    = glm::vec3(0.18f, 0.44f, 0.82f);
    glm::vec3 sunsetZenith = glm::vec3(0.09f, 0.11f, 0.30f);
    glm::vec3 nightZenith  = glm::vec3(0.012f, 0.016f, 0.038f);

    glm::vec3 zenith = glm::mix(sunsetZenith, dayZenith, dayFactor);
    zenith = glm::mix(zenith, nightZenith, nightFactor);

    // 2. Color Horizonte (Horizon)
    glm::vec3 dayHorizon    = glm::vec3(0.68f, 0.82f, 0.94f);
    glm::vec3 sunsetHorizon = glm::vec3(0.98f, 0.42f, 0.12f);
    glm::vec3 nightHorizon  = glm::vec3(0.035f, 0.045f, 0.09f);

    glm::vec3 horizon = glm::mix(sunsetHorizon, dayHorizon, dayFactor);
    horizon = glm::mix(horizon, nightHorizon, nightFactor);

    // 3. Color Suelo (Ground)
    glm::vec3 dayGround    = glm::vec3(0.35f, 0.32f, 0.28f);
    glm::vec3 sunsetGround = glm::vec3(0.18f, 0.12f, 0.10f);
    glm::vec3 nightGround  = glm::vec3(0.015f, 0.015f, 0.025f);

    glm::vec3 ground = glm::mix(sunsetGround, dayGround, dayFactor);
    ground = glm::mix(ground, nightGround, nightFactor);

    // 4. Color del Sol (Sun Light Color)
    glm::vec3 daySun    = glm::vec3(1.0f, 0.98f, 0.92f);
    glm::vec3 sunsetSun = glm::vec3(1.0f, 0.45f, 0.14f);
    glm::vec3 nightSun  = glm::vec3(0.18f, 0.25f, 0.38f); // Luz plateada lunar

    glm::vec3 sunCol = glm::mix(sunsetSun, daySun, dayFactor);
    sunCol = glm::mix(sunCol, nightSun, nightFactor);

    // 5. Color Ambiente para las Nubes
    glm::vec3 dayCloudAmb    = glm::vec3(0.52f, 0.62f, 0.75f);
    glm::vec3 sunsetCloudAmb = glm::vec3(0.30f, 0.18f, 0.32f); // Sombras púrpuras crepusculares
    glm::vec3 nightCloudAmb  = glm::vec3(0.04f, 0.055f, 0.095f);

    glm::vec3 cloudAmb = glm::mix(sunsetCloudAmb, dayCloudAmb, dayFactor);
    cloudAmb = glm::mix(cloudAmb, nightCloudAmb, nightFactor);

    // 6. Intensidad y resplandor
    float sunInt = glm::mix(1.8f, 1.4f, dayFactor);
    sunInt = glm::mix(sunInt, 0.15f, nightFactor);

    float sunGlw = glm::mix(0.85f, 0.35f, dayFactor);
    sunGlw = glm::mix(sunGlw, 0.15f, nightFactor);

    // Asignar salidas
    currentAtmosphere.zenithColor = zenith;
    currentAtmosphere.horizonColor = horizon;
    currentAtmosphere.groundColor = ground;
    currentAtmosphere.sunColor = sunCol;
    currentAtmosphere.cloudAmbientColor = cloudAmb;
    currentAtmosphere.sunIntensity = sunInt;
    currentAtmosphere.sunGlow = sunGlw;
    currentAtmosphere.sunsetFactor = sunsetFactor;
    currentAtmosphere.nightFactor = nightFactor;

    // Actualizar parámetros para nubes
    params.sunColor = sunCol;
    params.cloudAmbientColor = cloudAmb;
}

// ============================================================================
// Inyección de Parámetros Uniformes
// ============================================================================
void SlipSkyComponent::BindParameters(SlipShader* shader, float currentTime)
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glActiveTexture || !glad_glBindTexture) return;
#endif
    if (!shader) return;

    if (params.dynamicAtmosphere) {
        UpdateAtmosphereFromSun();
    }

    EnsureDefaultTextures();

    shader->use();

    // 1. Textura de densidad/ruido 2D (Unidad 0)
    glActiveTexture(GL_TEXTURE0);
    unsigned int noiseID = (cloudNoiseTexture && cloudNoiseTexture->getID() != 0)
                           ? cloudNoiseTexture->getID()
                           : m_DefaultNoiseTex;
    glBindTexture(GL_TEXTURE_2D, noiseID);
    shader->setInt("u_CloudNoise", 0);

    // 2. Textura de Blue Noise (Unidad 1)
    glActiveTexture(GL_TEXTURE1);
    unsigned int blueID = (blueNoiseTexture && blueNoiseTexture->getID() != 0)
                          ? blueNoiseTexture->getID()
                          : m_DefaultBlueNoiseTex;
    glBindTexture(GL_TEXTURE_2D, blueID);
    shader->setInt("u_BlueNoise", 1);

    // 3. Offset de viento
    glm::vec2 windDirNorm = glm::length(params.windDirection) > 0.0001f
                            ? glm::normalize(params.windDirection)
                            : glm::vec2(1.0f, 0.0f);
    glm::vec2 windOffset = windDirNorm * (params.windSpeed * currentTime * params.cloudScale);
    shader->setVec2("u_WindOffset", windOffset);

    // 4. Uniforms climáticos y geométricos
    glm::vec3 sunDirNorm = glm::length(params.sunDirection) > 0.0001f
                           ? glm::normalize(params.sunDirection)
                           : glm::vec3(0.0f, 1.0f, 0.0f);
    shader->setVec3("u_SunDirection", sunDirNorm);
    shader->setVec3("u_SunColor", params.sunColor);
    shader->setVec3("u_CloudAmbientColor", params.cloudAmbientColor);

    shader->setFloat("u_CloudCoverage", params.cloudCoverage);
    shader->setFloat("u_CloudDensity", params.cloudDensity);
    shader->setFloat("u_WindSpeed", params.windSpeed);
    shader->setFloat("u_CloudBaseHeight", params.cloudBaseHeight);
    shader->setFloat("u_CloudLayerThickness", params.cloudLayerThickness);
    shader->setFloat("u_CloudScale", params.cloudScale);
    shader->setFloat("u_CloudEdgeErosion", params.cloudEdgeErosion);
    shader->setFloat("u_CloudDetailScale", params.cloudDetailScale);

    shader->setFloat("u_Absorption", params.absorption);
    shader->setFloat("u_HenyeyG", params.henyeyG);
    shader->setFloat("u_SilverSpread", params.silverSpread);
    shader->setFloat("u_PowderFactor", params.powderFactor);

    shader->setInt("u_TotalSlices", params.totalSlices);
    shader->setFloat("u_DiscardThreshold", params.discardThreshold);
    shader->setInt("u_RaymarchSteps", params.raymarchSteps);
    shader->setFloat("u_DepthSensitivity", params.depthSensitivity);
    shader->setFloat("u_Time", currentTime);
}

// ============================================================================
// Búsqueda del Componente Activo en la Escena
// ============================================================================
SlipSkyComponent* SlipSkyComponent::FindActive(SlipLevel* level)
{
    if (!level) return nullptr;

    const auto& entities = level->GetEntities();
    for (SlipEntity* ent : entities) {
        if (!ent) continue;
        if (auto sky = ent->GetComponent<SlipSkyComponent>()) {
            if (sky->IsEnabled()) {
                return sky.get();
            }
        }
    }
    return nullptr;
}

// ============================================================================
// Serialización y Deserialización en Formato de Escena
// ============================================================================
void SlipSkyComponent::SerializeTo(std::ostream& out) const
{
    out << "  SlipSkyComponent:\n";
    out << "    SunDirection: " << params.sunDirection.x << " " << params.sunDirection.y << " " << params.sunDirection.z << "\n";
    out << "    SunColor: " << params.sunColor.r << " " << params.sunColor.g << " " << params.sunColor.b << "\n";
    out << "    CloudAmbientColor: " << params.cloudAmbientColor.r << " " << params.cloudAmbientColor.g << " " << params.cloudAmbientColor.b << "\n";
    out << "    CloudCoverage: " << params.cloudCoverage << "\n";
    out << "    CloudDensity: " << params.cloudDensity << "\n";
    out << "    WindSpeed: " << params.windSpeed << "\n";
    out << "    WindDirection: " << params.windDirection.x << " " << params.windDirection.y << "\n";
    out << "    CloudBaseHeight: " << params.cloudBaseHeight << "\n";
    out << "    CloudLayerThickness: " << params.cloudLayerThickness << "\n";
    out << "    CloudScale: " << params.cloudScale << "\n";
    out << "    CloudEdgeErosion: " << params.cloudEdgeErosion << "\n";
    out << "    CloudDetailScale: " << params.cloudDetailScale << "\n";
    out << "    Absorption: " << params.absorption << "\n";
    out << "    HenyeyG: " << params.henyeyG << "\n";
    out << "    SilverSpread: " << params.silverSpread << "\n";
    out << "    PowderFactor: " << params.powderFactor << "\n";
    out << "    TotalSlices: " << params.totalSlices << "\n";
    out << "    DiscardThreshold: " << params.discardThreshold << "\n";
    out << "    RaymarchSteps: " << params.raymarchSteps << "\n";
    out << "    DepthSensitivity: " << params.depthSensitivity << "\n";
    out << "    FollowDirectionalLight: " << (params.followDirectionalLight ? 1 : 0) << "\n";
    out << "    DynamicAtmosphere: " << (params.dynamicAtmosphere ? 1 : 0) << "\n";
    out << "    DownscaleFactor: " << params.downscaleFactor << "\n";
    out << "    CloudShadowsEnabled: " << (params.cloudShadowsEnabled ? 1 : 0) << "\n";
    out << "    CloudShadowIntensity: " << params.cloudShadowIntensity << "\n";
    out << "    CloudNoisePath: " << cloudNoisePath << "\n";
    out << "    BlueNoisePath: " << blueNoisePath << "\n";
}

void SlipSkyComponent::DeserializeKey(const std::string& key, const std::string& value)
{
    std::stringstream ss(value);
    if (key == "SunDirection") ss >> params.sunDirection.x >> params.sunDirection.y >> params.sunDirection.z;
    else if (key == "SunColor") ss >> params.sunColor.r >> params.sunColor.g >> params.sunColor.b;
    else if (key == "CloudAmbientColor") ss >> params.cloudAmbientColor.r >> params.cloudAmbientColor.g >> params.cloudAmbientColor.b;
    else if (key == "CloudCoverage") ss >> params.cloudCoverage;
    else if (key == "CloudDensity") ss >> params.cloudDensity;
    else if (key == "WindSpeed") ss >> params.windSpeed;
    else if (key == "WindDirection") ss >> params.windDirection.x >> params.windDirection.y;
    else if (key == "CloudBaseHeight") ss >> params.cloudBaseHeight;
    else if (key == "CloudLayerThickness") ss >> params.cloudLayerThickness;
    else if (key == "CloudScale") ss >> params.cloudScale;
    else if (key == "CloudEdgeErosion") ss >> params.cloudEdgeErosion;
    else if (key == "CloudDetailScale") ss >> params.cloudDetailScale;
    else if (key == "Absorption") ss >> params.absorption;
    else if (key == "HenyeyG") ss >> params.henyeyG;
    else if (key == "SilverSpread") ss >> params.silverSpread;
    else if (key == "PowderFactor") ss >> params.powderFactor;
    else if (key == "TotalSlices") ss >> params.totalSlices;
    else if (key == "DiscardThreshold") ss >> params.discardThreshold;
    else if (key == "RaymarchSteps") ss >> params.raymarchSteps;
    else if (key == "DepthSensitivity") ss >> params.depthSensitivity;
    else if (key == "FollowDirectionalLight") { int v = 1; ss >> v; params.followDirectionalLight = (v != 0); }
    else if (key == "DynamicAtmosphere") { int v = 1; ss >> v; params.dynamicAtmosphere = (v != 0); }
    else if (key == "DownscaleFactor") ss >> params.downscaleFactor;
    else if (key == "CloudShadowsEnabled") { int v = 1; ss >> v; params.cloudShadowsEnabled = (v != 0); }
    else if (key == "CloudShadowIntensity") ss >> params.cloudShadowIntensity;
    else if (key == "CloudNoisePath") {
        cloudNoisePath = value;
        if (!cloudNoisePath.empty() && SlipFileSystem::Exists(cloudNoisePath)) {
            cloudNoiseTexture = SlipTexture::load(cloudNoisePath.c_str());
        }
    }
    else if (key == "BlueNoisePath") {
        blueNoisePath = value;
        if (!blueNoisePath.empty() && SlipFileSystem::Exists(blueNoisePath)) {
            blueNoiseTexture = SlipTexture::load(blueNoisePath.c_str());
        }
    }
}

// ============================================================================
// Inspección Dear ImGui
// ============================================================================
void SlipSkyComponent::OnDrawInspectorUI()
{
#if defined(SLIP_EDITOR)
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Cielo Dinamico y Nubes Volumetricas Hibridas");
    ImGui::Separator();

    ImGui::Spacing();
    ImGui::Text("Morfologia de la Capa de Nubes:");
    ImGui::SliderFloat("Altura Base (m)", &params.cloudBaseHeight, 100.0f, 5000.0f, "%.1f");
    ImGui::SliderFloat("Grosor de Capa (m)", &params.cloudLayerThickness, 50.0f, 2000.0f, "%.1f");
    ImGui::SliderFloat("Cobertura Global", &params.cloudCoverage, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Densidad Optica", &params.cloudDensity, 0.001f, 0.2f, "%.4f");
    ImGui::SliderFloat("Escala UV XZ", &params.cloudScale, 0.00005f, 0.002f, "%.6f");
    ImGui::SliderFloat("Desperfectos en Bordes", &params.cloudEdgeErosion, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Escala Desperfectos", &params.cloudDetailScale, 1.0f, 12.0f, "%.1f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Dinamica de Viento:");
    ImGui::SliderFloat("Velocidad Viento", &params.windSpeed, -60.0f, 60.0f, "%.1f u/s");
    ImGui::DragFloat2("Direccion Viento", &params.windDirection.x, 0.02f, -1.0f, 1.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Iluminacion y Dispersión:");
    ImGui::DragFloat3("Direccion del Sol", &params.sunDirection.x, 0.01f, -1.0f, 1.0f);
    if (glm::length(params.sunDirection) > 0.0001f) {
        params.sunDirection = glm::normalize(params.sunDirection);
    }
    ImGui::ColorEdit3("Color del Sol", &params.sunColor.r);
    ImGui::ColorEdit3("Color Ambiente Nubes", &params.cloudAmbientColor.r);
    ImGui::SliderFloat("Scattering Frontal (Henyey-G)", &params.henyeyG, 0.0f, 0.95f, "%.2f");
    ImGui::SliderFloat("Factor de Absorcion", &params.absorption, 0.1f, 4.0f, "%.2f");
    ImGui::SliderFloat("Efecto Azucar / Powder", &params.powderFactor, 0.1f, 3.0f, "%.2f");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Sombras Dinamicas en Terreno:");
    ImGui::Checkbox("Proyectar Sombras en Terreno", &params.cloudShadowsEnabled);
    if (params.cloudShadowsEnabled) {
        ImGui::SliderFloat("Intensidad de Sombra en Suelo", &params.cloudShadowIntensity, 0.0f, 1.0f, "%.2f");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Optimizacion Multiplataforma:");
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Perfil Activo: Android (OpenGL ES 3.0 TBDR)");
    ImGui::SliderInt("Slices Instanciados", &params.totalSlices, 4, 32);
    ImGui::SliderFloat("Umbral Discard TBDR", &params.discardThreshold, 0.001f, 0.15f, "%.3f");
#else
    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Perfil Activo: Desktop PC (Raymarching 1/4 + Bilateral)");
    ImGui::SliderInt("Pasos Raymarching", &params.raymarchSteps, 8, 64);
    ImGui::SliderFloat("Sensibilidad Bilateral", &params.depthSensitivity, 0.01f, 1.0f, "%.3f");
    ImGui::SliderInt("Slices (Perfil Android)", &params.totalSlices, 4, 32);
    ImGui::SliderFloat("Discard TBDR (Android)", &params.discardThreshold, 0.001f, 0.15f, "%.3f");
#endif

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Texturas de Nubes:");

    // Ruido 2D
    std::string noiseLabel = cloudNoisePath.empty() ? "(Procedural Generada en GPU)" : cloudNoisePath;
    ImGui::Text("Ruido 2D: %s", noiseLabel.c_str());
    if (cloudNoiseTexture && cloudNoiseTexture->getID() != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)cloudNoiseTexture->getID(), ImVec2(64, 64));
    } else if (m_DefaultNoiseTex != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)m_DefaultNoiseTex, ImVec2(64, 64));
    }

    // Blue Noise
    std::string blueLabel = blueNoisePath.empty() ? "(Procedural Generada en GPU)" : blueNoisePath;
    ImGui::Text("Blue Noise: %s", blueLabel.c_str());
    if (blueNoiseTexture && blueNoiseTexture->getID() != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)blueNoiseTexture->getID(), ImVec2(64, 64));
    } else if (m_DefaultBlueNoiseTex != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)m_DefaultBlueNoiseTex, ImVec2(64, 64));
    }

    if (ImGui::Button("Restablecer a Texturas Procedurales")) {
        cloudNoiseTexture = nullptr;
        blueNoiseTexture = nullptr;
        cloudNoisePath.clear();
        blueNoisePath.clear();
    }
#endif
}
