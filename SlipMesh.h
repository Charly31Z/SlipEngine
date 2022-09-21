#pragma once

#ifndef SLIP_MESH_H
#define SLIP_MESH_H

#include <glm/glm.hpp>

#include "SlipShader.h"
#include <vector>
#include <string>

#define MAX_BONE_INFLUENCE 4

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int m_BoneIDs[MAX_BONE_INFLUENCE];
    float m_Weights[MAX_BONE_INFLUENCE];
};

struct Texture {
    unsigned int id;
    std::string type;
    std::string path;
};

class SlipMesh
{
public:
    // mesh data
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture>      textures;

    SlipMesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, std::vector<Texture> textures);
    void Draw(SlipShader& shader);
private:
    //  render data
    GLuint VAO, VBO, EBO;

    void setupMesh();
};

#endif // !SLIP_MESH_H