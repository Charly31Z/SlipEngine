#pragma once

#ifndef SLIP_FRUSTUM_H
#define SLIP_FRUSTUM_H

#include "SlipEngine.h"
#include <glm/glm.hpp>

struct SLIPENGINE_API SlipFrustumPlane
{
    glm::vec3 normal = glm::vec3(0.0f);
    float distance = 0.0f;

    void Normalize()
    {
        float length = glm::length(normal);
        if (length > 1e-6f) {
            normal /= length;
            distance /= length;
        }
    }

    float GetSignedDistanceToPoint(const glm::vec3& point) const
    {
        return glm::dot(normal, point) + distance;
    }
};

class SLIPENGINE_API SlipFrustum
{
public:
    enum Plane {
        LEFT = 0,
        RIGHT,
        BOTTOM,
        TOP,
        NEAR_PLANE,
        FAR_PLANE,
        COUNT = 6
    };

    SlipFrustumPlane planes[COUNT];

    SlipFrustum() = default;
    SlipFrustum(const glm::mat4& viewProjection);

    void Update(const glm::mat4& viewProjection);

    bool IntersectsAABB(const glm::vec3& minPos, const glm::vec3& maxPos) const;
    bool ContainsPoint(const glm::vec3& point) const;
};

#endif // !SLIP_FRUSTUM_H

