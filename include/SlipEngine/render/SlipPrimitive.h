#pragma once

#ifndef SLIP_PRIMITIVE_H
#define SLIP_PRIMITIVE_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/render/SlipMesh.h"
#include <vector>
#include <string>

enum class PrimitiveType {
    Cube,
    Sphere,
    Plane
};

class SLIPENGINE_API SlipPrimitiveEntity : public SlipEntity {
public:
    PrimitiveType primitiveType;
    glm::vec4 color{ 0.75f, 0.78f, 0.82f, 1.0f };
    bool wireframe = false;

    SlipPrimitiveEntity(PrimitiveType type = PrimitiveType::Cube, const std::string& entityName = "Primitivo");
    virtual ~SlipPrimitiveEntity();

    void InitGeometry();
    void Draw(const glm::mat4& view, const glm::mat4& projection, unsigned int shaderProgram);
    void DrawInspectorUI() override;

    unsigned int GetVAO() const { return m_VAO; }
    unsigned int GetIndexCount() const { return m_IndexCount; }

private:
    unsigned int m_VAO = 0;
    unsigned int m_VBO = 0;
    unsigned int m_EBO = 0;
    unsigned int m_IndexCount = 0;
    bool m_Initialized = false;
};

class SLIPENGINE_API SlipPrimitive {
public:
    static void GenerateCube(float size, std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices);
    static void GenerateSphere(float radius, int rings, int sectors, std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices);
    static void GeneratePlane(float width, float length, std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices);

    static SlipPrimitiveEntity* CreateEntity(PrimitiveType type, const std::string& name = "");
    static SlipEntity* CreatePrimitiveEntity(PrimitiveType type, const std::string& name = "");
};

#endif // !SLIP_PRIMITIVE_H

