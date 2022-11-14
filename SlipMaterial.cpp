#include "SlipMaterial.h"

/*#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"*/

SlipMaterial SlipMaterial::generateMaterial(std::string code, const char* path)
{
    std::string vertPath = code + ".vert";
    std::string fragPath = code + ".frag";
    SlipShader s{ vertPath.c_str(), fragPath.c_str() };

    return SlipMaterial(path);
}

SlipMaterial SlipMaterial::generateMaterial2(SlipShader shader, const char* path)
{
    return SlipMaterial(path);
}

/*void SlipMaterial::createTexture(const char* type)
{
    if (type == "diff")
    {
        std::string filename = std::string(diffuseTexture.path);

        glGenTextures(1, &diffuseTexture.id);

        int width, height, nrComponents;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
        if (data)
        {
            GLenum format;
            if (nrComponents == 1)
                format = GL_RED;
            else if (nrComponents == 3)
                format = GL_RGB;
            else if (nrComponents == 4)
                format = GL_RGBA;

            glBindTexture(GL_TEXTURE_2D, diffuseTexture.id);
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(data);
        }
        else
        {
            printf("Texture failed to load at path: %s", path);
            stbi_image_free(data);
        }
    }
}*/

void SlipMaterial::loadMatFromFile(std::string path)
{
    std::ifstream input(path + ".material_cache", std::ios::binary);

    float v3[3];

    IO::read(input, v3);
    color = glm::vec3(v3[0], v3[1], v3[2]);

    IO::read(input, v3);
    ambient = glm::vec3(v3[0], v3[1], v3[2]);

    IO::read(input, v3);
    specular = glm::vec3(v3[0], v3[1], v3[2]);

    IO::read(input, shaderType);

    IO::read(input, shininess);

    IO::read(input, diffPath);

    /*if (diffuseTexture.path)
    {
        createTexture("diff");
    }*/

    input.close();

    if (std::strlen(diffPath) != 0)
    {
        std::string pathtxt = std::string(diffPath);
        std::ifstream input(pathtxt + ".texture_cache", std::ios::binary);

        diffuseTexture = new SlipTexture();

        IO::read(input, diffuseTexture->width);
        IO::read(input, diffuseTexture->height);
        IO::read(input, diffuseTexture->nrComponents);
        IO::read(input, diffuseTexture->format);

        IO::read(input, diffuseTexture->imgData.size);

        diffuseTexture->imgData.data = new unsigned char[diffuseTexture->imgData.size];
        input.read((char*)&diffuseTexture->imgData.data[0], diffuseTexture->imgData.size);

        input.close();

        diffuseTexture->init();
    }
}

SlipMaterial::SlipMaterial(std::string path) : path(path)
{
    loadMatFromFile(path);

    std::string sType = std::string(shaderType);
    std::string vertPath = std::string("assets/shaders/" + sType + ".vert");
    std::string fragPath = std::string("assets/shaders/" + sType + ".frag");

    shader = new SlipShader(vertPath.c_str(), fragPath.c_str());
}

void SlipMaterial::bind(glm::mat4 &model)
{
    shader->use();

    shader->setVec3("material.ambient", ambient);
    shader->setVec3("material.diffuse", color);
    shader->setVec3("material.specular", specular);
    shader->setFloat("material.shininess", shininess);

    /*shader.setVec3("sunLight.direction", sunLight.position);
    shader.setVec3("sunLight.ambient", sunLight.ambient);
    shader.setVec3("sunLight.diffuse", sunLight.diffuse);
    shader.setVec3("sunLight.specular", sunLight.diffuse);

    for (int i = 0; i < lights.size(); i++)
    {
        shader.setVec3("lights["+ std::to_string(i) + "].position", lights[i].position);
        shader.setVec3("lights[" + std::to_string(i) + "].ambient", lights[i].ambient);
        shader.setVec3("lights[" + std::to_string(i) + "].diffuse", lights[i].diffuse);
        shader.setVec3("lights[" + std::to_string(i) + "].specular", lights[i].diffuse);
        shader.setFloat("lights[" + std::to_string(i) + "].constant", 1.0f);
        shader.setFloat("lights[" + std::to_string(i) + "].linear", 0.09f);
        shader.setFloat("lights[" + std::to_string(i) + "].quadratic", 0.032f);
    }*/

    shader->setVec3("viewPos", SlipLevel::Camera.Position);

    glm::mat4 proj = SlipLevel::Camera.GetProjectionMatrix();
    glm::mat4 view = SlipLevel::Camera.GetViewMatrix();

    shader->setMat4("projection", proj);
    shader->setMat4("view", view);
    shader->setMat4("model", model);

    // bind appropriate textures
    if (std::strlen(diffPath) != 0)
    {
        shader->setInt("texture_diffuse", 0);
        glActiveTexture(GL_TEXTURE0);
        diffuseTexture->draw();
    }
}
