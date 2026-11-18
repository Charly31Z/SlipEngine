#pragma once

#ifndef SLIP_CLUSTER_H
#define SLIP_CLUSTER_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/SlipGL.h"
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>

// ============================================================================
// V-Clust (Voxel-Anchored Cluster LOD) - Estructuras de Datos Binarias
// Alineación estricta y empaquetamiento eficiente para memoria y disco
// Compatible con OpenGL 4.5+ (PC) y OpenGL ES 3.0+ (Android TBDR)
// ============================================================================

#pragma pack(push, 1)

/**
 * @brief Formato de vértice cuantizado y comprimido para clusters V-Clust (24 bytes / alineado a 4 bytes).
 * - Position: 3x float32 (12 bytes)
 * - Normal:   GL_INT_2_10_10_10_REV (4 bytes)
 * - Tangent:  GL_INT_2_10_10_10_REV (4 bytes, el bit W almacena el signo de la bitangente)
 * - UV:       2x GL_HALF_FLOAT (4 bytes)
 */
struct alignas(4) SlipClusterVertex
{
    glm::vec3 position;       // 12 bytes (Offset 0)
    uint32_t  packedNormal;   // 4 bytes  (Offset 12) -> GL_INT_2_10_10_10_REV
    uint32_t  packedTangent;  // 4 bytes  (Offset 16) -> GL_INT_2_10_10_10_REV
    uint16_t  uv[2];          // 4 bytes  (Offset 20) -> GL_HALF_FLOAT (2x float16)

    // Empaqueta un vector normalizado [-1.0, 1.0] en formato GL_INT_2_10_10_10_REV (con signo)
    static inline uint32_t PackInt2_10_10_10_Rev(const glm::vec3& v, float w = 0.0f)
    {
        return glm::packSnorm3x10_1x2(glm::vec4(
            glm::clamp(v.x, -1.0f, 1.0f),
            glm::clamp(v.y, -1.0f, 1.0f),
            glm::clamp(v.z, -1.0f, 1.0f),
            glm::clamp(w,   -1.0f, 1.0f)
        ));
    }

    // Desempaqueta un uint32_t en formato GL_INT_2_10_10_10_REV hacia glm::vec4
    static inline glm::vec4 UnpackInt2_10_10_10_Rev(uint32_t packed)
    {
        return glm::unpackSnorm3x10_1x2(packed);
    }

    // Empaqueta coordenadas UV float32 a IEEE 754 half-float (GL_HALF_FLOAT)
    static inline void PackHalfUV(const glm::vec2& texCoords, uint16_t outUV[2])
    {
        outUV[0] = glm::packHalf1x16(texCoords.x);
        outUV[1] = glm::packHalf1x16(texCoords.y);
    }

    // Desempaqueta coordenadas UV desde GL_HALF_FLOAT a glm::vec2
    static inline glm::vec2 UnpackHalfUV(const uint16_t inUV[2])
    {
        return glm::vec2(glm::unpackHalf1x16(inUV[0]), glm::unpackHalf1x16(inUV[1]));
    }
};
static_assert(sizeof(SlipClusterVertex) == 24, "SlipClusterVertex debe medir exactamente 24 bytes.");

/**
 * @brief Estructura binaria de un nodo/clúster dentro del grafo jerárquico V-Clust (80 bytes, alineada a 16 bytes).
 * Diseñada para caber en líneas de caché de CPU y en SSBOs std430 de GPU (OpenGL 4.5+).
 */
struct alignas(16) SlipCluster
{
    // 1. Offsets y conteos en los búferes lineales contiguos (VBO / EBO) [16 bytes]
    // Cada clúster hoja o interno almacena ~64 a 128 triángulos.
    uint32_t vertexOffset  = 0;
    uint32_t vertexCount   = 0;
    uint32_t indexOffset   = 0;
    uint32_t indexCount    = 0;   // Número de índices (triángulos * 3)

    // 2. Esfera delimitadora local (Bounding Sphere) [16 bytes]
    glm::vec3 center       = glm::vec3(0.0f);
    float     radius       = 0.0f;

    // 3. Cono normal promedio para Backface Cone Culling [16 bytes]
    // Si dot(normalize(center - cameraPos), coneAxis) >= coneCutoff, el clúster entero mira hacia atrás.
    glm::vec3 coneAxis     = glm::vec3(0.0f, 1.0f, 0.0f);
    float     coneCutoff   = 1.0f; // sin(coneHalfAngle) o umbral de rechazo en producto punto

    // 4. Métrica de error volumétrico local y jerarquía del DAG/Árbol [16 bytes]
    float     lodError     = 0.0f; // Error geométrico en unidades de mundo (LOD 0 = 0.0f)
    float     parentError  = 1e30f;// Error del clúster padre para transición monótona sin saltos
    int32_t   parentId     = -1;   // ID del clúster padre (-1 si es nodo raíz del árbol)
    int32_t   firstChildId = -1;   // Índice inicial de los clusters hijos (-1 si es hoja LOD 0)

    // 5. Metadatos de topología, nivel LOD y material [16 bytes]
    uint16_t  childCount   = 0;    // Cantidad de hijos directos (2 a 4 en la jerarquía)
    uint16_t  lodLevel     = 0;    // Nivel en la pirámide (0 = malla original densa, N = más simplificado)
    uint32_t  materialIndex= 0;    // Slot de material asociado para batching por estado
    uint32_t  submeshIndex = 0;    // Índice de submalla de origen
    uint32_t  flags        = 0;    // Flags de estado (bit 0: raíz, bit 1: hoja, bit 2: doble cara)
};
static_assert(sizeof(SlipCluster) == 80, "SlipCluster debe tener un tamaño exacto de 80 bytes (múltiplo de 16).");

/**
 * @brief Comando estándar de OpenGL para glMultiDrawElementsIndirect (OpenGL 4.3+ / 4.5+).
 */
struct alignas(4) DrawElementsIndirectCommand
{
    uint32_t count         = 0; // Cantidad de índices a renderizar
    uint32_t instanceCount = 1; // Cantidad de instancias (1 por defecto)
    uint32_t firstIndex    = 0; // Offset en unidades de índice dentro del EBO
    int32_t  baseVertex    = 0; // Offset sumado a cada índice para leer del VBO
    uint32_t baseInstance  = 0; // Offset de instancia (0 en ES 3.0 / GL 4.5 base)
};
static_assert(sizeof(DrawElementsIndirectCommand) == 20, "DrawElementsIndirectCommand debe medir 20 bytes según especificación OpenGL.");

/**
 * @brief Cabecera de bloque binario V-Clust dentro de archivos .model_data y paquetes game.pak.
 */
struct alignas(8) SlipClusterHierarchyHeader
{
    char     magic[8]          = {'V', 'C', 'L', 'U', 'S', 'T', '0', '1'};
    uint32_t version           = 1;
    uint32_t totalClusters     = 0;
    uint32_t rootClusterOffset = 0;
    uint32_t rootClusterCount  = 0;
    uint32_t totalVertices     = 0; // Vértices cuantizados en el pool de clusters
    uint32_t totalIndices      = 0; // Índices contiguos de todos los niveles LOD
    float    baseVoxelSize     = 0.01f;
    uint32_t maxLodLevel       = 0;
};
static_assert(sizeof(SlipClusterHierarchyHeader) == 40, "SlipClusterHierarchyHeader debe medir 40 bytes.");

/**
 * @brief Cabecera y entrada del archivo empaquetado secuencial game.pak.
 */
struct alignas(8) SlipPakHeader
{
    char     magic[8]   = {'S', 'L', 'I', 'P', 'P', 'A', 'K', '1'};
    uint32_t version    = 1;
    uint32_t entryCount = 0;
};

struct alignas(8) SlipPakEntry
{
    char     assetPath[192] = { 0 };
    char     guid[40]       = { 0 };
    uint64_t dataOffset     = 0;
    uint64_t dataSize       = 0;
    uint32_t assetType      = 0; // 1 = VClustModel, 2 = Material, 3 = Texture, 0 = Generic
    uint32_t reserved       = 0;
};

#pragma pack(pop)

// Configura los atributos de vértice OpenGL para un VBO con formato SlipClusterVertex
inline void SetupSlipClusterVertexAttributes()
{
    // Location 0: Position (3x GL_FLOAT)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SlipClusterVertex),
                          reinterpret_cast<const void*>(offsetof(SlipClusterVertex, position)));

    // Location 1: Normal comprimida (GL_INT_2_10_10_10_REV normalizado)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(SlipClusterVertex),
                          reinterpret_cast<const void*>(offsetof(SlipClusterVertex, packedNormal)));

    // Location 2: Coordenadas UV cuantizadas (2x GL_HALF_FLOAT)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_HALF_FLOAT, GL_FALSE, sizeof(SlipClusterVertex),
                          reinterpret_cast<const void*>(offsetof(SlipClusterVertex, uv)));

    // Location 3: Tangente comprimida (GL_INT_2_10_10_10_REV normalizado)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(SlipClusterVertex),
                          reinterpret_cast<const void*>(offsetof(SlipClusterVertex, packedTangent)));
}

#endif // !SLIP_CLUSTER_H
