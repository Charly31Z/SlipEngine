#pragma once

#ifndef SLIP_MESH_H
#define SLIP_MESH_H

#include "SlipEngine.h"

#include <glm/glm.hpp>

#include <vector>
#include <string>
#include <fstream>
#include <functional>

#include "SlipCollision.h"
#include "SlipMaterial.h"
#include "SlipShader.h"
#include "SlipCluster.h"
#include "io.h"

#define MAX_BONE_INFLUENCE 4

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec2 TexCoords2; // Canal UV2 para Lightmaps
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    float m_Weights[MAX_BONE_INFLUENCE];
    int materialIndex;
};

// Formato legado para compatibilidad hacia atrás con cachés SLIPMSH5 y anteriores
struct VertexLegacyV5 {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    float m_Weights[MAX_BONE_INFLUENCE];
    int materialIndex;
};

class SLIPENGINE_API SlipMesh
{
public:
    using BakeCallback = std::function<bool(const std::string& modelPath, std::string& outCachePath)>;
    static void SetBakeCallback(BakeCallback cb);

    struct Material
    {
        char matPath[192] = { 0 };
        std::string slotName;
        SlipMaterial* mat = nullptr;

        Material(std::string rawEntry)
        {
            std::memset(this->matPath, 0, sizeof(this->matPath));
            mat = nullptr;
            size_t pipePos = rawEntry.find('|');
            if (pipePos != std::string::npos) {
                slotName = rawEntry.substr(0, pipePos);
                std::string p = rawEntry.substr(pipePos + 1);
                std::strncpy(this->matPath, p.c_str(), sizeof(this->matPath) - 1);
            } else {
                std::strncpy(this->matPath, rawEntry.c_str(), sizeof(this->matPath) - 1);
                slotName = rawEntry;
            }
        }
    };

    struct Mesh
    {
        unsigned int VAO = 0;
        unsigned int VBO = 0;
        unsigned int EBO = 0;

        int materialIndex = 0;
        std::vector<Vertex>       vertices;
        std::vector<unsigned int> indices;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t baseVertex = 0;
        uint32_t vertexCount = 0;

        glm::vec3 aabbMin = glm::vec3(-0.5f);
        glm::vec3 aabbMax = glm::vec3(0.5f);
        bool hasBounds = false;
    };

    std::string path;

    bool initialized = false;
    bool isUnifiedBuffer = false;
    unsigned int masterVAO = 0;
    unsigned int masterVBO = 0;
    unsigned int masterEBO = 0;

    // Datos de geometría virtualizada V-Clust (Voxel-Anchored Cluster LOD)
    bool hasClusterLOD = false;
    unsigned int clusterVAO = 0;
    unsigned int clusterVBO = 0;
    unsigned int clusterEBO = 0;
    SlipClusterHierarchyHeader clusterHeader{};
    std::vector<SlipCluster> clusters;
    std::vector<SlipClusterVertex> clusterVertices;
    std::vector<uint32_t> clusterIndices;

    // mesh data
    std::vector<Mesh> meshes;

    glm::vec3 aabbMin = glm::vec3(-0.5f);
    glm::vec3 aabbMax = glm::vec3(0.5f);
    bool hasBounds = false;

    char colPath[192] = { 0 };
    SlipCollision* collision = nullptr;

    std::vector<Material> materials;

    SlipMesh(const char* path);
    SlipMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    ~SlipMesh();

    void init();
    void initColl();
    void computeBounds();
    void setupClusterBuffers();

    void draw(glm::mat4 transform, SlipMaterial* overrideMaterial = nullptr, bool bindMaterial = true);
    void clean();

    const std::vector<Vertex>& GetUnifiedVertices() const { return m_UnifiedVertices; }
    const std::vector<uint32_t>& GetUnifiedIndices() const { return m_UnifiedIndices; }
private:
    std::vector<Vertex> m_UnifiedVertices;
    std::vector<uint32_t> m_UnifiedIndices;
    void loadFromFile();
    void loadColFromFile(std::string path);
    void loadMatFromFile();
    void setupMesh();
};

#endif // !SLIP_MESH_H