#include "SlipUI.h"

#include "stb_image.h"

void SlipUI::setupMesh()
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    // load data into vertex buffers
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    // A great thing about structs is that their memory layout is sequential for all its items.
    // The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
    // again translates to 3/2 floats which translates to a byte array.
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    // set the vertex attribute pointers
    // vertex Positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    // vertex texture coords
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SlipUI::setupTexture()
{
    glGenTextures(1, &texture.id);

    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(texture.path.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        printf("Loaded texture: %s", texture.path.c_str());

        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, texture.id);
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
        printf("Texture failed to load at path: %s", texture.path.c_str());
        stbi_image_free(data);
    }

    shader.use();

    shader.setInt("texture1", 0);
}

SlipUI::SlipUI(glm::vec2 position, std::string texturePath)
{
	vertices = {
		Vertex{glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)},
		Vertex{glm::vec3( 0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f)},
		Vertex{glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)},
		Vertex{glm::vec3(-0.5f,  0.5f, 0.0f), glm::vec2(0.0f, 1.0f)}
	};

	indices = {
		0, 1, 3,
        1, 2, 3
	};

    texture.path = texturePath;

    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(position, 0.0f));

	setupMesh();
    setupTexture();
}

void SlipUI::draw(glm::mat4 ortho)
{
    shader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);

    mvp = ortho * model;
    shader.setMat4("mvp", mvp);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
