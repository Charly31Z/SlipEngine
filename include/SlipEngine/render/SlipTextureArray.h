#pragma once

#ifndef SLIP_TEXTURE_ARRAY_H
#define SLIP_TEXTURE_ARRAY_H

#include "SlipEngine.h"
#include <string>
#include <unordered_map>
#include <cstdint>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif

class SLIPENGINE_API SlipTextureArray
{
public:
    SlipTextureArray(uint32_t width = 1024, uint32_t height = 1024, uint32_t maxLayers = 64);
    ~SlipTextureArray();

    bool Init();
    void Clean();

    // Registra una textura en el array. Retorna el índice del layer (0..maxLayers-1) o -1 si falla.
    int AddOrGetTextureLayer(const std::string& texturePath);

    void Bind(unsigned int unit = 0) const;
    void Unbind(unsigned int unit = 0) const;

    GLuint GetTextureId() const { return m_TextureId; }
    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    uint32_t GetLayerCount() const { return static_cast<uint32_t>(m_PathToLayer.size()); }

private:
    GLuint m_TextureId = 0;
    uint32_t m_Width = 1024;
    uint32_t m_Height = 1024;
    uint32_t m_MaxLayers = 64;
    std::unordered_map<std::string, int> m_PathToLayer;

    bool UploadLayer(int layer, const uint8_t* rgbaData);
};

#endif // !SLIP_TEXTURE_ARRAY_H

