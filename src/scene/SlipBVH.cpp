#include "SlipBVH.h"
#include "SlipEntity.h"
#include "SlipComponent.h"
#include "SlipMesh.h"
#include "SlipTerrainComponent.h"
#include "SlipPrimitive.h"
#include <algorithm>

void SlipBVH::Clear()
{
    m_Nodes.clear();
    m_Items.clear();
}

void SlipBVH::Build(std::vector<BVHItem> items, int maxLeafItems)
{
    Clear();
    if (items.empty()) return;

    m_Items = std::move(items);

    BVHNode root;
    root.itemStartIndex = 0;
    root.itemCount = static_cast<int>(m_Items.size());
    root.aabbMin = glm::vec3(1e30f);
    root.aabbMax = glm::vec3(-1e30f);

    for (const auto& item : m_Items) {
        root.aabbMin = glm::min(root.aabbMin, item.worldAabbMin);
        root.aabbMax = glm::max(root.aabbMax, item.worldAabbMax);
    }

    m_Nodes.push_back(root);
    Subdivide(0, 0, maxLeafItems);
}

int SlipBVH::Subdivide(int nodeIndex, int depth, int maxLeafItems)
{
    if (m_Nodes[nodeIndex].itemCount <= maxLeafItems || depth >= 24) {
        return nodeIndex;
    }

    const BVHNode& current = m_Nodes[nodeIndex];
    glm::vec3 extent = current.aabbMax - current.aabbMin;

    int splitAxis = 0;
    if (extent.y > extent.x && extent.y > extent.z) splitAxis = 1;
    else if (extent.z > extent.x && extent.z > extent.y) splitAxis = 2;

    int start = current.itemStartIndex;
    int count = current.itemCount;

    auto comp = [splitAxis](const BVHItem& a, const BVHItem& b) {
        return a.GetCenter()[splitAxis] < b.GetCenter()[splitAxis];
    };

    std::sort(m_Items.begin() + start, m_Items.begin() + start + count, comp);

    int mid = count / 2;
    if (mid <= 0 || mid >= count) {
        return nodeIndex;
    }

    BVHNode leftNode;
    leftNode.itemStartIndex = start;
    leftNode.itemCount = mid;
    leftNode.aabbMin = glm::vec3(1e30f);
    leftNode.aabbMax = glm::vec3(-1e30f);
    for (int i = start; i < start + mid; ++i) {
        leftNode.aabbMin = glm::min(leftNode.aabbMin, m_Items[i].worldAabbMin);
        leftNode.aabbMax = glm::max(leftNode.aabbMax, m_Items[i].worldAabbMax);
    }

    BVHNode rightNode;
    rightNode.itemStartIndex = start + mid;
    rightNode.itemCount = count - mid;
    rightNode.aabbMin = glm::vec3(1e30f);
    rightNode.aabbMax = glm::vec3(-1e30f);
    for (int i = start + mid; i < start + count; ++i) {
        rightNode.aabbMin = glm::min(rightNode.aabbMin, m_Items[i].worldAabbMin);
        rightNode.aabbMax = glm::max(rightNode.aabbMax, m_Items[i].worldAabbMax);
    }

    int leftIdx = static_cast<int>(m_Nodes.size());
    m_Nodes.push_back(leftNode);
    int rightIdx = static_cast<int>(m_Nodes.size());
    m_Nodes.push_back(rightNode);

    m_Nodes[nodeIndex].leftChild = leftIdx;
    m_Nodes[nodeIndex].rightChild = rightIdx;
    m_Nodes[nodeIndex].itemCount = 0; // Nodo interno

    Subdivide(leftIdx, depth + 1, maxLeafItems);
    Subdivide(rightIdx, depth + 1, maxLeafItems);

    return nodeIndex;
}

void SlipBVH::TraverseFrustum(int nodeIndex, const SlipFrustum& frustum, std::vector<BVHItem>& outItems) const
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_Nodes.size())) return;
    const auto& node = m_Nodes[nodeIndex];

    // Poda instantánea del nodo completo contra los 6 planos del frustum
    if (!frustum.IntersectsAABB(node.aabbMin, node.aabbMax)) {
        return;
    }

    if (node.IsLeaf()) {
        for (int i = 0; i < node.itemCount; ++i) {
            const auto& item = m_Items[node.itemStartIndex + i];
            if (frustum.IntersectsAABB(item.worldAabbMin, item.worldAabbMax)) {
                outItems.push_back(item);
            }
        }
    } else {
        if (node.leftChild >= 0) TraverseFrustum(node.leftChild, frustum, outItems);
        if (node.rightChild >= 0) TraverseFrustum(node.rightChild, frustum, outItems);
    }
}

void SlipBVH::QueryFrustum(const SlipFrustum& frustum, std::vector<BVHItem>& outVisibleItems) const
{
    if (m_Nodes.empty()) return;
    TraverseFrustum(0, frustum, outVisibleItems);
}

void SlipBVH::TraverseAABB(int nodeIndex, const glm::vec3& minPos, const glm::vec3& maxPos, std::vector<BVHItem>& outItems) const
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_Nodes.size())) return;
    const auto& node = m_Nodes[nodeIndex];

    if (node.aabbMax.x < minPos.x || node.aabbMin.x > maxPos.x ||
        node.aabbMax.y < minPos.y || node.aabbMin.y > maxPos.y ||
        node.aabbMax.z < minPos.z || node.aabbMin.z > maxPos.z) {
        return;
    }

    if (node.IsLeaf()) {
        for (int i = 0; i < node.itemCount; ++i) {
            const auto& item = m_Items[node.itemStartIndex + i];
            if (item.worldAabbMax.x >= minPos.x && item.worldAabbMin.x <= maxPos.x &&
                item.worldAabbMax.y >= minPos.y && item.worldAabbMin.y <= maxPos.y &&
                item.worldAabbMax.z >= minPos.z && item.worldAabbMin.z <= maxPos.z) {
                outItems.push_back(item);
            }
        }
    } else {
        if (node.leftChild >= 0) TraverseAABB(node.leftChild, minPos, maxPos, outItems);
        if (node.rightChild >= 0) TraverseAABB(node.rightChild, minPos, maxPos, outItems);
    }
}

void SlipBVH::QueryAABB(const glm::vec3& minPos, const glm::vec3& maxPos, std::vector<BVHItem>& outItems) const
{
    if (m_Nodes.empty()) return;
    TraverseAABB(0, minPos, maxPos, outItems);
}

// ---------------------------------------------------------------------
// SlipSpatialScene: Administrador de BVH Dual (Estático y Dinámico)
// ---------------------------------------------------------------------

void SlipSpatialScene::ComputeSubmeshWorldAABB(const glm::mat4& transform,
                                              const glm::vec3& localMin, const glm::vec3& localMax,
                                              glm::vec3& outWorldMin, glm::vec3& outWorldMax)
{
    glm::vec3 corners[8] = {
        { localMin.x, localMin.y, localMin.z },
        { localMax.x, localMin.y, localMin.z },
        { localMin.x, localMax.y, localMin.z },
        { localMax.x, localMax.y, localMin.z },
        { localMin.x, localMin.y, localMax.z },
        { localMax.x, localMin.y, localMax.z },
        { localMin.x, localMax.y, localMax.z },
        { localMax.x, localMax.y, localMax.z }
    };

    outWorldMin = glm::vec3(1e30f);
    outWorldMax = glm::vec3(-1e30f);

    for (int c = 0; c < 8; ++c) {
        glm::vec3 wc = glm::vec3(transform * glm::vec4(corners[c], 1.0f));
        outWorldMin = glm::min(outWorldMin, wc);
        outWorldMax = glm::max(outWorldMax, wc);
    }
}

void SlipSpatialScene::RebuildStatic(const std::vector<SlipEntity*>& entities)
{
    std::vector<BVHItem> staticItems;
    staticItems.reserve(entities.size() * 2);

    for (SlipEntity* ent : entities) {
        if (!ent || !ent->IsStatic()) continue;

        auto meshComp = ent->GetComponent<MeshComponent>();
        auto skinnedComp = ent->GetComponent<SkinnedMeshComponent>();
        auto terrainComp = ent->GetComponent<SlipTerrainComponent>();
        SlipPrimitiveEntity* prim = dynamic_cast<SlipPrimitiveEntity*>(ent);
        glm::mat4 modelMat = ent->getMatrix();

        SlipMesh* model = nullptr;
        if (meshComp && meshComp->IsEnabled() && meshComp->model) {
            model = meshComp->model;
        } else if (skinnedComp && skinnedComp->IsEnabled() && skinnedComp->model) {
            model = skinnedComp->model;
        } else if (terrainComp && terrainComp->IsEnabled() && terrainComp->GetMesh()) {
            model = terrainComp->GetMesh();
        }

        if (model) {
            for (size_t smIdx = 0; smIdx < model->meshes.size(); ++smIdx) {
                const auto& sm = model->meshes[smIdx];
                BVHItem item;
                item.entity = ent;
                item.mesh = model;
                item.submeshIndex = static_cast<uint32_t>(smIdx);
                item.worldTransform = modelMat;
                item.isStatic = true;

                glm::vec3 lMin = sm.hasBounds ? sm.aabbMin : model->aabbMin;
                glm::vec3 lMax = sm.hasBounds ? sm.aabbMax : model->aabbMax;
                if (skinnedComp) {
                    glm::vec3 center = (lMin + lMax) * 0.5f;
                    glm::vec3 halfExtents = (lMax - lMin) * 0.5f * 1.35f;
                    lMin = center - halfExtents;
                    lMax = center + halfExtents;
                }
                ComputeSubmeshWorldAABB(modelMat, lMin, lMax, item.worldAabbMin, item.worldAabbMax);
                staticItems.push_back(item);
            }
        } else if (prim) {
            BVHItem item;
            item.entity = ent;
            item.mesh = nullptr;
            item.submeshIndex = 0;
            item.worldTransform = modelMat;
            item.isStatic = true;
            glm::vec3 lMin(-0.5f), lMax(0.5f);
            ComputeSubmeshWorldAABB(modelMat, lMin, lMax, item.worldAabbMin, item.worldAabbMax);
            staticItems.push_back(item);
        }
    }

    m_StaticBVH.Build(std::move(staticItems));
    m_StaticDirty = false;
}

void SlipSpatialScene::UpdateDynamic(const std::vector<SlipEntity*>& entities)
{
    std::vector<BVHItem> dynamicItems;
    dynamicItems.reserve(entities.size());

    for (SlipEntity* ent : entities) {
        if (!ent || ent->IsStatic()) continue;

        auto meshComp = ent->GetComponent<MeshComponent>();
        auto skinnedComp = ent->GetComponent<SkinnedMeshComponent>();
        auto terrainComp = ent->GetComponent<SlipTerrainComponent>();
        SlipPrimitiveEntity* prim = dynamic_cast<SlipPrimitiveEntity*>(ent);
        glm::mat4 modelMat = ent->getMatrix();

        SlipMesh* model = nullptr;
        if (meshComp && meshComp->IsEnabled() && meshComp->model) {
            model = meshComp->model;
        } else if (skinnedComp && skinnedComp->IsEnabled() && skinnedComp->model) {
            model = skinnedComp->model;
        } else if (terrainComp && terrainComp->IsEnabled() && terrainComp->GetMesh()) {
            model = terrainComp->GetMesh();
        }

        if (model) {
            for (size_t smIdx = 0; smIdx < model->meshes.size(); ++smIdx) {
                const auto& sm = model->meshes[smIdx];
                BVHItem item;
                item.entity = ent;
                item.mesh = model;
                item.submeshIndex = static_cast<uint32_t>(smIdx);
                item.worldTransform = modelMat;
                item.isStatic = false;

                glm::vec3 lMin = sm.hasBounds ? sm.aabbMin : model->aabbMin;
                glm::vec3 lMax = sm.hasBounds ? sm.aabbMax : model->aabbMax;
                if (skinnedComp) {
                    glm::vec3 center = (lMin + lMax) * 0.5f;
                    glm::vec3 halfExtents = (lMax - lMin) * 0.5f * 1.35f;
                    lMin = center - halfExtents;
                    lMax = center + halfExtents;
                }
                ComputeSubmeshWorldAABB(modelMat, lMin, lMax, item.worldAabbMin, item.worldAabbMax);
                dynamicItems.push_back(item);
            }
        } else if (prim) {
            BVHItem item;
            item.entity = ent;
            item.mesh = nullptr;
            item.submeshIndex = 0;
            item.worldTransform = modelMat;
            item.isStatic = false;
            glm::vec3 lMin(-0.5f), lMax(0.5f);
            ComputeSubmeshWorldAABB(modelMat, lMin, lMax, item.worldAabbMin, item.worldAabbMax);
            dynamicItems.push_back(item);
        }
    }

    m_DynamicBVH.Build(std::move(dynamicItems));
}

void SlipSpatialScene::QueryFrustum(const SlipFrustum& frustum, std::vector<BVHItem>& outVisible) const
{
    m_StaticBVH.QueryFrustum(frustum, outVisible);
    m_DynamicBVH.QueryFrustum(frustum, outVisible);
}

void SlipSpatialScene::Clear()
{
    m_StaticBVH.Clear();
    m_DynamicBVH.Clear();
    m_StaticDirty = true;
}

