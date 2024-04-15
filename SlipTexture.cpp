#include "SlipTexture.h"

#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

SlipTexture* SlipTexture::load(const char* image)
{
    SlipTexture* returned = new SlipTexture();

    glGenTextures(1, &returned->ID);

    returned->path = image;

    returned->imgData.data = stbi_load(returned->path.c_str(), &returned->width, &returned->height, &returned->nrComponents, STBI_rgb_alpha);

    if (returned->imgData.data)
    {
        if (returned->nrComponents == 1)
            returned->format = GL_RED;
        else if (returned->nrComponents == 3)
            returned->format = GL_RGB;
        else if (returned->nrComponents == 4)
            returned->format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, returned->ID);
        glTexImage2D(GL_TEXTURE_2D, 0, returned->format, returned->width, returned->height, 0, returned->format, GL_UNSIGNED_BYTE, returned->imgData.data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(returned->imgData.data);
    }
    else
    {
        printf("Texture failed to load at path: %s", image);
        stbi_image_free(returned->imgData.data);
    }

    return returned;
}

void SlipTexture::init()
{
    glGenTextures(1, &ID);

    unsigned char* data = stbi_load_from_memory(imgData.data, (int)imgData.size, &width, &height, &nrComponents, 3);

    if (data)
    {
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, ID);
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

void SlipTexture::draw()
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ID);
}
