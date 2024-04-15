#include "SlipTerrain.h"

#include <glad/glad.h>

#include "SlipGlobals.h"

#include "stb_image.h"

SlipTerrain::SlipTerrain()
{
    shader = new SlipShader("assets/shaders/height.vert", "assets/shaders/height.frag", "assets/shaders/height.tssc", "assets/shaders/height.tsse");
}

SlipTerrain::~SlipTerrain()
{
}

void SlipTerrain::apply()
{
    if (initialized)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object

        unsigned char* data = stbi_load(heightfieldIMG,
            &width, &height, &nChannels,
            0);

        if (data)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            shader->setInt("heightMap", 0);
            printf("Loaded heightmap of size %d x %d", height, width);
        }

        vertices.clear();

        rez = 20;
        for (unsigned i = 0; i <= rez - 1; i++)
        {
            for (unsigned j = 0; j <= rez - 1; j++)
            {
                //Terrain
                vertices.push_back(-width / 2.0f + width * i / (float)rez); // v.x
                vertices.push_back(0.0f); // v.y
                vertices.push_back(-height / 2.0f + height * j / (float)rez); // v.z
                vertices.push_back(i / (float)rez); // u
                vertices.push_back(j / (float)rez); // v

                vertices.push_back(-width / 2.0f + width * (i + 1) / (float)rez); // v.x
                vertices.push_back(0.0f); // v.y
                vertices.push_back(-height / 2.0f + height * j / (float)rez); // v.z
                vertices.push_back((i + 1) / (float)rez); // u
                vertices.push_back(j / (float)rez); // v

                vertices.push_back(-width / 2.0f + width * i / (float)rez); // v.x
                vertices.push_back(0.0f); // v.y
                vertices.push_back(-height / 2.0f + height * (j + 1) / (float)rez); // v.z
                vertices.push_back(i / (float)rez); // u
                vertices.push_back((j + 1) / (float)rez); // v

                vertices.push_back(-width / 2.0f + width * (i + 1) / (float)rez); // v.x
                vertices.push_back(0.0f); // v.y
                vertices.push_back(-height / 2.0f + height * (j + 1) / (float)rez); // v.z
                vertices.push_back((i + 1) / (float)rez); // u
                vertices.push_back((j + 1) / (float)rez); // v
            }
        }

        stbi_image_free(data);

        collision = new SlipCollision(collisionPath);
        collision->init();
        collision->initRigidBody(glm::vec3(0.f), glm::vec3(0.f));
    }
    else {
        init();
    }
}

void SlipTerrain::init()
{
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
    // set the texture wrapping parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// set texture wrapping to GL_REPEAT (default wrapping method)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char* data = stbi_load(heightfieldIMG,
        &width, &height, &nChannels,
        0);

    if (data)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        shader->setInt("heightMap", 0);
        printf("Loaded heightmap of size %d x %d", height, width);
    }

    rez = 20;
    for (unsigned i = 0; i <= rez - 1; i++)
    {
        for (unsigned j = 0; j <= rez - 1; j++)
        {
            //Terrain
            vertices.push_back(-width / 2.0f + width * i / (float)rez); // v.x
            vertices.push_back(0.0f); // v.y
            vertices.push_back(-height / 2.0f + height * j / (float)rez); // v.z
            vertices.push_back(i / (float)rez); // u
            vertices.push_back(j / (float)rez); // v

            vertices.push_back(-width / 2.0f + width * (i + 1) / (float)rez); // v.x
            vertices.push_back(0.0f); // v.y
            vertices.push_back(-height / 2.0f + height * j / (float)rez); // v.z
            vertices.push_back((i + 1) / (float)rez); // u
            vertices.push_back(j / (float)rez); // v

            vertices.push_back(-width / 2.0f + width * i / (float)rez); // v.x
            vertices.push_back(0.0f); // v.y
            vertices.push_back(-height / 2.0f + height * (j + 1) / (float)rez); // v.z
            vertices.push_back(i / (float)rez); // u
            vertices.push_back((j + 1) / (float)rez); // v

            vertices.push_back(-width / 2.0f + width * (i + 1) / (float)rez); // v.x
            vertices.push_back(0.0f); // v.y
            vertices.push_back(-height / 2.0f + height * (j + 1) / (float)rez); // v.z
            vertices.push_back((i + 1) / (float)rez); // u
            vertices.push_back((j + 1) / (float)rez); // v
        }
    }

    stbi_image_free(data);

    glGenVertexArrays(1, &terrainVAO);
    glBindVertexArray(terrainVAO);

    glGenBuffers(1, &terrainVBO);
    glBindBuffer(GL_ARRAY_BUFFER, terrainVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * vertices.size(), &vertices[0], GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // texCoord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(sizeof(float) * 3));
    glEnableVertexAttribArray(1);

    glPatchParameteri(GL_PATCH_VERTICES, NUM_PATCH_PTS);

    collision = new SlipCollision(collisionPath);
    collision->init();
    collision->initRigidBody(glm::vec3(0.f, 15.f, 0.f), glm::vec3(0.f));

    initialized = true;
}

void SlipTerrain::draw()
{
    glDisable(GL_CULL_FACE);

    shader->use();

    glm::mat4 proj = SlipGlobals::Get().getProjection();
    glm::mat4 view = SlipGlobals::Get().getView();

    shader->setMat4("projection", proj);
    shader->setMat4("view", view);

    // world transformation
    glm::mat4 model = glm::mat4(1.0f);
    shader->setMat4("model", model);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(terrainVAO);
    glDrawArrays(GL_PATCHES, 0, NUM_PATCH_PTS * rez * rez);

    glEnable(GL_CULL_FACE);
}

void SlipTerrain::clean()
{
    glDeleteVertexArrays(1, &terrainVAO);
    glDeleteBuffers(1, &terrainVBO);
    glDeleteTextures(1, &texture);
}
