#pragma once

#ifndef SLIP_MESH_H
#define SLIP_MESH_H

#include <glm/glm.hpp>

#include <vector>
#include <string>
#include <fstream>

#include "SlipCollision.h"
#include "SlipMaterial.h"
#include "SlipShader.h"
#include "io.h"

#define MAX_BONE_INFLUENCE 4

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    float m_Weights[MAX_BONE_INFLUENCE];
    int materialIndex;
};

class SlipMesh
{
public:
    struct Material
    {
        char matPath[192];
        SlipMaterial* mat;

        Material(std::string matPath)
        {
            strcpy(this->matPath, matPath.c_str());
        }
    };

    struct Mesh
    {
        unsigned int VAO, VBO, EBO;

        int materialIndex;
        std::vector<Vertex>       vertices;
        std::vector<unsigned int> indices;
    };

    std::string path;

    bool initialized = false;

    // mesh data
    std::vector<Mesh> meshes;

    char colPath[192];
    SlipCollision* collision;

    std::vector<Material> materials;

    SlipMesh(const char* path);

    void init();
    void initColl();

    void draw(glm::mat4 transform);
    void clean();
private:
    void loadFromFile();
    void loadColFromFile(std::string path);
    void loadMatFromFile();
    void setupMesh();
};

#endif // !SLIP_MESH_H