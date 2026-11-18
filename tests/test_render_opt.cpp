#include <iostream>
#include <vector>
#include <cassert>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "SlipEngine/scene/SlipFrustum.h"
#include "SlipEngine/scene/SlipBVH.h"
#include "SlipEngine/render/SlipRenderQueue.h"
#include "SlipEngine/render/SlipCluster.h"
#include "SlipEngine/render/SlipRender.h"
#include "SlipEngine/editor/SlipClusterBuilder.h"

void TestFrustumCulling()
{
    std::cout << "[TEST] 1. Probando Frustum Culling..." << std::endl;

    glm::vec3 eye(0.0f, 0.0f, 5.0f);
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, target, up);
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    SlipFrustum frustum(proj * view);

    // Caja claramente frente a la cámara
    glm::vec3 frontMin(-1.0f, -1.0f, -1.0f);
    glm::vec3 frontMax(1.0f, 1.0f, 1.0f);
    assert(frustum.IntersectsAABB(frontMin, frontMax) == true);

    // Caja situada DETRÁS de la cámara (z = 10, cámara en z = 5 mirando a z = 0)
    glm::vec3 behindMin(-1.0f, -1.0f, 10.0f);
    glm::vec3 behindMax(1.0f, 1.0f, 12.0f);
    assert(frustum.IntersectsAABB(behindMin, behindMax) == false);

    // Caja situada muy lejos a la derecha (fuera del cono de visión)
    glm::vec3 farRightMin(50.0f, -1.0f, 0.0f);
    glm::vec3 farRightMax(52.0f, 1.0f, 2.0f);
    assert(frustum.IntersectsAABB(farRightMin, farRightMax) == false);

    std::cout << "  -> Frustum Culling validado con éxito." << std::endl;
}

void TestBVHHierarchy()
{
    std::cout << "[TEST] 2. Probando construcción y travesía de BVH..." << std::endl;

    SlipBVH bvh;
    std::vector<BVHItem> items;

    // Generar 200 objetos a lo largo de un pasillo (eje Z desde -100 hasta +100)
    for (int i = -100; i < 100; ++i) {
        BVHItem item;
        item.isStatic = true;
        float z = static_cast<float>(i * 2);
        item.worldAabbMin = glm::vec3(-1.0f, 0.0f, z);
        item.worldAabbMax = glm::vec3(1.0f, 2.0f, z + 1.0f);
        items.push_back(item);
    }

    bvh.Build(items, 4);
    assert(!bvh.IsEmpty());
    assert(bvh.GetNodes().size() > 1);

    // Cámara en Z = 0 mirando hacia Z = -100
    glm::vec3 eye(0.0f, 1.0f, 0.0f);
    glm::vec3 target(0.0f, 1.0f, -10.0f);
    glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 50.0f);
    SlipFrustum frustum(proj * view);

    std::vector<BVHItem> visibleItems;
    bvh.QueryFrustum(frustum, visibleItems);

    std::cout << "  -> Total items en BVH: " << items.size()
              << " | Nodos generados: " << bvh.GetNodes().size()
              << " | Items visibles en Frustum: " << visibleItems.size() << std::endl;

    assert(visibleItems.size() > 0);
    assert(visibleItems.size() < items.size()); // Descarte masivo verificado

    // Verificar que NINGÚN item visible esté detrás de la cámara (Z > 0)
    for (const auto& vis : visibleItems) {
        assert(vis.worldAabbMax.z <= 1.0f);
    }

    std::cout << "  -> BVH poda y descarta geometría trasera correctamente." << std::endl;
}

void TestRenderQueueSorting()
{
    std::cout << "[TEST] 3. Probando Material Sorting y Render Queue..." << std::endl;

    SlipRenderQueue queue;

    // Comandos con distintos shaders, materiales y profundidades
    RenderCommand cmd1; // Opaque, Shader 2, Mat 10, dist 5.0
    cmd1.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 2, 10, 1, 5.0f, true);

    RenderCommand cmd2; // Opaque, Shader 1, Mat 5, dist 2.0
    cmd2.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 1, 5, 1, 2.0f, true);

    RenderCommand cmd3; // Opaque, Shader 2, Mat 10, dist 2.0
    cmd3.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 2, 10, 1, 2.0f, true);

    RenderCommand cmd4; // Opaque, Shader 1, Mat 5, dist 8.0
    cmd4.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, 1, 5, 1, 8.0f, true);

    queue.Submit(cmd1);
    queue.Submit(cmd2);
    queue.Submit(cmd3);
    queue.Submit(cmd4);

    queue.Sort();

    const auto& sorted = queue.GetCommands();
    assert(sorted.size() == 4);

    // Debe ordenar primero por Shader 1 (cmd2, cmd4) y luego por Shader 2 (cmd3, cmd1)
    // Dentro de cada material, front-to-back por distancia (menor depth primero)
    assert(sorted[0].sortKey == cmd2.sortKey);
    assert(sorted[1].sortKey == cmd4.sortKey);
    assert(sorted[2].sortKey == cmd3.sortKey);
    assert(sorted[3].sortKey == cmd1.sortKey);

    std::cout << "  -> Material Sorting validado: Shader 1 procesado en lote antes de Shader 2 con front-to-back sorting." << std::endl;
}

void TestVClustVertexCompressionAndHashGrid()
{
    std::cout << "[TEST] 4. Probando estructuras binarias V-Clust, cuantizacion y Spatial Hash Grid 3D..." << std::endl;

    // 1. Verificar tamaños y alineación estricta
    assert(sizeof(SlipClusterVertex) == 24);
    assert(sizeof(SlipCluster) == 80);
    assert(alignof(SlipCluster) == 16);
    assert(sizeof(DrawElementsIndirectCommand) == 20);

    // 2. Probar cuantización GL_HALF_FLOAT (UVs) y GL_INT_2_10_10_10_REV (Normales)
    glm::vec3 origNormal = glm::normalize(glm::vec3(0.57735f, -0.57735f, 0.57735f));
    uint32_t packedN = SlipClusterVertex::PackInt2_10_10_10_Rev(origNormal, 1.0f);
    glm::vec4 unpackedN4 = SlipClusterVertex::UnpackInt2_10_10_10_Rev(packedN);
    glm::vec3 unpackedN(unpackedN4.x, unpackedN4.y, unpackedN4.z);
    assert(glm::length(origNormal - unpackedN) < 0.01f);

    glm::vec2 origUV(0.75f, -1.25f);
    uint16_t halfUV[2] = { 0, 0 };
    SlipClusterVertex::PackHalfUV(origUV, halfUV);
    glm::vec2 unpackedUV = SlipClusterVertex::UnpackHalfUV(halfUV);
    assert(glm::length(origUV - unpackedUV) < 1e-3f);

    // 3. Probar Spatial Hash Grid 3D y prevención de grietas (cracks) en bordes compartidos
    SlipSpatialHashGrid3D grid(0.1f, 4096);
    Vertex sharedBorderVertA{};
    sharedBorderVertA.Position  = glm::vec3(1.234f, -0.567f, 3.891f);
    sharedBorderVertA.Normal    = glm::vec3(0.0f, 1.0f, 0.0f);
    sharedBorderVertA.TexCoords = glm::vec2(0.5f, 0.5f);
    sharedBorderVertA.Tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
    sharedBorderVertA.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

    Vertex sharedBorderVertB = sharedBorderVertA;

    // Dos clusters adyacentes insertan el mismo vértice compartido de borde de forma independiente
    uint32_t slotFromClusterA = grid.InsertVertex(sharedBorderVertA);
    uint32_t slotFromClusterB = grid.InsertVertex(sharedBorderVertB);
    grid.FinalizeCentroids();

    uint32_t resolvedSlotA = 0, resolvedSlotB = 0;
    Vertex collapsedA = grid.GetCollapsedVertex(sharedBorderVertA, &resolvedSlotA);
    Vertex collapsedB = grid.GetCollapsedVertex(sharedBorderVertB, &resolvedSlotB);
    assert(slotFromClusterA == slotFromClusterB);
    assert(resolvedSlotA == resolvedSlotB);
    assert(collapsedA.Position == collapsedB.Position);

    std::cout << "  -> Empaquetamiento (24B vertex / 80B cluster) y Spatial Hash Grid sin grietas validados." << std::endl;
}

void TestVClustHierarchyAndRuntimeSSE()
{
    std::cout << "[TEST] 5. Probando baking jerarquico V-Clust, SSE, Frustum y Backface Cone Culling..." << std::endl;

    // Generar una malla densa (rejilla 32x32 = 2048 triángulos) mirando hacia +Z
    const int gridN = 32;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve((gridN + 1) * (gridN + 1));

    for (int y = 0; y <= gridN; ++y) {
        for (int x = 0; x <= gridN; ++x) {
            Vertex vert{};
            vert.Position  = glm::vec3((float(x) / gridN - 0.5f) * 10.0f, (float(y) / gridN - 0.5f) * 10.0f, 0.0f);
            vert.Normal    = glm::vec3(0.0f, 0.0f, 1.0f);
            vert.TexCoords = glm::vec2(float(x) / gridN, float(y) / gridN);
            vert.Tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
            vert.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
            vertices.push_back(vert);
        }
    }

    for (int y = 0; y < gridN; ++y) {
        for (int x = 0; x < gridN; ++x) {
            uint32_t i0 = y * (gridN + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = (y + 1) * (gridN + 1) + x;
            uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
    }

    SlipClusterBuildOptions opts;
    opts.maxVerticesPerCluster  = 64;
    opts.maxTrianglesPerCluster = 64;
    opts.baseVoxelSize          = 0.6f;
    opts.groupSize              = 4;

    SlipClusterBuildResult buildRes;
    bool built = SlipClusterBuilder::BuildFromUnifiedMesh(vertices, indices, 0, 0, opts, buildRes);
    assert(built);
    assert(!buildRes.clusters.empty());
    assert(buildRes.header.maxLodLevel >= 1);
    assert(buildRes.header.rootClusterCount >= 1);

    std::cout << "  -> Jerarquia V-Clust construida: " << buildRes.clusters.size()
              << " clusters totales hasta LOD " << buildRes.header.maxLodLevel
              << " (Raices: " << buildRes.header.rootClusterCount << ")." << std::endl;

    // Verificar serialización/deserialización binaria VCLUST01
    std::vector<uint8_t> blob;
    SlipClusterBuilder::SerializeClusterData(buildRes, blob);
    assert(!blob.empty());

    SlipClusterBuildResult loadedRes{};
    bool deserialized = SlipClusterBuilder::DeserializeClusterData(
        blob.data(), blob.size(), loadedRes);
    assert(deserialized);
    assert(loadedRes.clusters.size() == buildRes.clusters.size());
    assert(loadedRes.quantizedVertices.size() == buildRes.quantizedVertices.size());

    const auto& loadedHdr = loadedRes.header;
    const auto& loadedClusters = loadedRes.clusters;

    // Verificar fórmula SSE: ErrorPixeles = (lodError * ViewportHeight) / (2.0f * Distancia * tan(FOV * 0.5f))
    float fovRad = glm::radians(60.0f);
    float sseNear = SlipRender::ComputeScreenSpaceError(0.05f, 2.0f, 1080.0f, fovRad);
    float sseFar  = SlipRender::ComputeScreenSpaceError(0.05f, 200.0f, 1080.0f, fovRad);
    assert(sseNear > 1.0f); // De cerca el error proyectado supera 1 píxel -> desciende a hijos
    assert(sseFar <= 1.0f); // De lejos el error proyectado es <= 1 píxel -> selecciona el padre

    // Traversal a corta distancia (selecciona niveles detallados) vs larga distancia (selecciona padres simplificados)
    glm::mat4 identity(1.0f);
    glm::vec3 nearCamPos(0.0f, 0.0f, 6.0f);
    glm::mat4 nearView = glm::lookAt(nearCamPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(fovRad, 16.0f / 9.0f, 0.1f, 1000.0f);
    SlipFrustum nearFrustum(proj * nearView);

    std::vector<SlipVisibleCluster> nearVisible;
    SlipVClustStats nearStats{};
    SlipRender::TraverseClusterHierarchy(
        loadedClusters, loadedHdr.rootClusterCount, identity, nearFrustum,
        nearCamPos, 1080.0f, fovRad, nearVisible, &nearStats, 1.0f, true);
    assert(!nearVisible.empty());

    glm::vec3 farCamPos(0.0f, 0.0f, 6000.0f);
    glm::mat4 farProj = glm::perspective(fovRad, 16.0f / 9.0f, 0.1f, 20000.0f);
    glm::mat4 farView = glm::lookAt(farCamPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    SlipFrustum farFrustum(farProj * farView);

    std::vector<SlipVisibleCluster> farVisible;
    SlipVClustStats farStats{};
    SlipRender::TraverseClusterHierarchy(
        loadedClusters, loadedHdr.rootClusterCount, identity, farFrustum,
        farCamPos, 1080.0f, fovRad, farVisible, &farStats, 1.0f, true);
    assert(!farVisible.empty());
    assert(farStats.trianglesSubmitted < nearStats.trianglesSubmitted);

    // Probar Backface Cone Culling colocando la cámara detrás del plano (Z = -10 mirando hacia Z = 0, cuyas normales apuntan a +Z)
    glm::vec3 backCamPos(0.0f, 0.0f, -15.0f);
    glm::mat4 backView = glm::lookAt(backCamPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    SlipFrustum backFrustum(proj * backView);

    std::vector<SlipVisibleCluster> backVisible;
    SlipVClustStats backStats{};
    SlipRender::TraverseClusterHierarchy(
        loadedClusters, loadedHdr.rootClusterCount, identity, backFrustum,
        backCamPos, 1080.0f, fovRad, backVisible, &backStats, 1.0f, true);
    assert(backStats.backfaceCulledClusters > 0);

    std::cout << "  -> Runtime SSE (Cerca: " << nearStats.trianglesSubmitted << " tris | Lejos: "
              << farStats.trianglesSubmitted << " tris) y Backface Cone Culling ("
              << backStats.backfaceCulledClusters << " clusters descartados) validados con exito." << std::endl;
}

int main(int argc, char* argv[])
{
    std::cout << "=================================================" << std::endl;
    std::cout << "   SlipEngine: Render Optimization Test Suite    " << std::endl;
    std::cout << "=================================================" << std::endl;

    TestFrustumCulling();
    TestBVHHierarchy();
    TestRenderQueueSorting();
    TestVClustVertexCompressionAndHashGrid();
    TestVClustHierarchyAndRuntimeSSE();

    std::cout << "=================================================" << std::endl;
    std::cout << "   ¡TODAS LAS PRUEBAS UNITARIAS COMPLETADAS!    " << std::endl;
    std::cout << "=================================================" << std::endl;

    return 0;
}

