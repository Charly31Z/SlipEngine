#pragma once

#ifndef SLIP_ANIMATION_H
#define SLIP_ANIMATION_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/SlipSkeleton.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct KeyPosition {
    glm::vec3 position{0.0f};
    float timeStamp = 0.0f;
};

struct KeyRotation {
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    float timeStamp = 0.0f;
};

struct KeyScale {
    glm::vec3 scale{1.0f};
    float timeStamp = 0.0f;
};

struct AnimationTrack {
    std::string boneName;
    int boneIndex = -1;
    std::vector<KeyPosition> positions;
    std::vector<KeyRotation> rotations;
    std::vector<KeyScale> scales;

    glm::vec3 InterpolatePosition(float animTime) const {
        if (positions.empty()) return glm::vec3(0.0f);
        if (positions.size() == 1) return positions[0].position;

        size_t p0Index = 0;
        for (size_t i = 0; i < positions.size() - 1; ++i) {
            if (animTime < positions[i + 1].timeStamp) {
                p0Index = i;
                break;
            }
        }
        size_t p1Index = p0Index + 1;
        if (p1Index >= positions.size()) return positions.back().position;

        float dt = positions[p1Index].timeStamp - positions[p0Index].timeStamp;
        float factor = (dt > 1e-6f) ? ((animTime - positions[p0Index].timeStamp) / dt) : 0.0f;
        factor = glm::clamp(factor, 0.0f, 1.0f);
        return glm::mix(positions[p0Index].position, positions[p1Index].position, factor);
    }

    glm::quat InterpolateRotation(float animTime) const {
        if (rotations.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (rotations.size() == 1) return glm::normalize(rotations[0].orientation);

        size_t p0Index = 0;
        for (size_t i = 0; i < rotations.size() - 1; ++i) {
            if (animTime < rotations[i + 1].timeStamp) {
                p0Index = i;
                break;
            }
        }
        size_t p1Index = p0Index + 1;
        if (p1Index >= rotations.size()) return glm::normalize(rotations.back().orientation);

        float dt = rotations[p1Index].timeStamp - rotations[p0Index].timeStamp;
        float factor = (dt > 1e-6f) ? ((animTime - rotations[p0Index].timeStamp) / dt) : 0.0f;
        factor = glm::clamp(factor, 0.0f, 1.0f);
        return glm::normalize(glm::slerp(rotations[p0Index].orientation, rotations[p1Index].orientation, factor));
    }

    glm::vec3 InterpolateScale(float animTime) const {
        if (scales.empty()) return glm::vec3(1.0f);
        if (scales.size() == 1) return scales[0].scale;

        size_t p0Index = 0;
        for (size_t i = 0; i < scales.size() - 1; ++i) {
            if (animTime < scales[i + 1].timeStamp) {
                p0Index = i;
                break;
            }
        }
        size_t p1Index = p0Index + 1;
        if (p1Index >= scales.size()) return scales.back().scale;

        float dt = scales[p1Index].timeStamp - scales[p0Index].timeStamp;
        float factor = (dt > 1e-6f) ? ((animTime - scales[p0Index].timeStamp) / dt) : 0.0f;
        factor = glm::clamp(factor, 0.0f, 1.0f);
        return glm::mix(scales[p0Index].scale, scales[p1Index].scale, factor);
    }

    glm::mat4 Evaluate(float animTime) const {
        glm::vec3 pos = InterpolatePosition(animTime);
        glm::quat rot = InterpolateRotation(animTime);
        glm::vec3 scl = InterpolateScale(animTime);

        glm::mat4 t = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 r = glm::toMat4(rot);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scl);
        return t * r * s;
    }
};

class SLIPENGINE_API AnimationClip {
public:
    std::string name = "DefaultClip";
    float duration = 1.0f;          // Duración total en segundos
    float ticksPerSecond = 24.0f;
    std::vector<AnimationTrack> tracks;
    std::unordered_map<std::string, size_t> trackMap;

    int startFrame = 0;
    int endFrame = 0;

    void AddTrack(const AnimationTrack& track) {
        trackMap[track.boneName] = tracks.size();
        tracks.push_back(track);
    }

    const AnimationTrack* FindTrack(const std::string& boneName) const {
        auto it = trackMap.find(boneName);
        if (it != trackMap.end() && it->second < tracks.size()) {
            return &tracks[it->second];
        }
        return nullptr;
    }

    int GetTotalFrames() const {
        return static_cast<int>(duration * ticksPerSecond);
    }
};

inline void SlipSkeleton::ComputeTransforms(const AnimationClip* clip, float time, std::vector<glm::mat4>& outTransforms) const {
    size_t count = (std::min)(m_Bones.size(), static_cast<size_t>(100));
    outTransforms.assign(count, glm::mat4(1.0f));
    std::vector<glm::mat4> globalPose(m_Bones.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < m_Bones.size(); ++i) {
        const auto& b = m_Bones[i];
        glm::mat4 localMat = b.localBindTransform;

        if (clip) {
            const AnimationTrack* trk = clip->FindTrack(b.name);
            if (trk) {
                const glm::mat4& bind = b.localBindTransform;
                glm::vec3 pos = glm::vec3(bind[3]);
                glm::vec3 scl(
                    glm::length(glm::vec3(bind[0])),
                    glm::length(glm::vec3(bind[1])),
                    glm::length(glm::vec3(bind[2]))
                );
                glm::mat3 rotM;
                rotM[0] = (scl.x > 1e-6f) ? glm::vec3(bind[0]) / scl.x : glm::vec3(1, 0, 0);
                rotM[1] = (scl.y > 1e-6f) ? glm::vec3(bind[1]) / scl.y : glm::vec3(0, 1, 0);
                rotM[2] = (scl.z > 1e-6f) ? glm::vec3(bind[2]) / scl.z : glm::vec3(0, 0, 1);
                glm::quat rot = glm::normalize(glm::quat_cast(rotM));

                if (!trk->positions.empty()) pos = trk->InterpolatePosition(time);
                if (!trk->rotations.empty()) rot = trk->InterpolateRotation(time);
                if (!trk->scales.empty()) scl = trk->InterpolateScale(time);

                localMat = glm::translate(glm::mat4(1.0f), pos) *
                           glm::mat4_cast(rot) *
                           glm::scale(glm::mat4(1.0f), scl);
            }
        }

        if (b.parentIndex >= 0 && b.parentIndex < static_cast<int>(globalPose.size())) {
            globalPose[i] = globalPose[b.parentIndex] * localMat;
        } else {
            globalPose[i] = localMat;
        }

        if (i < count) {
            outTransforms[i] = globalPose[i] * b.offsetMatrix;
        }
    }
}

#endif // !SLIP_ANIMATION_H

