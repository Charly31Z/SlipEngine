#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/render/SlipGL.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
static void CalculateTangents(std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    for (auto& v : vertices) {
        v.Tangent = glm::vec3(0.0f);
        v.Bitangent = glm::vec3(0.0f);
    }
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        unsigned int i0 = indices[i];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const glm::vec3& v0 = vertices[i0].Position;
        const glm::vec3& v1 = vertices[i1].Position;
        const glm::vec3& v2 = vertices[i2].Position;

        const glm::vec2& uv0 = vertices[i0].TexCoords;
        const glm::vec2& uv1 = vertices[i1].TexCoords;
        const glm::vec2& uv2 = vertices[i2].TexCoords;

        glm::vec3 deltaPos1 = v1 - v0;
        glm::vec3 deltaPos2 = v2 - v0;

        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float r = (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
        float f = (std::abs(r) > 1e-6f) ? (1.0f / r) : 0.0f;

        glm::vec3 tangent = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * f;
        glm::vec3 bitangent = (deltaPos2 * deltaUV1.x - deltaPos1 * deltaUV2.x) * f;

        vertices[i0].Tangent += tangent;
        vertices[i1].Tangent += tangent;
        vertices[i2].Tangent += tangent;

        vertices[i0].Bitangent += bitangent;
        vertices[i1].Bitangent += bitangent;
        vertices[i2].Bitangent += bitangent;
    }

    for (auto& v : vertices) {
        glm::vec3 n = glm::length(v.Normal) > 1e-6f ? glm::normalize(v.Normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 t = v.Tangent;
        glm::vec3 b = v.Bitangent;

        if (glm::length(t) > 1e-6f) {
            t = glm::normalize(t - n * glm::dot(t, n));
        } else {
            t = (std::abs(n.y) > 0.99f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), n));
        }

        if (glm::length(b) > 1e-6f) {
            b = glm::normalize(b - n * glm::dot(b, n) - t * glm::dot(b, t));
        } else {
            b = glm::normalize(glm::cross(n, t));
        }

        v.Tangent = t;
        v.Bitangent = b;
    }
}
} // namespace

void SlipPrimitive::GenerateCube(float size, std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    float h = size * 0.5f;

    struct FaceVertex {
        glm::vec3 pos;
        glm::vec3 norm;
        glm::vec2 uv;
    };

    FaceVertex rawFaces[24] = {
        // Front (+Z)
        { {-h, -h,  h}, {0, 0, 1}, {0, 0} },
        { { h, -h,  h}, {0, 0, 1}, {1, 0} },
        { { h,  h,  h}, {0, 0, 1}, {1, 1} },
        { {-h,  h,  h}, {0, 0, 1}, {0, 1} },

        // Back (-Z)
        { { h, -h, -h}, {0, 0, -1}, {0, 0} },
        { {-h, -h, -h}, {0, 0, -1}, {1, 0} },
        { {-h,  h, -h}, {0, 0, -1}, {1, 1} },
        { { h,  h, -h}, {0, 0, -1}, {0, 1} },

        // Top (+Y)
        { {-h,  h,  h}, {0, 1, 0}, {0, 0} },
        { { h,  h,  h}, {0, 1, 0}, {1, 0} },
        { { h,  h, -h}, {0, 1, 0}, {1, 1} },
        { {-h,  h, -h}, {0, 1, 0}, {0, 1} },

        // Bottom (-Y)
        { {-h, -h, -h}, {0, -1, 0}, {0, 0} },
        { { h, -h, -h}, {0, -1, 0}, {1, 0} },
        { { h, -h,  h}, {0, -1, 0}, {1, 1} },
        { {-h, -h,  h}, {0, -1, 0}, {0, 1} },

        // Right (+X)
        { { h, -h,  h}, {1, 0, 0}, {0, 0} },
        { { h, -h, -h}, {1, 0, 0}, {1, 0} },
        { { h,  h, -h}, {1, 0, 0}, {1, 1} },
        { { h,  h,  h}, {1, 0, 0}, {0, 1} },

        // Left (-X)
        { {-h, -h, -h}, {-1, 0, 0}, {0, 0} },
        { {-h, -h,  h}, {-1, 0, 0}, {1, 0} },
        { {-h,  h,  h}, {-1, 0, 0}, {1, 1} },
        { {-h,  h, -h}, {-1, 0, 0}, {0, 1} }
    };

    for (int i = 0; i < 24; ++i) {
        Vertex v{};
        v.Position = rawFaces[i].pos;
        v.Normal = rawFaces[i].norm;
        v.TexCoords = rawFaces[i].uv;
        outVertices.push_back(v);
    }

    for (unsigned int f = 0; f < 6; ++f) {
        unsigned int base = f * 4;
        outIndices.push_back(base + 0);
        outIndices.push_back(base + 1);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 3);
        outIndices.push_back(base + 0);
    }
    CalculateTangents(outVertices, outIndices);
}

void SlipPrimitive::GenerateSphere(float radius, int rings, int sectors, std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    if (rings < 4) rings = 4;
    if (sectors < 4) sectors = 4;

    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            float const y = sin(-M_PI / 2.0f + M_PI * r * R);
            float const x = cos(2.0f * M_PI * s * S) * sin(M_PI * r * R);
            float const z = sin(2.0f * M_PI * s * S) * sin(M_PI * r * R);

            Vertex v{};
            v.Position = glm::vec3(x, y, z) * radius;
            v.Normal = glm::normalize(glm::vec3(x, y, z));
            v.TexCoords = glm::vec2(s * S, r * R);
            outVertices.push_back(v);
        }
    }

    for (int r = 0; r < rings - 1; ++r) {
        for (int s = 0; s < sectors - 1; ++s) {
            int curRow = r * sectors;
            int nextRow = (r + 1) * sectors;

            outIndices.push_back(curRow + s);
            outIndices.push_back(nextRow + s);
            outIndices.push_back(nextRow + (s + 1));

            outIndices.push_back(curRow + s);
            outIndices.push_back(nextRow + (s + 1));
            outIndices.push_back(curRow + (s + 1));
        }
    }
    CalculateTangents(outVertices, outIndices);
}

void SlipPrimitive::GeneratePlane(float width, float length, std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    float hw = width * 0.5f;
    float hl = length * 0.5f;

    Vertex v0{}, v1{}, v2{}, v3{};

    v0.Position = glm::vec3(-hw, 0.0f,  hl);
    v0.Normal   = glm::vec3(0.0f, 1.0f, 0.0f);
    v0.TexCoords= glm::vec2(0.0f, 0.0f);

    v1.Position = glm::vec3( hw, 0.0f,  hl);
    v1.Normal   = glm::vec3(0.0f, 1.0f, 0.0f);
    v1.TexCoords= glm::vec2(1.0f, 0.0f);

    v2.Position = glm::vec3( hw, 0.0f, -hl);
    v2.Normal   = glm::vec3(0.0f, 1.0f, 0.0f);
    v2.TexCoords= glm::vec2(1.0f, 1.0f);

    v3.Position = glm::vec3(-hw, 0.0f, -hl);
    v3.Normal   = glm::vec3(0.0f, 1.0f, 0.0f);
    v3.TexCoords= glm::vec2(0.0f, 1.0f);

    outVertices.push_back(v0);
    outVertices.push_back(v1);
    outVertices.push_back(v2);
    outVertices.push_back(v3);

    outIndices.push_back(0);
    outIndices.push_back(1);
    outIndices.push_back(2);
    outIndices.push_back(2);
    outIndices.push_back(3);
    outIndices.push_back(0);
    CalculateTangents(outVertices, outIndices);
}

SlipPrimitiveEntity* SlipPrimitive::CreateEntity(PrimitiveType type, const std::string& name) {
    std::string defaultName = name;
    if (defaultName.empty()) {
        switch (type) {
            case PrimitiveType::Cube:   defaultName = "Cubo"; break;
            case PrimitiveType::Sphere: defaultName = "Esfera"; break;
            case PrimitiveType::Plane:  defaultName = "Plano"; break;
        }
    }

    SlipPrimitiveEntity* entity = new SlipPrimitiveEntity(type, defaultName);
    entity->InitGeometry();
    return entity;
}

SlipPrimitiveEntity::SlipPrimitiveEntity(PrimitiveType type, const std::string& entityName)
    : SlipEntity(entityName), primitiveType(type) {
    position = glm::vec3(0.0f, 0.0f, 0.0f);
    rotation = glm::vec3(0.0f, 0.0f, 0.0f);
    scale = glm::vec3(1.0f, 1.0f, 1.0f);
}

SlipPrimitiveEntity::~SlipPrimitiveEntity() {
    if (m_VAO != 0) {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO != 0) {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    if (m_EBO != 0) {
        glDeleteBuffers(1, &m_EBO);
        m_EBO = 0;
    }
    m_Initialized = false;
}

void SlipPrimitiveEntity::InitGeometry() {
    if (m_Initialized) return;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    switch (primitiveType) {
        case PrimitiveType::Cube:
            SlipPrimitive::GenerateCube(1.0f, vertices, indices);
            break;
        case PrimitiveType::Sphere:
            SlipPrimitive::GenerateSphere(0.5f, 20, 20, vertices, indices);
            break;
        case PrimitiveType::Plane:
            SlipPrimitive::GeneratePlane(4.0f, 4.0f, vertices, indices);
            break;
    }

    m_IndexCount = (unsigned int)indices.size();

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Posición
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Position));

    // Normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    // TexCoords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    glBindVertexArray(0);

    m_Initialized = true;
}

void SlipPrimitiveEntity::Draw(const glm::mat4& view, const glm::mat4& projection, unsigned int shaderProgram) {
    if (!m_Initialized || m_VAO == 0 || shaderProgram == 0) return;

    glUseProgram(shaderProgram);

    glm::mat4 model = getMatrix();
    GLint uModelLoc = glGetUniformLocation(shaderProgram, "u_Model");
    GLint uViewLoc = glGetUniformLocation(shaderProgram, "u_View");
    GLint uProjLoc = glGetUniformLocation(shaderProgram, "u_Projection");
    GLint uColorLoc = glGetUniformLocation(shaderProgram, "u_Color");

    if (uModelLoc >= 0) glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));
    if (uViewLoc >= 0) glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (uProjLoc >= 0) glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    if (uColorLoc >= 0) glUniform4f(uColorLoc, color.r, color.g, color.b, color.a);

#if !defined(SLIP_PLATFORM_ANDROID)
    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
#endif

    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

#if !defined(SLIP_PLATFORM_ANDROID)
    if (wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
#endif

    glUseProgram(0);
}

void SlipPrimitiveEntity::DrawInspectorUI() {
    // Implementado de forma reactiva en InspectorPanel
}

SlipEntity* SlipPrimitive::CreatePrimitiveEntity(PrimitiveType type, const std::string& name) {
    std::string entName = name;
    if (entName.empty()) {
        switch (type) {
            case PrimitiveType::Cube:   entName = "Cubo"; break;
            case PrimitiveType::Sphere: entName = "Esfera"; break;
            case PrimitiveType::Plane:  entName = "Plano"; break;
        }
    }

    SlipEntity* ent = new SlipEntity(entName);
    auto meshComp = ent->AddComponent<MeshComponent>();

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    switch (type) {
        case PrimitiveType::Cube: {
            ent->position = glm::vec3(0.0f, 0.5f, 0.0f);
            if (auto tc = ent->GetComponent<TransformComponent>()) {
                tc->position = glm::vec3(0.0f, 0.5f, 0.0f);
            }
            GenerateCube(1.0f, vertices, indices);
            meshComp->model = new SlipMesh(vertices, indices);
            meshComp->modelPath = "primitive:cube";
            ent->AddComponent<BoxColliderComponent>(glm::vec3(1.0f));
            break;
        }
        case PrimitiveType::Sphere: {
            ent->position = glm::vec3(0.0f, 0.5f, 0.0f);
            if (auto tc = ent->GetComponent<TransformComponent>()) {
                tc->position = glm::vec3(0.0f, 0.5f, 0.0f);
            }
            GenerateSphere(0.5f, 24, 24, vertices, indices);
            meshComp->model = new SlipMesh(vertices, indices);
            meshComp->modelPath = "primitive:sphere";
            ent->AddComponent<SphereColliderComponent>(0.5f);
            break;
        }
        case PrimitiveType::Plane: {
            GeneratePlane(10.0f, 10.0f, vertices, indices);
            meshComp->model = new SlipMesh(vertices, indices);
            meshComp->modelPath = "primitive:plane";
            ent->AddComponent<BoxColliderComponent>(glm::vec3(10.0f, 0.05f, 10.0f));
            break;
        }
    }

    return ent;
}

