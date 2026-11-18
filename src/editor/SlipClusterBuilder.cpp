#include "SlipEngine/editor/SlipClusterBuilder.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <thread>
#include <future>

// ============================================================================
// Implementación de SlipSpatialHashGrid3D (Tabla Hash Espacial Plana 3D)
// ============================================================================

SlipSpatialHashGrid3D::SlipSpatialHashGrid3D(float cellSize, uint32_t tableSize)
    : m_TableSize(tableSize > 0 ? tableSize : DEFAULT_TABLE_SIZE)
{
    m_Table.resize(m_TableSize);
    m_ActiveSlots.reserve(4096);
    Clear(cellSize);
}

void SlipSpatialHashGrid3D::Clear(float newCellSize)
{
    m_CellSize = (newCellSize > 1e-6f) ? newCellSize : 1e-4f;
    m_InvCellSize = 1.0f / m_CellSize;

    for (uint32_t slot : m_ActiveSlots) {
        m_Table[slot] = CellEntry{};
    }
    m_ActiveSlots.clear();
}

void SlipSpatialHashGrid3D::Discretize(const glm::vec3& pos, int32_t& outX, int32_t& outY, int32_t& outZ) const
{
    outX = static_cast<int32_t>(std::floor(pos.x * m_InvCellSize));
    outY = static_cast<int32_t>(std::floor(pos.y * m_InvCellSize));
    outZ = static_cast<int32_t>(std::floor(pos.z * m_InvCellSize));
}

uint32_t SlipSpatialHashGrid3D::FindOrCreateSlot(int32_t ix, int32_t iy, int32_t iz)
{
    // Si la ocupación supera el 70%, duplicar la tabla plana y re-hashear para evitar colisiones largas
    if (m_ActiveSlots.size() * 10 >= m_TableSize * 7) {
        std::vector<CellEntry> oldEntries;
        oldEntries.reserve(m_ActiveSlots.size());
        for (uint32_t s : m_ActiveSlots) {
            oldEntries.push_back(m_Table[s]);
        }
        m_TableSize *= 2;
        m_Table.assign(m_TableSize, CellEntry{});
        m_ActiveSlots.clear();
        for (const auto& entry : oldEntries) {
            uint32_t h = ComputeHash(entry.ix, entry.iy, entry.iz, m_TableSize);
            while (m_Table[h].occupied) {
                h = (h + 1) % m_TableSize;
            }
            m_Table[h] = entry;
            m_ActiveSlots.push_back(h);
        }
    }

    uint32_t slot = ComputeHash(ix, iy, iz, m_TableSize);
    for (uint32_t probe = 0; probe < m_TableSize; ++probe) {
        CellEntry& entry = m_Table[slot];
        if (!entry.occupied) {
            entry.occupied = true;
            entry.ix = ix;
            entry.iy = iy;
            entry.iz = iz;
            m_ActiveSlots.push_back(slot);
            return slot;
        }
        if (entry.ix == ix && entry.iy == iy && entry.iz == iz) {
            return slot;
        }
        slot = (slot + 1) % m_TableSize;
    }
    return slot;
}

int32_t SlipSpatialHashGrid3D::FindSlot(int32_t ix, int32_t iy, int32_t iz) const
{
    uint32_t slot = ComputeHash(ix, iy, iz, m_TableSize);
    for (uint32_t probe = 0; probe < m_TableSize; ++probe) {
        const CellEntry& entry = m_Table[slot];
        if (!entry.occupied) {
            return -1;
        }
        if (entry.ix == ix && entry.iy == iy && entry.iz == iz) {
            return static_cast<int32_t>(slot);
        }
        slot = (slot + 1) % m_TableSize;
    }
    return -1;
}

uint32_t SlipSpatialHashGrid3D::InsertVertex(const Vertex& v)
{
    int32_t ix, iy, iz;
    Discretize(v.Position, ix, iy, iz);
    uint32_t slot = FindOrCreateSlot(ix, iy, iz);
    CellEntry& entry = m_Table[slot];

    if (entry.count == 0) {
        entry.firstUV = v.TexCoords;
        entry.maxUvDelta = 0.0f;
    } else {
        float uvDist = glm::length(v.TexCoords - entry.firstUV);
        if (uvDist > entry.maxUvDelta) {
            entry.maxUvDelta = uvDist;
        }
    }

    entry.sumPos       += v.Position;
    entry.sumNormal    += v.Normal;
    entry.sumTangent   += v.Tangent;
    entry.sumBitangent += v.Bitangent;
    entry.sumUV        += v.TexCoords;
    entry.count        += 1;

    return slot;
}

void SlipSpatialHashGrid3D::InsertSharedEdge(const Vertex& v0, const Vertex& v1)
{
    // Al insertar aristas compartidas entre clusters adyacentes, ambos extremos se acumulan
    // en sus respectivas celdas de anclaje. Si la longitud de la arista es menor que S y caen
    // en la misma celda discreta, la arista colapsa automáticamente al centroide común sin dejar grietas.
    InsertVertex(v0);
    InsertVertex(v1);
}

void SlipSpatialHashGrid3D::FinalizeCentroids()
{
    for (uint32_t slot : m_ActiveSlots) {
        CellEntry& entry = m_Table[slot];
        if (entry.count > 0) {
            float invCount = 1.0f / static_cast<float>(entry.count);
            entry.sumPos *= invCount;
            entry.sumUV  *= invCount;

            float nLen = glm::length(entry.sumNormal);
            entry.sumNormal = (nLen > 1e-6f) ? (entry.sumNormal / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);

            float tLen = glm::length(entry.sumTangent);
            entry.sumTangent = (tLen > 1e-6f) ? (entry.sumTangent / tLen) : glm::vec3(1.0f, 0.0f, 0.0f);

            float bLen = glm::length(entry.sumBitangent);
            entry.sumBitangent = (bLen > 1e-6f) ? (entry.sumBitangent / bLen) : glm::vec3(0.0f, 0.0f, 1.0f);

            entry.count = 1; // Marcado como promediado
        }
    }
}

Vertex SlipSpatialHashGrid3D::GetCollapsedVertex(const Vertex& originalVertex, uint32_t* outCellSlot) const
{
    int32_t ix, iy, iz;
    Discretize(originalVertex.Position, ix, iy, iz);
    int32_t slot = FindSlot(ix, iy, iz);
    if (outCellSlot) {
        *outCellSlot = (slot >= 0) ? static_cast<uint32_t>(slot) : 0xFFFFFFFFu;
    }

    if (slot < 0) {
        return originalVertex;
    }

    const CellEntry& entry = m_Table[slot];
    Vertex collapsed = originalVertex;
    collapsed.Position  = entry.sumPos;
    collapsed.Normal    = entry.sumNormal;
    collapsed.Tangent   = entry.sumTangent;
    collapsed.Bitangent = entry.sumBitangent;
    // Si la celda cruza una costura de islas UV (UV seam > 0.02), preservar la coordenada UV original
    // del vértice para no estirar triángulos a través de regiones distintas del atlas de textura
    if (entry.maxUvDelta <= 0.02f) {
        collapsed.TexCoords = entry.sumUV;
    }
    return collapsed;
}

// ============================================================================
// Implementación de SlipClusterBuilder (Voxelización y Jerarquía Multi-Nivel)
// ============================================================================

namespace {

static SlipClusterVertex QuantizeToClusterVertex(const Vertex& v)
{
    SlipClusterVertex cv{};
    cv.position = v.Position;

    glm::vec3 n = v.Normal;
    float nLen = glm::length(n);
    n = (nLen > 1e-6f) ? (n / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 t = v.Tangent;
    float tLen = glm::length(t);
    t = (tLen > 1e-6f) ? (t / tLen) : glm::vec3(1.0f, 0.0f, 0.0f);

    // Determinar el signo de la bitangente para reconstruir B = cross(N, T) * sign en el shader
    float handedness = (glm::dot(glm::cross(n, t), v.Bitangent) < 0.0f) ? -1.0f : 1.0f;

    cv.packedNormal  = SlipClusterVertex::PackInt2_10_10_10_Rev(n, 0.0f);
    cv.packedTangent = SlipClusterVertex::PackInt2_10_10_10_Rev(t, handedness);
    SlipClusterVertex::PackHalfUV(v.TexCoords, cv.uv);

    return cv;
}

struct EdgeKey
{
    uint32_t v0;
    uint32_t v1;

    bool operator==(const EdgeKey& o) const { return v0 == o.v0 && v1 == o.v1; }
};

struct EdgeKeyHash
{
    size_t operator()(const EdgeKey& k) const
    {
        return (static_cast<size_t>(k.v0) * 73856093u) ^ (static_cast<size_t>(k.v1) * 19349663u);
    }
};

} // namespace

bool SlipClusterBuilder::BuildFromUnifiedMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    uint32_t materialIndex,
    uint32_t submeshIndex,
    const SlipClusterBuildOptions& options,
    SlipClusterBuildResult& outResult)
{
    SlipMesh::Mesh sm;
    sm.materialIndex = static_cast<int>(materialIndex);
    sm.vertices = vertices;
    sm.indices = indices;
    sm.baseVertex = 0;
    sm.vertexCount = static_cast<uint32_t>(vertices.size());
    sm.firstIndex = 0;
    sm.indexCount = static_cast<uint32_t>(indices.size());

    std::vector<SlipMesh::Mesh> list = { sm };
    return BuildFromSubmeshes(list, options, outResult);
}

bool SlipClusterBuilder::BuildFromSubmeshes(
    const std::vector<SlipMesh::Mesh>& submeshes,
    const SlipClusterBuildOptions& options,
    SlipClusterBuildResult& outResult)
{
    outResult = SlipClusterBuildResult{};
    if (submeshes.empty()) return false;

    // 1. Calcular el AABB global del modelo para determinar el tamaño base de celda S si no se especificó
    glm::vec3 globalMin(1e30f), globalMax(-1e30f);
    size_t totalInputTriangles = 0;
    for (const auto& sm : submeshes) {
        for (const auto& v : sm.vertices) {
            globalMin = glm::min(globalMin, v.Position);
            globalMax = glm::max(globalMax, v.Position);
        }
        totalInputTriangles += sm.indices.size() / 3;
    }

    if (totalInputTriangles == 0) return false;

    float diagonal = glm::length(globalMax - globalMin);
    if (diagonal < 1e-4f) diagonal = 1.0f;

    // Tamaño discreto base S para el primer nivel de simplificación volumétrica (LOD 1)
    float baseVoxelSize = (options.baseVoxelSize > 1e-6f) ? options.baseVoxelSize : (diagonal * 0.005f);
    outResult.header.baseVoxelSize = baseVoxelSize;

    const size_t maxVertices = std::clamp<size_t>(options.maxVerticesPerCluster, 32, 256);
    const size_t maxTriangles = std::clamp<size_t>(options.maxTrianglesPerCluster, 32, 128);
    const float coneWeight = std::clamp(options.coneWeight, 0.0f, 1.0f);
    // Para modelos masivos (millones de triángulos), permitir hasta 12 niveles jerárquicos para alcanzar una raíz compacta
    const uint32_t effectiveMaxLevels = std::max<uint32_t>(options.maxHierarchyLevels, 12u);

    // Estructura temporal de trabajo para cada clúster construido
    struct WorkingCluster
    {
        SlipCluster           meta;
        std::vector<uint32_t> globalIndices; // Índices hacia outResult.unpackedVertices
    };

    std::vector<WorkingCluster> allClusters;
    allClusters.reserve(totalInputTriangles / 64 + 16);

    // ========================================================================
    // PASO 1: Segmentación inicial (LOD 0) con meshopt_buildMeshlets
    // ========================================================================
    for (size_t smIdx = 0; smIdx < submeshes.size(); ++smIdx) {
        const auto& sm = submeshes[smIdx];
        if (sm.vertices.empty() || sm.indices.size() < 3) continue;

        // Optimizar localidad de caché de vértices de la submalla antes de particionar en meshlets
        std::vector<uint32_t> optIndices(sm.indices.size());
        meshopt_optimizeVertexCache(optIndices.data(), sm.indices.data(), sm.indices.size(), sm.vertices.size());

        size_t maxMeshlets = meshopt_buildMeshletsBound(optIndices.size(), maxVertices, maxTriangles);
        std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertices(optIndices.size());
        std::vector<unsigned char> meshletTriangles(optIndices.size());

        size_t meshletCount = meshopt_buildMeshlets(
            meshlets.data(),
            meshletVertices.data(),
            meshletTriangles.data(),
            optIndices.data(),
            optIndices.size(),
            reinterpret_cast<const float*>(sm.vertices.data()),
            sm.vertices.size(),
            sizeof(Vertex),
            maxVertices,
            maxTriangles,
            coneWeight
        );

        meshlets.resize(meshletCount);

        // Registrar los vértices base de esta submalla en el pool global
        uint32_t submeshBaseVertex = static_cast<uint32_t>(outResult.unpackedVertices.size());
        outResult.unpackedVertices.insert(outResult.unpackedVertices.end(), sm.vertices.begin(), sm.vertices.end());
        for (const auto& v : sm.vertices) {
            outResult.quantizedVertices.push_back(QuantizeToClusterVertex(v));
        }

        std::vector<uint32_t> currentSubmeshLodClusters;
        currentSubmeshLodClusters.reserve(meshletCount);

        for (size_t m = 0; m < meshletCount; ++m) {
            const meshopt_Meshlet& ml = meshlets[m];
            if (ml.triangle_count == 0) continue;

            // Optimizar orden interno del meshlet
            meshopt_optimizeMeshlet(
                &meshletVertices[ml.vertex_offset],
                &meshletTriangles[ml.triangle_offset],
                ml.triangle_count,
                ml.vertex_count
            );

            // Calcular esfera delimitadora y cono de normales con meshoptimizer
            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertices[ml.vertex_offset],
                &meshletTriangles[ml.triangle_offset],
                ml.triangle_count,
                reinterpret_cast<const float*>(sm.vertices.data()),
                sm.vertices.size(),
                sizeof(Vertex)
            );

            WorkingCluster wc{};
            wc.meta.vertexOffset  = 0; // Índices globales ya incluyen submeshBaseVertex para compatibilidad directa con ES 3.0 y MultiDrawIndirect
            wc.meta.vertexCount   = static_cast<uint32_t>(sm.vertices.size());
            wc.meta.indexOffset   = static_cast<uint32_t>(outResult.indices.size());
            wc.meta.indexCount    = ml.triangle_count * 3;
            wc.meta.center        = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
            wc.meta.radius        = std::max(bounds.radius, 1e-4f);
            wc.meta.coneAxis      = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
            wc.meta.coneCutoff    = bounds.cone_cutoff;
            wc.meta.lodError      = 0.0f; // Nivel 0 = geometría exacta sin error de simplificación
            wc.meta.parentError   = 1e30f;
            wc.meta.parentId      = -1;
            wc.meta.firstChildId  = -1;
            wc.meta.childCount    = 0;
            wc.meta.lodLevel      = 0;
            wc.meta.materialIndex = (sm.materialIndex >= 0) ? static_cast<uint32_t>(sm.materialIndex) : 0u;
            wc.meta.submeshIndex  = static_cast<uint32_t>(smIdx);
            wc.meta.flags         = 0x02u; // Bit 1 = Hoja LOD 0

            wc.globalIndices.reserve(ml.triangle_count * 3);
            for (size_t t = 0; t < ml.triangle_count * 3; ++t) {
                uint32_t localVertIdx = meshletVertices[ml.vertex_offset + meshletTriangles[ml.triangle_offset + t]];
                uint32_t globalVertIdx = submeshBaseVertex + localVertIdx;
                wc.globalIndices.push_back(globalVertIdx);
                outResult.indices.push_back(globalVertIdx);
            }

            uint32_t clusterId = static_cast<uint32_t>(allClusters.size());
            allClusters.push_back(std::move(wc));
            currentSubmeshLodClusters.push_back(clusterId);
        }

        // ====================================================================
        // PASO 2 y 3: Jerarquía Multi-Nivel y Voxelización con Spatial Hash Grid
        // Optimizado para mallas de millones de triángulos (O(N), multihilo, sin rehashes costosos)
        // ====================================================================
        float currentCellSize = baseVoxelSize;
        uint32_t currentLodLevel = 1;

        size_t estimatedGridSlots = std::max<size_t>(
            SlipSpatialHashGrid3D::DEFAULT_TABLE_SIZE,
            std::min<size_t>(1u << 22, sm.vertices.size() / 2 + 65536)
        );
        SlipSpatialHashGrid3D spatialGrid(currentCellSize, static_cast<uint32_t>(estimatedGridSlots));

        while (currentSubmeshLodClusters.size() > 1 && currentLodLevel <= effectiveMaxLevels) {
            const size_t numActive = currentSubmeshLodClusters.size();
            const size_t targetGroupSize = std::clamp<size_t>(options.groupSize, 2, 4);

            std::vector<std::vector<uint32_t>> balancedGroups;
            balancedGroups.reserve((numActive + targetGroupSize - 1) / targetGroupSize);

            // Para niveles masivos (> 2,048 clusters, ej. modelos de 4M+ triángulos con ~35,000 clusters),
            // usar particionado espacial por curva Z-Order (Morton 3D) en O(N log N) en vez de grafos pesados
            if (numActive > 2048) {
                glm::vec3 cMin(1e30f), cMax(-1e30f);
                for (uint32_t cid : currentSubmeshLodClusters) {
                    const glm::vec3& c = allClusters[cid].meta.center;
                    cMin = glm::min(cMin, c);
                    cMax = glm::max(cMax, c);
                }
                glm::vec3 cExt = glm::max(cMax - cMin, glm::vec3(1e-4f));

                auto expandBits10 = [](uint32_t v) -> uint32_t {
                    v = (v * 0x00010001u) & 0xFF0000FFu;
                    v = (v * 0x00000101u) & 0x0F00F00Fu;
                    v = (v * 0x00000011u) & 0xC30C30C3u;
                    v = (v * 0x00000005u) & 0x49249249u;
                    return v;
                };
                auto computeMorton3D = [&](const glm::vec3& p) -> uint32_t {
                    glm::vec3 norm = glm::clamp((p - cMin) / cExt, glm::vec3(0.0f), glm::vec3(1.0f));
                    uint32_t xx = expandBits10(static_cast<uint32_t>(norm.x * 1023.0f));
                    uint32_t yy = expandBits10(static_cast<uint32_t>(norm.y * 1023.0f));
                    uint32_t zz = expandBits10(static_cast<uint32_t>(norm.z * 1023.0f));
                    return (xx * 4u) + (yy * 2u) + zz;
                };

                struct MortonCluster {
                    uint32_t morton;
                    uint32_t clusterId;
                };
                std::vector<MortonCluster> sorted(numActive);
                for (size_t i = 0; i < numActive; ++i) {
                    uint32_t cid = currentSubmeshLodClusters[i];
                    sorted[i] = { computeMorton3D(allClusters[cid].meta.center), cid };
                }
                std::sort(sorted.begin(), sorted.end(), [](const MortonCluster& a, const MortonCluster& b) {
                    return a.morton < b.morton;
                });

                for (size_t offset = 0; offset < numActive; offset += targetGroupSize) {
                    size_t end = std::min(numActive, offset + targetGroupSize);
                    std::vector<uint32_t> grp;
                    grp.reserve(end - offset);
                    for (size_t k = offset; k < end; ++k) {
                        grp.push_back(sorted[k].clusterId);
                    }
                    balancedGroups.push_back(std::move(grp));
                }
            } else {
                // Preparar datos de conectividad para agrupar clusters vecinos (2 a 4)
                std::vector<unsigned int> flatClusterIndices;
                std::vector<unsigned int> clusterIndexCounts(numActive);
                for (size_t i = 0; i < numActive; ++i) {
                    const auto& c = allClusters[currentSubmeshLodClusters[i]];
                    clusterIndexCounts[i] = static_cast<unsigned int>(c.globalIndices.size());
                    flatClusterIndices.insert(flatClusterIndices.end(), c.globalIndices.begin(), c.globalIndices.end());
                }

                std::vector<unsigned int> partitionIds(numActive, 0);
                size_t partitionCount = 1;

                if (numActive > targetGroupSize) {
                    partitionCount = meshopt_partitionClusters(
                        partitionIds.data(),
                        flatClusterIndices.data(),
                        flatClusterIndices.size(),
                        clusterIndexCounts.data(),
                        numActive,
                        reinterpret_cast<const float*>(outResult.unpackedVertices.data()),
                        outResult.unpackedVertices.size(),
                        sizeof(Vertex),
                        targetGroupSize
                    );
                }

                std::vector<std::vector<uint32_t>> groups(partitionCount);
                for (size_t i = 0; i < numActive; ++i) {
                    groups[partitionIds[i]].push_back(currentSubmeshLodClusters[i]);
                }

                std::vector<uint32_t> singletons;
                for (auto& g : groups) {
                    if (g.size() >= 2) {
                        for (size_t offset = 0; offset < g.size(); offset += 4) {
                            size_t end = std::min(g.size(), offset + 4);
                            balancedGroups.emplace_back(g.begin() + offset, g.begin() + end);
                        }
                    } else if (g.size() == 1) {
                        singletons.push_back(g[0]);
                    }
                }

                if (!singletons.empty()) {
                    std::sort(singletons.begin(), singletons.end(), [&](uint32_t a, uint32_t b) {
                        const glm::vec3& ca = allClusters[a].meta.center;
                        const glm::vec3& cb = allClusters[b].meta.center;
                        if (std::abs(ca.x - cb.x) > 1e-3f) return ca.x < cb.x;
                        if (std::abs(ca.y - cb.y) > 1e-3f) return ca.y < cb.y;
                        return ca.z < cb.z;
                    });
                    for (size_t offset = 0; offset < singletons.size(); offset += targetGroupSize) {
                        size_t end = std::min(singletons.size(), offset + targetGroupSize);
                        balancedGroups.emplace_back(singletons.begin() + offset, singletons.begin() + end);
                    }
                }
            }

            // Poblar la Spatial Hash Grid unificada para todo este nivel LOD con resolución S_L = base * 2^(L-1)
            // Esto garantiza que cualquier vértice o arista compartida entre dos grupos de clusters distintos
            // colapse hacia el MISMO centroide de celda sin producir grietas (cracks).
            spatialGrid.Clear(currentCellSize);

            // Solo insertar aristas explícitamente en mallas pequeñas; en mallas de millones de triángulos
            // la inserción directa de los vértices indexados ya acumula exactamente los mismos extremos en la celda hash
            // sin pagar 12 millones de inserciones en std::unordered_map.
            if (numActive <= 1024) {
                std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> edgeSharingMap;
                for (uint32_t cid : currentSubmeshLodClusters) {
                    const auto& c = allClusters[cid];
                    for (size_t t = 0; t + 2 < c.globalIndices.size(); t += 3) {
                        uint32_t idx[3] = { c.globalIndices[t], c.globalIndices[t + 1], c.globalIndices[t + 2] };
                        for (int e = 0; e < 3; ++e) {
                            uint32_t a = idx[e];
                            uint32_t b = idx[(e + 1) % 3];
                            EdgeKey key{ std::min(a, b), std::max(a, b) };
                            edgeSharingMap[key]++;
                        }
                    }
                }
                for (const auto& kv : edgeSharingMap) {
                    if (kv.second > 1) {
                        spatialGrid.InsertSharedEdge(
                            outResult.unpackedVertices[kv.first.v0],
                            outResult.unpackedVertices[kv.first.v1]
                        );
                    }
                }
            }

            for (uint32_t cid : currentSubmeshLodClusters) {
                const auto& c = allClusters[cid];
                for (uint32_t vIdx : c.globalIndices) {
                    spatialGrid.InsertVertex(outResult.unpackedVertices[vIdx]);
                }
            }
            spatialGrid.FinalizeCentroids();

            // Vector plano O(1) para mapear slot de celda hash -> índice de vértice canónico en el buffer unificado
            std::vector<uint32_t> cellSlotToGlobalVertex(spatialGrid.GetTableSize(), 0xFFFFFFFFu);

            // Fase A: Colapsar los triángulos de cada grupo a las celdas de la Spatial Hash Grid
            struct PreparedGroup {
                std::vector<uint32_t> collapsedTriangles;
                std::vector<Vertex>   localVertices;
                std::vector<uint32_t> localToGlobalVert;
                std::vector<uint32_t> localIndices;
                float                 maxChildError = 0.0f;
                float                 maxDisplacementError = 0.0f;
                meshopt_Bounds        parentBounds{};
            };

            const size_t numGroups = balancedGroups.size();
            std::vector<PreparedGroup> preparedGroups(numGroups);

            for (size_t gIdx = 0; gIdx < numGroups; ++gIdx) {
                const auto& group = balancedGroups[gIdx];
                if (group.empty()) continue;

                PreparedGroup& pg = preparedGroups[gIdx];
                pg.collapsedTriangles.reserve(group.size() * maxTriangles * 3);

                for (uint32_t childId : group) {
                    const auto& child = allClusters[childId];
                    pg.maxChildError = std::max(pg.maxChildError, child.meta.lodError);

                    for (size_t t = 0; t + 2 < child.globalIndices.size(); t += 3) {
                        uint32_t idx0 = child.globalIndices[t];
                        uint32_t idx1 = child.globalIndices[t + 1];
                        uint32_t idx2 = child.globalIndices[t + 2];
                        if (idx0 != idx1 && idx1 != idx2 && idx2 != idx0) {
                            pg.collapsedTriangles.push_back(idx0);
                            pg.collapsedTriangles.push_back(idx1);
                            pg.collapsedTriangles.push_back(idx2);
                        }
                    }
                }

                if (pg.collapsedTriangles.empty()) {
                    const auto& firstChild = allClusters[group[0]];
                    if (firstChild.globalIndices.size() >= 3) {
                        pg.collapsedTriangles.insert(
                            pg.collapsedTriangles.end(),
                            firstChild.globalIndices.begin(),
                            firstChild.globalIndices.begin() + 3
                        );
                    }
                }

                // Extraer sub-buffer local compacto (~200 vértices en vez de 4,000,000) para que
                // meshopt_simplify y meshopt_computeClusterBounds sean O(V_local) y puedan correr en paralelo
                pg.localVertices.reserve(pg.collapsedTriangles.size());
                pg.localToGlobalVert.reserve(pg.collapsedTriangles.size());
                pg.localIndices.resize(pg.collapsedTriangles.size());

                for (size_t i = 0; i < pg.collapsedTriangles.size(); ++i) {
                    uint32_t gVert = pg.collapsedTriangles[i];
                    int32_t foundLocal = -1;
                    for (size_t lv = 0; lv < pg.localToGlobalVert.size(); ++lv) {
                        if (pg.localToGlobalVert[lv] == gVert) {
                            foundLocal = static_cast<int32_t>(lv);
                            break;
                        }
                    }
                    if (foundLocal < 0) {
                        foundLocal = static_cast<int32_t>(pg.localVertices.size());
                        pg.localToGlobalVert.push_back(gVert);
                        pg.localVertices.push_back(outResult.unpackedVertices[gVert]);
                    }
                    pg.localIndices[i] = static_cast<uint32_t>(foundLocal);
                }
            }

            // Fase B: Simplificar y calcular bounds en paralelo (Multithreading por bloques) en O(V_local)
            auto processGroupRange = [&](size_t startIdx, size_t endIdx) {
                for (size_t gIdx = startIdx; gIdx < endIdx; ++gIdx) {
                    PreparedGroup& pg = preparedGroups[gIdx];
                    if (pg.localIndices.empty() || pg.localVertices.empty()) continue;

                    size_t targetTriangles = maxTriangles;
                    if (currentLodLevel == 1) targetTriangles = (maxTriangles * 3) / 4;
                    else if (currentLodLevel == 2) targetTriangles = maxTriangles / 2;
                    else if (currentLodLevel == 3) targetTriangles = maxTriangles / 4;
                    else if (currentLodLevel == 4) targetTriangles = maxTriangles / 8;
                    else if (currentLodLevel >= 5) targetTriangles = (std::max<size_t>)(6, maxTriangles / 16);

                    size_t targetIdxCount = targetTriangles * 3;

                    if (pg.localIndices.size() / 3 > targetTriangles) {
                        std::vector<unsigned int> simplified(pg.localIndices.size());
                        float simpError = 0.0f;
                        // 1. Simplificación Quadric Error Metric (QEM) preservando detalles geométricos y UVs
                        size_t simpCount = meshopt_simplify(
                            simplified.data(),
                            pg.localIndices.data(),
                            pg.localIndices.size(),
                            reinterpret_cast<const float*>(pg.localVertices.data()),
                            pg.localVertices.size(),
                            sizeof(Vertex),
                            targetIdxCount,
                            0.20f,
                            0, // Sin LockBorder para permitir decimar subpiezas abiertas
                            &simpError
                        );
                        // 2. Si la simplificación normal no alcanzó el presupuesto por bordes o costuras no-manifold,
                        // invocar meshopt_simplifySloppy para garantizar el presupuesto estricto hacia quads/proxy
                        if (simpCount > targetIdxCount || simpCount < 3) {
                            float sloppyError = 0.0f;
                            size_t sloppyCount = meshopt_simplifySloppy(
                                simplified.data(),
                                pg.localIndices.data(),
                                pg.localIndices.size(),
                                reinterpret_cast<const float*>(pg.localVertices.data()),
                                pg.localVertices.size(),
                                sizeof(Vertex),
                                targetIdxCount,
                                0.35f,
                                &sloppyError
                            );
                            if (sloppyCount >= 3) {
                                simpCount = sloppyCount;
                                simpError = std::max(simpError, sloppyError);
                            }
                        }
                        if (simpCount >= 3) {
                            simplified.resize(simpCount);
                            pg.localIndices.swap(simplified);
                            float scale = meshopt_simplifyScale(
                                reinterpret_cast<const float*>(pg.localVertices.data()),
                                pg.localVertices.size(),
                                sizeof(Vertex)
                            );
                            pg.maxDisplacementError += (simpError * scale);
                        }
                    }

                    size_t safeIndexCount = (std::min)(pg.localIndices.size(), static_cast<size_t>(256 * 3));
                    pg.parentBounds = meshopt_computeClusterBounds(
                        pg.localIndices.data(),
                        safeIndexCount,
                        reinterpret_cast<const float*>(pg.localVertices.data()),
                        pg.localVertices.size(),
                        sizeof(Vertex)
                    );

                    // Traducir de vuelta los índices locales simplificados a índices globales
                    pg.collapsedTriangles.resize(pg.localIndices.size());
                    for (size_t i = 0; i < pg.localIndices.size(); ++i) {
                        pg.collapsedTriangles[i] = pg.localToGlobalVert[pg.localIndices[i]];
                    }
                }
            };

            unsigned int hwThreads = std::max(1u, std::thread::hardware_concurrency());
            if (numGroups >= 64 && hwThreads > 1) {
                size_t chunk = (numGroups + hwThreads - 1) / hwThreads;
                std::vector<std::future<void>> futures;
                futures.reserve(hwThreads);
                for (unsigned int t = 0; t < hwThreads; ++t) {
                    size_t sIdx = t * chunk;
                    if (sIdx >= numGroups) break;
                    size_t eIdx = std::min(numGroups, sIdx + chunk);
                    futures.push_back(std::async(std::launch::async, processGroupRange, sIdx, eIdx));
                }
                for (auto& f : futures) f.get();
            } else {
                processGroupRange(0, numGroups);
            }

            // Fase C: Ensamblar los clusters padres en serie
            std::vector<uint32_t> nextLevelClusters;
            nextLevelClusters.reserve(numGroups);

            for (size_t gIdx = 0; gIdx < numGroups; ++gIdx) {
                const auto& group = balancedGroups[gIdx];
                if (group.empty()) continue;
                PreparedGroup& pg = preparedGroups[gIdx];

                float voxelErrorBound = std::max(pg.maxDisplacementError, currentCellSize * 0.8660254f);
                float parentLodError = pg.maxChildError + voxelErrorBound;

                glm::vec3 pCenter(pg.parentBounds.center[0], pg.parentBounds.center[1], pg.parentBounds.center[2]);
                float pRadius = std::max(pg.parentBounds.radius, 1e-4f);
                for (uint32_t childId : group) {
                    const auto& ch = allClusters[childId].meta;
                    float distToChild = glm::distance(pCenter, ch.center) + ch.radius;
                    pRadius = std::max(pRadius, distToChild);
                }

                WorkingCluster parentWc{};
                parentWc.meta.vertexOffset  = 0;
                parentWc.meta.vertexCount   = static_cast<uint32_t>(outResult.unpackedVertices.size());
                parentWc.meta.indexOffset   = static_cast<uint32_t>(outResult.indices.size());
                parentWc.meta.indexCount    = static_cast<uint32_t>(pg.collapsedTriangles.size());
                parentWc.meta.center        = pCenter;
                parentWc.meta.radius        = pRadius;
                parentWc.meta.coneAxis      = glm::vec3(pg.parentBounds.cone_axis[0], pg.parentBounds.cone_axis[1], pg.parentBounds.cone_axis[2]);
                parentWc.meta.coneCutoff    = pg.parentBounds.cone_cutoff;
                parentWc.meta.lodError      = parentLodError;
                parentWc.meta.parentError   = 1e30f;
                parentWc.meta.parentId      = -1;
                parentWc.meta.firstChildId  = static_cast<int32_t>(group[0]);
                parentWc.meta.childCount    = static_cast<uint16_t>(group.size());
                parentWc.meta.lodLevel      = static_cast<uint16_t>(currentLodLevel);
                parentWc.meta.materialIndex = (sm.materialIndex >= 0) ? static_cast<uint32_t>(sm.materialIndex) : 0u;
                parentWc.meta.submeshIndex  = static_cast<uint32_t>(smIdx);
                parentWc.meta.flags         = 0u;

                parentWc.globalIndices = std::move(pg.collapsedTriangles);
                outResult.indices.insert(outResult.indices.end(), parentWc.globalIndices.begin(), parentWc.globalIndices.end());

                uint32_t parentClusterId = static_cast<uint32_t>(allClusters.size());
                allClusters.push_back(std::move(parentWc));

                for (uint32_t childId : group) {
                    allClusters[childId].meta.parentId    = static_cast<int32_t>(parentClusterId);
                    allClusters[childId].meta.parentError = parentLodError;
                }

                nextLevelClusters.push_back(parentClusterId);
            }

            if (nextLevelClusters.size() == currentSubmeshLodClusters.size()) {
                break; // No fue posible reducir más niveles
            }

            currentSubmeshLodClusters = std::move(nextLevelClusters);
            currentCellSize *= 2.0f; // Duplicar el paso de rejilla (2 * S) para el siguiente nivel
            currentLodLevel++;
        }

        outResult.header.maxLodLevel = std::max(outResult.header.maxLodLevel, currentLodLevel - 1);
    }

    if (allClusters.empty()) return false;

    // Reordenar el árbol continuo de forma que:
    // 1. Los hijos de cada nodo padre queden almacenados en un bloque contiguo [firstChildId .. firstChildId + childCount - 1].
    // 2. Los nodos raíz (parentId == -1) queden agrupados al principio o en un rango contiguo conocido.
    std::vector<uint32_t> roots;
    std::unordered_map<int32_t, std::vector<uint32_t>> childrenMap;
    for (uint32_t i = 0; i < allClusters.size(); ++i) {
        int32_t pid = allClusters[i].meta.parentId;
        if (pid < 0) {
            allClusters[i].meta.flags |= 0x01u; // Bit 0 = Raíz
            roots.push_back(i);
        } else {
            childrenMap[pid].push_back(i);
        }
    }

    std::vector<SlipCluster> orderedClusters;
    orderedClusters.reserve(allClusters.size());
    std::vector<int32_t> oldToNewId(allClusters.size(), -1);

    // Colocar todas las raíces al inicio del arreglo continuo [0 .. rootClusterCount - 1]
    for (uint32_t r : roots) {
        oldToNewId[r] = static_cast<int32_t>(orderedClusters.size());
        orderedClusters.push_back(allClusters[r].meta);
    }

    // Recorrido BFS para garantizar que los hijos de cualquier nodo queden estrictamente contiguos
    size_t cursor = 0;
    std::vector<uint32_t> newToOldId(roots.begin(), roots.end());
    while (cursor < orderedClusters.size()) {
        uint32_t oldIdx = newToOldId[cursor];
        auto it = childrenMap.find(static_cast<int32_t>(oldIdx));
        if (it != childrenMap.end() && !it->second.empty()) {
            const auto& kids = it->second;
            int32_t firstChildNewIdx = static_cast<int32_t>(orderedClusters.size());
            orderedClusters[cursor].firstChildId = firstChildNewIdx;
            orderedClusters[cursor].childCount   = static_cast<uint16_t>(kids.size());

            for (uint32_t kOld : kids) {
                int32_t kNew = static_cast<int32_t>(orderedClusters.size());
                oldToNewId[kOld] = kNew;
                SlipCluster kMeta = allClusters[kOld].meta;
                kMeta.parentId = static_cast<int32_t>(cursor);
                orderedClusters.push_back(kMeta);
                newToOldId.push_back(kOld);
            }
        } else {
            orderedClusters[cursor].firstChildId = -1;
            orderedClusters[cursor].childCount   = 0;
        }
        cursor++;
    }

    outResult.clusters = std::move(orderedClusters);
    outResult.header.totalClusters     = static_cast<uint32_t>(outResult.clusters.size());
    outResult.header.rootClusterOffset = 0;
    outResult.header.rootClusterCount  = static_cast<uint32_t>(roots.size());
    outResult.header.totalVertices     = static_cast<uint32_t>(outResult.quantizedVertices.size());
    outResult.header.totalIndices      = static_cast<uint32_t>(outResult.indices.size());

    return true;
}

void SlipClusterBuilder::SerializeClusterData(
    const SlipClusterBuildResult& buildResult,
    std::vector<uint8_t>& outBuffer)
{
    auto appendBytes = [&outBuffer](const void* src, size_t size) {
        if (size == 0 || !src) return;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(src);
        outBuffer.insert(outBuffer.end(), p, p + size);
    };

    appendBytes(&buildResult.header, sizeof(SlipClusterHierarchyHeader));
    appendBytes(buildResult.clusters.data(), buildResult.clusters.size() * sizeof(SlipCluster));
    appendBytes(buildResult.quantizedVertices.data(), buildResult.quantizedVertices.size() * sizeof(SlipClusterVertex));
    appendBytes(buildResult.indices.data(), buildResult.indices.size() * sizeof(uint32_t));
}

bool SlipClusterBuilder::DeserializeClusterData(
    const uint8_t* data,
    size_t dataSize,
    SlipClusterBuildResult& outResult)
{
    if (!data || dataSize < sizeof(SlipClusterHierarchyHeader)) return false;

    size_t cursor = 0;
    auto readBytes = [&](void* dst, size_t sz) -> bool {
        if (cursor + sz > dataSize) return false;
        std::memcpy(dst, data + cursor, sz);
        cursor += sz;
        return true;
    };

    if (!readBytes(&outResult.header, sizeof(SlipClusterHierarchyHeader))) return false;
    if (std::memcmp(outResult.header.magic, "VCLUST01", 8) != 0) return false;

    outResult.clusters.resize(outResult.header.totalClusters);
    if (outResult.header.totalClusters > 0) {
        if (!readBytes(outResult.clusters.data(), outResult.header.totalClusters * sizeof(SlipCluster))) return false;
    }

    outResult.quantizedVertices.resize(outResult.header.totalVertices);
    if (outResult.header.totalVertices > 0) {
        if (!readBytes(outResult.quantizedVertices.data(), outResult.header.totalVertices * sizeof(SlipClusterVertex))) return false;
    }

    outResult.indices.resize(outResult.header.totalIndices);
    if (outResult.header.totalIndices > 0) {
        if (!readBytes(outResult.indices.data(), outResult.header.totalIndices * sizeof(uint32_t))) return false;
    }

    return true;
}

bool SlipClusterBuilder::CompileGamePak(
    const std::string& projectRootDir,
    const std::string& outputPakPath)
{
    if (projectRootDir.empty() || outputPakPath.empty()) return false;

    std::string cacheDir = SlipFileSystem::CombinePath(projectRootDir, "cache");
    std::string assetsDir = SlipFileSystem::CombinePath(projectRootDir, "assets");

    std::vector<FileInfo> filesToPack;
    if (SlipFileSystem::Exists(cacheDir)) {
        auto cachedFiles = SlipFileSystem::ListDirectoryRecursive(cacheDir, ".model_data");
        filesToPack.insert(filesToPack.end(), cachedFiles.begin(), cachedFiles.end());
    }
    if (SlipFileSystem::Exists(assetsDir)) {
        auto assetFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".mat,.png,.jpg,.tga,.scene,.lua");
        filesToPack.insert(filesToPack.end(), assetFiles.begin(), assetFiles.end());
    }

    SlipPakHeader pakHeader{};
    pakHeader.version = 1;
    pakHeader.entryCount = static_cast<uint32_t>(filesToPack.size());

    std::vector<SlipPakEntry> directory(pakHeader.entryCount);
    std::vector<std::vector<uint8_t>> payloads(pakHeader.entryCount);

    uint64_t currentOffset = sizeof(SlipPakHeader) + pakHeader.entryCount * sizeof(SlipPakEntry);

    for (size_t i = 0; i < filesToPack.size(); ++i) {
        const auto& fi = filesToPack[i];
        SlipPakEntry& entry = directory[i];

        std::string relPath = fi.fullPath;
        std::string normRoot = SlipFileSystem::NormalizePath(projectRootDir);
        if (!normRoot.empty() && normRoot.back() != '/') normRoot.push_back('/');
        if (relPath.rfind(normRoot, 0) == 0) {
            relPath = relPath.substr(normRoot.size());
        }

        std::strncpy(entry.assetPath, relPath.c_str(), sizeof(entry.assetPath) - 1);
        std::string guid = SlipFileSystem::GetGuidForAsset(fi.fullPath);
        std::strncpy(entry.guid, guid.c_str(), sizeof(entry.guid) - 1);

        SlipFileSystem::ReadAllBytes(fi.fullPath, payloads[i]);
        entry.dataOffset = currentOffset;
        entry.dataSize   = static_cast<uint64_t>(payloads[i].size());

        if (fi.extension == ".model_data") entry.assetType = 1;
        else if (fi.extension == ".mat") entry.assetType = 2;
        else if (fi.extension == ".png" || fi.extension == ".jpg" || fi.extension == ".tga") entry.assetType = 3;
        else entry.assetType = 0;

        currentOffset += entry.dataSize;
    }

    std::vector<uint8_t> pakBytes;
    pakBytes.reserve(static_cast<size_t>(currentOffset));

    auto writeRaw = [&pakBytes](const void* ptr, size_t sz) {
        if (sz == 0 || !ptr) return;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(ptr);
        pakBytes.insert(pakBytes.end(), b, b + sz);
    };

    writeRaw(&pakHeader, sizeof(SlipPakHeader));
    writeRaw(directory.data(), directory.size() * sizeof(SlipPakEntry));
    for (const auto& payload : payloads) {
        writeRaw(payload.data(), payload.size());
    }

    bool ok = SlipFileSystem::WriteAllBytes(outputPakPath, pakBytes.data(), pakBytes.size());
    if (ok) {
        SlipConsole::LogSuccess("V-Clust: Paquete game.pak compilado secuencialmente (" +
                                std::to_string(pakHeader.entryCount) + " entradas): " + outputPakPath);
    }
    return ok;
}
