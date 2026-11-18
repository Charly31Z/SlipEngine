#pragma once

#ifndef SLIP_BVH_H
#define SLIP_BVH_H

#include "SlipEngine.h"
#include <glm/glm.hpp>
#include <vector>
#include "SlipFrustum.h"

class SlipEntity;
class SlipMesh;

struct SLIPENGINE_API BVHItem
{
    SlipEntity* entity = nullptr;
    SlipMesh* mesh = nullptr;
    uint32_t submeshIndex = 0;
    glm::mat4 worldTransform = glm::mat4(1.0f);
    glm::vec3 worldAabbMin = glm::vec3(-0.5f);
    glm::vec3 worldAabbMax = glm::vec3(0.5f);
    bool isStatic = true;

    glm::vec3 GetCenter() const {
        return (worldAabbMin + worldAabbMax) * 0.5f;
    }
};

struct SLIPENGINE_API BVHNode
{
    glm::vec3 aabbMin = glm::vec3(1e30f);
    glm::vec3 aabbMax = glm::vec3(-1e30f);
    int leftChild = -1;
    int rightChild = -1;
    int itemStartIndex = -1;
    int itemCount = 0;

    bool IsLeaf() const { return leftChild == -1 && rightChild == -1; }
};

class SLIPENGINE_API SlipBVH
{
public:
    SlipBVH() = default;
    ~SlipBVH() = default;

    void Build(std::vector<BVHItem> items, int maxLeafItems = 2);
    void Clear();

    void QueryFrustum(const SlipFrustum& frustum, std::vector<BVHItem>& outVisibleItems) const;
    void QueryAABB(const glm::vec3& minPos, const glm::vec3& maxPos, std::vector<BVHItem>& outItems) const;

    bool IsEmpty() const { return m_Nodes.empty(); }
    const std::vector<BVHNode>& GetNodes() const { return m_Nodes; }
    const std::vector<BVHItem>& GetItems() const { return m_Items; }

private:
    std::vector<BVHNode> m_Nodes;
    std::vector<BVHItem> m_Items;

    int Subdivide(int nodeIndex, int depth, int maxLeafItems);
    void TraverseFrustum(int nodeIndex, const SlipFrustum& frustum, std::vector<BVHItem>& outItems) const;
    void TraverseAABB(int nodeIndex, const glm::vec3& minPos, const glm::vec3& maxPos, std::vector<BVHItem>& outItems) const;
};

class SLIPENGINE_API SlipSpatialScene
{
public:
    SlipSpatialScene() = default;
    ~SlipSpatialScene() = default;

    void RebuildStatic(const std::vector<SlipEntity*>& entities);
    void UpdateDynamic(const std::vector<SlipEntity*>& entities);
    void QueryFrustum(const SlipFrustum& frustum, std::vector<BVHItem>& outVisible) const;

    void Clear();

    const SlipBVH& GetStaticBVH() const { return m_StaticBVH; }
    const SlipBVH& GetDynamicBVH() const { return m_DynamicBVH; }

    void MarkStaticDirty() { m_StaticDirty = true; }
    bool IsStaticDirty() const { return m_StaticDirty; }

    static void ComputeSubmeshWorldAABB(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax,
                                       glm::vec3& outWorldMin, glm::vec3& outWorldMax);

private:
    SlipBVH m_StaticBVH;
    SlipBVH m_DynamicBVH;
    bool m_StaticDirty = true;
};

#endif // !SLIP_BVH_H

