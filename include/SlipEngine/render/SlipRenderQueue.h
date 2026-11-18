#pragma once

#ifndef SLIP_RENDER_QUEUE_H
#define SLIP_RENDER_QUEUE_H

#include "SlipEngine.h"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cstdint>

class SlipMesh;
class SlipMaterial;
class SlipShader;
class SlipEntity;
class SlipPrimitiveEntity;

enum class RenderPassType : uint8_t
{
    Shadow = 0,
    Opaque = 1,
    AlphaClip = 2,
    Transparent = 3
};

struct SLIPENGINE_API RenderCommand
{
    uint64_t sortKey = 0;

    SlipEntity* entity = nullptr;
    SlipMesh* mesh = nullptr;
    uint32_t submeshIndex = 0;
    SlipMaterial* material = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);

    bool doubleSided = false;
    bool wireframe = false;
    bool isPrimitive = false;
    bool isStencilSelected = false;
    SlipPrimitiveEntity* primEntity = nullptr;

    bool hasBones = false;
    int skinningQuality = 4;
    const glm::mat4* boneTransforms = nullptr;
    size_t boneTransformCount = 0;

    // Soporte de rango custom (V-Clust Cluster / LOD Override) y tinte de depuración visual
    bool useCustomIndexRange = false;
    uint32_t customVAO = 0;
    uint32_t customFirstIndex = 0;
    uint32_t customIndexCount = 0;
    bool useDebugTint = false;
    glm::vec3 debugTintColor = glm::vec3(1.0f);
    int debugClusterVisMode = 0; // 0: Normal, 1: Color por Cluster/Primitiva en GPU (1 Draw Call), 2: Heatmap LOD

    // Soporte de Lightmaps UV2
    bool hasLightmap = false;
    glm::vec4 lightmapScaleOffset{ 1.0f, 1.0f, 0.0f, 0.0f };

    static uint64_t MakeSortKey(RenderPassType pass, uint32_t shaderId, uint32_t matId, uint32_t vaoId, float depth, bool frontToBack = true);
};

class SLIPENGINE_API SlipRenderQueue
{
public:
    SlipRenderQueue() = default;
    ~SlipRenderQueue() = default;

    void Clear();
    void Submit(const RenderCommand& cmd);
    void Sort();

    // Ejecuta el vaciado de la cola reduciendo al mínimo los cambios de estado
    void Flush(const std::function<void(SlipShader*)>& onBindShaderGlobals);

    size_t GetCommandCount() const { return m_Commands.size(); }
    uint32_t GetActualDrawCalls() const { return m_ActualDrawCalls; }
    const std::vector<RenderCommand>& GetCommands() const { return m_Commands; }

private:
    std::vector<RenderCommand> m_Commands;
    uint32_t m_ActualDrawCalls = 0;
};

#endif // !SLIP_RENDER_QUEUE_H
