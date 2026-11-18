#pragma once

#ifndef SLIP_CLUSTER_BUILDER_H
#define SLIP_CLUSTER_BUILDER_H

#include "SlipEngine/render/SlipCluster.h"
#include "SlipEngine/render/SlipMesh.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

// ============================================================================
// V-Clust (Voxel-Anchored Cluster LOD) - Offline Baker (Exclusivo del Editor)
// ============================================================================

struct SlipClusterBuildOptions
{
    uint32_t maxVerticesPerCluster  = 128;   // Límite de vértices por meshlet
    uint32_t maxTrianglesPerCluster = 124;   // ~64 a 128 triángulos por clúster
    float    coneWeight             = 0.5f;  // Balance entre localidad espacial y cono normal
    float    baseVoxelSize          = 0.0f;  // Si <= 0.0f, se calcula automáticamente según la diagonal del AABB
    uint32_t maxHierarchyLevels     = 6;     // Profundidad máxima de la pirámide LOD
    uint32_t groupSize              = 4;     // Agrupación de 2 a 4 clusters vecinos por nivel
};

struct SlipClusterBuildResult
{
    SlipClusterHierarchyHeader     header;
    std::vector<SlipCluster>       clusters;
    std::vector<SlipClusterVertex> quantizedVertices;
    std::vector<Vertex>            unpackedVertices; // Compatible con el VBO maestro de SlipMesh
    std::vector<uint32_t>          indices;          // Búfer de índices contiguo de todos los niveles LOD
};

/**
 * @brief Tabla Hash Espacial Plana 3D de Anclaje Volumétrico.
 *
 * Previene grietas (cracks/T-junctions) entre clusters adyacentes durante la simplificación
 * sin requerir bloqueo analítico de bordes:
 *
 * Demostración matemática de invariancia de frontera:
 * Sea una arista o vértice compartido $v_b$ perteneciente a la frontera entre dos clusters adyacentes $C_A$ y $C_B$.
 * Dado que las coordenadas discretas de celda $(X_c, Y_c, Z_c) = \lfloor v / S \rfloor$ dependen únicamente de la
 * posición espacial global $v$ y del paso de rejilla $S$ (constante para todo el nivel $L$ de la jerarquía):
 * 1. Ambos clusters proyectan $v_b$ exactamente a la misma tupla entera $(X_c, Y_c, Z_c)$ y al mismo bucket
 *    determinista $\text{Hash}(X_c, Y_c, Z_c)$.
 * 2. Al acumular globalmente todos los vértices que caen en la celda $(X_c, Y_c, Z_c)$ antes de emitir la
 *    geometría simplificada del nivel $L$, el centroide volumétrico resultante $\bar{v}_{cell}$ es único y global.
 * 3. Cualquier par de vértices de frontera $v \in C_A$ y $v \in C_B$ que colapsen dentro de la misma celda de
 *    anclaje adoptan idénticas coordenadas cuantizadas $\bar{v}_{cell}$ y atributos normalizados, garantizando
 *    continuidad $C^0$ hermética (watertight) entre clusters vecinos.
 */
class SLIPENGINE_API SlipSpatialHashGrid3D
{
public:
    static constexpr uint32_t DEFAULT_TABLE_SIZE = 65536; // Potencia de 2 / tamaño fijo plano en memoria

    struct CellEntry
    {
        int32_t   ix = 0, iy = 0, iz = 0;
        glm::vec3 sumPos      = glm::vec3(0.0f);
        glm::vec3 sumNormal   = glm::vec3(0.0f);
        glm::vec3 sumTangent  = glm::vec3(0.0f);
        glm::vec3 sumBitangent= glm::vec3(0.0f);
        glm::vec2 sumUV       = glm::vec2(0.0f);
        glm::vec2 firstUV     = glm::vec2(0.0f);
        float     maxUvDelta  = 0.0f;
        uint32_t  count       = 0;
        uint32_t  canonicalVertexIndex = 0xFFFFFFFFu;
        bool      occupied    = false;
    };

    explicit SlipSpatialHashGrid3D(float cellSize, uint32_t tableSize = DEFAULT_TABLE_SIZE);

    // Función hash espacial exacta basada en primos grandes
    static inline uint32_t ComputeHash(int32_t xc, int32_t yc, int32_t zc, uint32_t tableSize)
    {
        const uint32_t ux = static_cast<uint32_t>(xc);
        const uint32_t uy = static_cast<uint32_t>(yc);
        const uint32_t uz = static_cast<uint32_t>(zc);
        const uint32_t h  = (ux * 73856093u) ^ (uy * 19349663u) ^ (uz * 83492791u);
        return h % tableSize;
    }

    void Clear(float newCellSize);

    // Inserta un vértice o extremo de arista compartida en la celda correspondiente
    uint32_t InsertVertex(const Vertex& v);

    // Inserta explícitamente ambos extremos de una arista compartida entre clusters
    void InsertSharedEdge(const Vertex& v0, const Vertex& v1);

    // Finaliza los centroides volumétricos de cada celda activa
    void FinalizeCentroids();

    // Obtiene el vértice colapsado hacia el centroide de su celda hash correspondiente
    Vertex GetCollapsedVertex(const Vertex& originalVertex, uint32_t* outCellSlot = nullptr) const;

    float GetCellSize() const { return m_CellSize; }
    uint32_t GetTableSize() const { return m_TableSize; }

private:
    uint32_t FindOrCreateSlot(int32_t ix, int32_t iy, int32_t iz);
    int32_t  FindSlot(int32_t ix, int32_t iy, int32_t iz) const;
    void     Discretize(const glm::vec3& pos, int32_t& outX, int32_t& outY, int32_t& outZ) const;

    float                  m_CellSize  = 0.01f;
    float                  m_InvCellSize = 100.0f;
    uint32_t               m_TableSize = DEFAULT_TABLE_SIZE;
    std::vector<CellEntry> m_Table;
    std::vector<uint32_t>  m_ActiveSlots;
};

class SLIPENGINE_API SlipClusterBuilder
{
public:
    /**
     * @brief Construye la jerarquía completa de clusters V-Clust (LOD 0 a LOD N) para una malla estática.
     * Utiliza meshopt_buildMeshlets para la segmentación inicial LOD 0 y SlipSpatialHashGrid3D para
     * la simplificación volumétrica jerárquica libre de grietas.
     */
    static bool BuildFromSubmeshes(
        const std::vector<SlipMesh::Mesh>& submeshes,
        const SlipClusterBuildOptions& options,
        SlipClusterBuildResult& outResult
    );

    /**
     * @briefConstruye la jerarquía V-Clust directamente desde un buffer unificado de vértices e índices.
     */
    static bool BuildFromUnifiedMesh(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t materialIndex,
        uint32_t submeshIndex,
        const SlipClusterBuildOptions& options,
        SlipClusterBuildResult& outResult
    );

    /**
     * @brief Serializa el resultado de V-Clust a un flujo de bytes binario listo para .model_data o game.pak.
     */
    static void SerializeClusterData(
        const SlipClusterBuildResult& buildResult,
        std::vector<uint8_t>& outBuffer
    );

    /**
     * @brief Deserializa un bloque binario VCLUST01 desde memoria.
     */
    static bool DeserializeClusterData(
        const uint8_t* data,
        size_t dataSize,
        SlipClusterBuildResult& outResult
    );

    /**
     * @brief Compila y empaqueta secuencialmente todos los assets cocinados del proyecto en un archivo game.pak.
     */
    static bool CompileGamePak(
        const std::string& projectRootDir,
        const std::string& outputPakPath
    );
};

#endif // !SLIP_CLUSTER_BUILDER_H
