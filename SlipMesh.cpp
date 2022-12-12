#include "SlipMesh.h"

#include <glad/glad.h>

void SlipMesh::init()
{
    setupMesh();

    initialized = true;
}

SlipMesh::SlipMesh(const char* path)
{
    this->path = path;

    loadFromFile(path);
    loadColFromFile(colPath);
    loadMatFromFile();
}

void SlipMesh::clean()
{
    for (int i = 0; i < meshes.size(); i++)
    {
        glDeleteVertexArrays(1, &meshes[i].VAO);
        glDeleteBuffers(1, &meshes[i].VBO);
        glDeleteBuffers(1, &meshes[i].EBO);
    }

    for (int i = 0; i < materials.size(); i++)
    {
        materials[i].mat->shader->destroy();
    }
    
}

void SlipMesh::draw(glm::mat4 transform)
{
    for (int i = 0; i < meshes.size(); i++)
    {
        glm::mat4 model = glm::mat4(1.f) * transform;

        materials[meshes[i].materialIndex].mat->bind(model);

        // draw mesh
        glBindVertexArray(meshes[i].VAO);
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(meshes[i].indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE0);
    }
}

void SlipMesh::loadFromFile(const char* path)
{
    std::ifstream input1(path, std::ios::binary);

    size_t meshesCount;
    IO::read(input1, meshesCount);

    for (int i = 0; i < meshesCount; i++)
    {
        Mesh tempMesh;

        size_t vertexCount;
        size_t indicesCount;

        IO::read(input1, tempMesh.materialIndex);
        IO::read(input1, vertexCount);
        IO::read(input1, indicesCount);

        for (int j = 0; j < vertexCount; j++)
        {
            Vertex vertex;

            float v3[3];
            float v2[2];

            IO::read(input1, v3);
            vertex.Position = glm::vec3(v3[0], v3[1], v3[2]);

            IO::read(input1, v3);
            vertex.Normal = glm::vec3(v3[0], v3[1], v3[2]);

            IO::read(input1, v2);
            vertex.TexCoords = glm::vec2(v2[0], v2[1]);

            tempMesh.vertices.push_back(vertex);
        }

        for (int k = 0; k < indicesCount; k++)
        {
            unsigned int index;

            IO::read(input1, index);
            tempMesh.indices.push_back(index);
        }

        meshes.push_back(tempMesh);
    }

    IO::read(input1, colPath);

    size_t materialsCount;
    IO::read(input1, materialsCount);

    for (int i = 0; i < materialsCount; i++)
    {
        char matPath[192];

        IO::read(input1, matPath);
        materials.emplace_back(matPath);
    }

    input1.close();
}

void SlipMesh::loadColFromFile(std::string path)
{
    if (std::strlen(colPath) != 0)
        collision = new SlipCollision(path);
}

void SlipMesh::loadMatFromFile()
{
    for (int i = 0; i < materials.size(); i++)
    {
        materials[i].mat = new SlipMaterial(materials[i].matPath);
    }
}

void SlipMesh::setupMesh()
{
    for (int i = 0; i < meshes.size(); i++)
    {
        // create buffers/arrays
        glGenVertexArrays(1, &meshes[i].VAO);
        glGenBuffers(1, &meshes[i].VBO);
        glGenBuffers(1, &meshes[i].EBO);

        glBindVertexArray(meshes[i].VAO);
        // load data into vertex buffers
        glBindBuffer(GL_ARRAY_BUFFER, meshes[i].VBO);
        // A great thing about structs is that their memory layout is sequential for all its items.
        // The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
        // again translates to 3/2 floats which translates to a byte array.
        glBufferData(GL_ARRAY_BUFFER, meshes[i].vertices.size() * sizeof(Vertex), &meshes[i].vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshes[i].EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, meshes[i].indices.size() * sizeof(unsigned int), &meshes[i].indices[0], GL_STATIC_DRAW);

        // set the vertex attribute pointers
        // vertex Positions
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        // vertex normals
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        // vertex texture coords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        // vertex tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        // vertex bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
        // ids
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

        // weights
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
        glBindVertexArray(0);
    }
}
