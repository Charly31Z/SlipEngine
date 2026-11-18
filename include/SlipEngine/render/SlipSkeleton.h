#pragma once

#ifndef SLIP_SKELETON_H
#define SLIP_SKELETON_H

#include "SlipEngine/core/SlipExport.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct BoneInfo {
    int id = -1;
    std::string name;
    int parentIndex = -1;
    glm::mat4 offsetMatrix = glm::mat4(1.0f);     // Inverse bind pose matrix
    glm::mat4 localBindTransform = glm::mat4(1.0f); // Rest pose local transform
};

class SLIPENGINE_API SlipSkeleton {
public:
    SlipSkeleton() = default;

    int AddBone(const std::string& name, const glm::mat4& offset, const glm::mat4& localBind = glm::mat4(1.0f), int parentIndex = -1) {
        auto it = m_BoneNameToIndex.find(name);
        if (it != m_BoneNameToIndex.end()) {
            return it->second;
        }
        int newIdx = static_cast<int>(m_Bones.size());
        BoneInfo info;
        info.id = newIdx;
        info.name = name;
        info.parentIndex = parentIndex;
        info.offsetMatrix = offset;
        info.localBindTransform = localBind;
        m_Bones.push_back(info);
        m_BoneNameToIndex[name] = newIdx;
        return newIdx;
    }

    int GetBoneIndex(const std::string& name) const {
        auto it = m_BoneNameToIndex.find(name);
        if (it != m_BoneNameToIndex.end()) return it->second;
        return -1;
    }

    const BoneInfo* GetBone(const std::string& name) const {
        int idx = GetBoneIndex(name);
        if (idx >= 0 && idx < static_cast<int>(m_Bones.size())) {
            return &m_Bones[idx];
        }
        return nullptr;
    }

    const BoneInfo* GetBone(int index) const {
        if (index >= 0 && index < static_cast<int>(m_Bones.size())) {
            return &m_Bones[index];
        }
        return nullptr;
    }

    BoneInfo* GetBone(int index) {
        if (index >= 0 && index < static_cast<int>(m_Bones.size())) {
            return &m_Bones[index];
        }
        return nullptr;
    }

    size_t GetBoneCount() const { return m_Bones.size(); }
    const std::vector<BoneInfo>& GetBones() const { return m_Bones; }
    std::vector<BoneInfo>& GetBones() { return m_Bones; }

    void ComputeTransforms(const class AnimationClip* clip, float time, std::vector<glm::mat4>& outTransforms) const;

private:
    std::vector<BoneInfo> m_Bones;
    std::unordered_map<std::string, int> m_BoneNameToIndex;
};

#endif // !SLIP_SKELETON_H

