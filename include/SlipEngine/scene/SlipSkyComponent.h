#pragma once

#ifndef SLIP_SKY_COMPONENT_H
#define SLIP_SKY_COMPONENT_H

#include "SlipEngine.h"
#include "SlipEngine/scene/SlipComponent.h"
#include <glm/glm.hpp>
#include <string>
#include <iosfwd>

class SlipTexture;
class SlipShader;
class SlipLevel;

// ============================================================================
// Salidas calculadas de atmósfera dinámica (Día, Atardecer, Crepúsculo, Noche)
// ============================================================================
struct SLIPENGINE_API SlipAtmosphereOutputs
{
    glm::vec3 zenithColor        = glm::vec3(0.18f, 0.44f, 0.82f);
    glm::vec3 horizonColor       = glm::vec3(0.68f, 0.82f, 0.94f);
    glm::vec3 groundColor        = glm::vec3(0.35f, 0.32f, 0.28f);
    glm::vec3 sunColor           = glm::vec3(1.0f, 0.98f, 0.92f);
    glm::vec3 cloudAmbientColor  = glm::vec3(0.52f, 0.62f, 0.75f);
    float     sunIntensity       = 1.4f;
    float     sunGlow            = 0.35f;
    float     sunsetFactor       = 0.0f;
    float     nightFactor        = 0.0f;
};

// ============================================================================
// Parámetros climáticos, lumínicos y atmosféricos compartidos
// ============================================================================
struct SLIPENGINE_API SlipSkyParameters
{
    // Iluminación celeste y solar
    glm::vec3 sunDirection        = glm::normalize(glm::vec3(0.35f, 0.75f, 0.45f));
    glm::vec3 sunColor            = glm::vec3(1.0f, 0.95f, 0.88f);
    glm::vec3 cloudAmbientColor   = glm::vec3(0.52f, 0.62f, 0.75f);

    // Integración de cielo dinámico
    bool      followDirectionalLight = true;     // Sincronizar automáticamente con la luz direccional de la escena
    bool      dynamicAtmosphere      = true;     // Transiciones cromáticas dinámicas (Día, Atardecer, Noche)

    // Morfología y dinámica de viento
    float     cloudCoverage       = 0.52f;        // 0.0 (despejado) .. 1.0 (cubierto)
    float     cloudDensity        = 0.08f;        // Coeficiente de densidad volumétrica
    float     windSpeed           = 12.0f;        // Velocidad de arrastre en unidades/segundo
    glm::vec2 windDirection       = glm::vec2(1.0f, 0.2f); // Dirección horizontal del viento
    float     cloudBaseHeight     = 120.0f;       // Altura inicial del manto nuboso (metros)
    float     cloudLayerThickness = 70.0f;        // Espesor vertical de la capa de nubes (metros)
    float     cloudScale          = 0.0015f;      // Frecuencia espacial UV de nubes en XZ
    float     cloudEdgeErosion    = 0.62f;        // Desperfectos y rugosidad en bordes (0 = suave, 1 = rasgado/deshilachado)
    float     cloudDetailScale    = 4.5f;         // Escala de micro-detalle para desperfectos y bordes fractales

    // Propiedades ópticas de dispersión (Beer-Lambert & Henyey-Greenstein)
    float     absorption          = 1.1f;         // Coeficiente de extinción óptica
    float     henyeyG             = 0.65f;        // Asimetría de scattering frontal (0 = isotrópico)
    float     silverSpread        = 0.95f;        // Exponente para el borde plateado solar
    float     powderFactor        = 1.25f;        // Factor de sugar/powder effect en micro-bordes

    // Ajustes específicos por backend de plataforma
    int       totalSlices         = 16;           // Instancias de planos horizontales (Android TBDR: 12-16)
    float     discardThreshold    = 0.015f;       // Umbral de descarte temprano (discard) en TBDR
    int       raymarchSteps       = 32;           // Pasos de muestreo continuo en PC (Alta fidelidad)
    float     depthSensitivity    = 0.15f;        // Sensibilidad bilateral contra el buffer de profundidad
    int       downscaleFactor     = 2;            // Escala PC: 1 = Nativa (Ultra), 2 = Media Res (Alta/Equilibrada), 4 = 1/4 Res

    // Proyección de sombras de nubes sobre el terreno (PC y Android)
    bool      cloudShadowsEnabled   = true;       // Proyectar sombras dinámicas sobre el suelo
    float     cloudShadowIntensity  = 0.65f;      // Atenuación de luz directa por sombra de nubes [0..1]
};

// ============================================================================
// SlipSkyComponent: Componente desacoplado de cielo dinámico y nubes volumétricas
// ============================================================================
class SLIPENGINE_API SlipSkyComponent : public SlipComponent
{
public:
    SlipSkyParameters params;
    SlipAtmosphereOutputs currentAtmosphere;

    // Punteros y rutas a texturas (2D densidad tileable y Blue Noise estocástico)
    SlipTexture* cloudNoiseTexture = nullptr;
    SlipTexture* blueNoiseTexture  = nullptr;
    std::string  cloudNoisePath;
    std::string  blueNoisePath;

    SlipSkyComponent();
    ~SlipSkyComponent() override;

    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    // Actualiza los colores atmosféricos en base al ángulo solar actual
    void UpdateAtmosphereFromSun();

    // Inyecta las variables uniformes climáticas en el shader activo
    void BindParameters(SlipShader* shader, float currentTime);

    // Inicializa texturas procedurales de respaldo en GPU si no se asignaron externas
    void EnsureDefaultTextures();
    unsigned int GetDefaultNoiseTex() const { return m_DefaultNoiseTex; }
    float GetCurrentTime() const { return m_CurrentTime; }

    // Serialización / Deserialización en texto plano
    void SerializeTo(std::ostream& out) const;
    void DeserializeKey(const std::string& key, const std::string& value);

    // Localiza el primer SlipSkyComponent activo en el nivel actual
    static SlipSkyComponent* FindActive(SlipLevel* level);

private:
    float        m_CurrentTime        = 0.0f;
    unsigned int m_DefaultNoiseTex    = 0;
    unsigned int m_DefaultBlueNoiseTex= 0;

    void GenerateDefaultTextures();
    void ReleaseDefaultTextures();
};

#endif // !SLIP_SKY_COMPONENT_H
