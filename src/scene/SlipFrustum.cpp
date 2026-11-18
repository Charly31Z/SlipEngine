#include "SlipFrustum.h"
#include <algorithm>

SlipFrustum::SlipFrustum(const glm::mat4& viewProjection)
{
    Update(viewProjection);
}

void SlipFrustum::Update(const glm::mat4& m)
{
    // Extracción estándar Gribb/Hartmann
    // Left
    planes[LEFT].normal.x = m[0][3] + m[0][0];
    planes[LEFT].normal.y = m[1][3] + m[1][0];
    planes[LEFT].normal.z = m[2][3] + m[2][0];
    planes[LEFT].distance = m[3][3] + m[3][0];

    // Right
    planes[RIGHT].normal.x = m[0][3] - m[0][0];
    planes[RIGHT].normal.y = m[1][3] - m[1][0];
    planes[RIGHT].normal.z = m[2][3] - m[2][0];
    planes[RIGHT].distance = m[3][3] - m[3][0];

    // Bottom
    planes[BOTTOM].normal.x = m[0][3] + m[0][1];
    planes[BOTTOM].normal.y = m[1][3] + m[1][1];
    planes[BOTTOM].normal.z = m[2][3] + m[2][1];
    planes[BOTTOM].distance = m[3][3] + m[3][1];

    // Top
    planes[TOP].normal.x = m[0][3] - m[0][1];
    planes[TOP].normal.y = m[1][3] - m[1][1];
    planes[TOP].normal.z = m[2][3] - m[2][1];
    planes[TOP].distance = m[3][3] - m[3][1];

    // Near
    planes[NEAR_PLANE].normal.x = m[0][3] + m[0][2];
    planes[NEAR_PLANE].normal.y = m[1][3] + m[1][2];
    planes[NEAR_PLANE].normal.z = m[2][3] + m[2][2];
    planes[NEAR_PLANE].distance = m[3][3] + m[3][2];

    // Far
    planes[FAR_PLANE].normal.x = m[0][3] - m[0][2];
    planes[FAR_PLANE].normal.y = m[1][3] - m[1][2];
    planes[FAR_PLANE].normal.z = m[2][3] - m[2][2];
    planes[FAR_PLANE].distance = m[3][3] - m[3][2];

    for (int i = 0; i < COUNT; ++i) {
        planes[i].Normalize();
    }
}

bool SlipFrustum::IntersectsAABB(const glm::vec3& minPos, const glm::vec3& maxPos) const
{
    for (int i = 0; i < COUNT; ++i) {
        const auto& p = planes[i];

        // Vértice P (más alineado con la normal del plano)
        glm::vec3 pVertex = minPos;
        if (p.normal.x >= 0.0f) pVertex.x = maxPos.x;
        if (p.normal.y >= 0.0f) pVertex.y = maxPos.y;
        if (p.normal.z >= 0.0f) pVertex.z = maxPos.z;

        if (p.GetSignedDistanceToPoint(pVertex) < -0.05f) {
            return false;
        }
    }
    return true;
}

bool SlipFrustum::ContainsPoint(const glm::vec3& point) const
{
    for (int i = 0; i < COUNT; ++i) {
        if (planes[i].GetSignedDistanceToPoint(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

