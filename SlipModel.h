#pragma once

#ifndef SLIP_MODEL_H
#define SLIP_MODEL_H

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <stdio.h>
#include <stdint.h>
#include <vector>
#include "SlipShader.h"
#include "SlipMesh.h"

class SlipModel
{
private:

	GLuint VAO, VBO, EBO;

    void loadModel(std::string const& filepath);
    void processNode(aiNode* node, const aiScene* scene);
    SlipMesh processMesh(aiMesh* mesh, const aiScene* scene);
    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName);
    unsigned int TextureFromFile(const char* path, const std::string& directory, bool gamma);
public:
    std::vector<Texture> textures_loaded;	// stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
    std::vector<SlipMesh> meshes;
    std::string directory;
    bool gammaCorrection;

    // constructor, expects a filepath to a 3D model.
    SlipModel(std::string const& filepath, bool gamma = false);

    // draws the model, and thus all its meshes
    void Draw(SlipShader& shader);
};

#endif // !SLIP_MODEL_H