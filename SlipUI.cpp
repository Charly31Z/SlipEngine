#include "SlipUI.h"

#include "SlipWindow.h"

#include "stb_image.h"
#include "stb_image_write.h"

#include "SlipShadows.h"

#include "SlipGlobals.h"
#include <iostream>

glm::mat4 SlipUIImage::getModelMatrix()
{
    float aspect = (float)(SlipWindow::Get().getWidth() / SlipWindow::Get().getHeight());

    float xPos = ((SlipWindow::Get().getWidth() * position.x) / (SlipWindow::Get().getWidth() / aspect));
    float yPos = ((SlipWindow::Get().getHeight() * position.y) / (SlipWindow::Get().getWidth() / aspect));

    float xSca = ((SlipWindow::Get().getHeight() * scale.x) / (SlipWindow::Get().getWidth() / (aspect / 2)));
    float ySca = ((SlipWindow::Get().getHeight() * scale.y) / (SlipWindow::Get().getWidth() / (aspect / 2)));

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(xPos, yPos, 0.0f));
    model = glm::scale(model, glm::vec3(xSca, ySca, 0.0f));

    return model;
}

void SlipUIImage::setupMesh()
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

void SlipUIImage::setupTexture()
{
    glGenTextures(1, &texture.id);

    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(texture.path.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        printf("Loaded texture: %s\n", texture.path.c_str());

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
        printf("Texture failed to load at path: %s\n", texture.path.c_str());
        stbi_image_free(data);
    }
}

SlipUIImage& SlipUI::image(std::string name, std::string texturePath, glm::vec2 position)
{
    SlipUIImage* img = new SlipUIImage();

    img->position = position;

    img->shader = new SlipShader("assets/shaders/ui.vert", "assets/shaders/ui.frag");

    img->vertices = {
        SlipUIImage::Vertex{glm::vec3( 1.f,  1.f, 0.0f), glm::vec2(1.0f, 1.0f)},
        SlipUIImage::Vertex{glm::vec3( 1.f, -1.f, 0.0f), glm::vec2(1.0f, 0.0f)},
        SlipUIImage::Vertex{glm::vec3(-1.f, -1.f, 0.0f), glm::vec2(0.0f, 0.0f)},
        SlipUIImage::Vertex{glm::vec3(-1.f,  1.f, 0.0f), glm::vec2(0.0f, 1.0f)}
	};

    img->indices = {
		0, 1, 3,
        1, 2, 3
	};

    img->texture.path = texturePath;

    img->setupMesh();
    img->setupTexture();

    elements.insert({ name, img });

    return *(SlipUIImage*)elements.rbegin()->second;
}

SlipUIImage& SlipUI::image(std::string name, unsigned int texture, glm::vec2 position)
{
    SlipUIImage* img = new SlipUIImage();

    img->position = position;

    img->shader = new SlipShader("assets/shaders/ui.vert", "assets/shaders/ui.frag");

    img->vertices = {
        SlipUIImage::Vertex{glm::vec3(0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)},
        SlipUIImage::Vertex{glm::vec3(0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f)},
        SlipUIImage::Vertex{glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)},
        SlipUIImage::Vertex{glm::vec3(-0.5f,  0.5f, 0.0f), glm::vec2(0.0f, 1.0f)}
    };

    img->indices = {
        0, 1, 3,
        1, 2, 3
    };

    img->texture.id = texture;

    img->setupMesh();

    elements.insert({ name, img });

    return *(SlipUIImage*)elements.rbegin()->second;
}

SlipUIText* SlipUI::text(std::string name, std::string text, glm::vec2 position)
{
    // TODO: insert return statement here
    SlipUIText* txt = new SlipUIText();

    txt->position = position;
    txt->text = text;

    elements.insert({ name, txt });

    return (SlipUIText*)elements[name];
}

void SlipUI::draw()
{
    for (auto const& [key, val] : elements)
    {
        val->draw();
    }
}

void SlipUIImage::draw()
{
    shader->use();

    shader->setInt("texture1", 0);
    shader->setVec3("color", color);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);

    glm::mat4 mvp = SlipGlobals::Get().getCamera().GetOrthographicMatrix() * getModelMatrix();
    shader->setMat4("mvp", mvp);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
}

SlipUI::SlipUI()
{
    assert(!m_Instance && "Slip UI has initialized...");
    m_Instance = this;
}

SlipUIInterface& SlipUI::Find(std::string name)
{
    // TODO: insert return statement here
    for (auto const& [key, val] : elements)
    {
        if (key == name)
        {
            return *val;
        }
    }
}

glm::mat4 SlipUIText::getModelMatrix()
{
    float aspect = (float)(SlipWindow::Get().getWidth() / SlipWindow::Get().getHeight());

    float xPos = ((SlipWindow::Get().getWidth() * position.x) / (SlipWindow::Get().getWidth() / aspect));
    float yPos = ((SlipWindow::Get().getHeight() * position.y) / (SlipWindow::Get().getWidth() / aspect));

    float xSca = ((SlipWindow::Get().getHeight() * scale.x) / (SlipWindow::Get().getWidth() / (aspect / 2)));
    float ySca = ((SlipWindow::Get().getHeight() * scale.y) / (SlipWindow::Get().getWidth() / (aspect / 2)));

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(xPos, yPos, 0.0f));
    model = glm::scale(model, glm::vec3(xSca, ySca, 0.0f));

    return model;
}

void SlipUIText::setupMesh()
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    // load data into vertex buffers
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    // A great thing about structs is that their memory layout is sequential for all its items.
    // The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
    // again translates to 3/2 floats which translates to a byte array.
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);

    // set the vertex attribute pointers
    // vertex Positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

SlipUIText::SlipUIText()
{
#ifndef SLIP_INSPECTOR
    if (FT_Init_FreeType(&ft))
    {
        std::cout << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
    }

    // load font as face
    FT_Face face;
    if (FT_New_Face(ft, "C:/Windows/Fonts/Arial.ttf", 0, &face)) {
        std::cout << "ERROR::FREETYPE: Failed to load font" << std::endl;
    }
    else {
        // set size to load glyphs as
        FT_Set_Pixel_Sizes(face, 0, 48);

        // disable byte-alignment restriction
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // load first 128 characters of ASCII set
        for (unsigned char c = 0; c < 128; c++)
        {
            // Load character glyph 
            if (FT_Load_Char(face, c, FT_LOAD_RENDER))
            {
                std::cout << "ERROR::FREETYTPE: Failed to load Glyph" << std::endl;
                continue;
            }
            // generate texture
            unsigned int texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RED,
                face->glyph->bitmap.width,
                face->glyph->bitmap.rows,
                0,
                GL_RED,
                GL_UNSIGNED_BYTE,
                face->glyph->bitmap.buffer
            );
            // set texture options
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            // now store character for later use
            Character character = {
                texture,
                glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
                glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
                static_cast<unsigned int>(face->glyph->advance.x)
            };
            Characters.insert(std::pair<char, Character>(c, character));
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    // destroy FreeType once we're finished
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    shader = new SlipShader("assets/shaders/uiText.vert", "assets/shaders/uiText.frag");
    setupMesh();
#endif // !SLIP_INSPECTOR
}

void SlipUIText::draw()
{
    // activate corresponding render state	
    shader->use();

    shader->setInt("texture1", 0);
    shader->setVec3("color", color);

    glm::mat4 mvp = glm::ortho(0.0f, (float)SlipWindow::Get().getWidth(), 0.0f, (float)SlipWindow::Get().getHeight());
    shader->setMat4("mvp", mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(VAO);

    float x = position.x;
    float y = position.y;

    float s = 1.f;

    // iterate through all characters
    std::string::const_iterator c;
    for (c = text.begin(); c != text.end(); c++)
    {
        Character ch = Characters[*c];

        float xpos = x + ch.Bearing.x * s;
        float ypos = y - (ch.Size.y - ch.Bearing.y) * s;

        float w = ch.Size.x * s;
        float h = ch.Size.y * s;
        // update VBO for each character
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };
        // render glyph texture over quad
        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        // update content of VBO memory
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); // be sure to use glBufferSubData and not glBufferData

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        // render quad
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // now advance cursors for next glyph (note that advance is number of 1/64 pixels)
        x += (ch.Advance >> 6) * s; // bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
