#include "SlipTextureArray.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "stb_image.h"
#include <algorithm>
#include <vector>

SlipTextureArray::SlipTextureArray(uint32_t width, uint32_t height, uint32_t maxLayers)
    : m_Width(width), m_Height(height), m_MaxLayers(maxLayers)
{
}

SlipTextureArray::~SlipTextureArray()
{
    Clean();
}

bool SlipTextureArray::Init()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) return false;
#endif
    if (m_TextureId != 0) return true;

    glGenTextures(1, &m_TextureId);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureId);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

#if defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
    float maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 1.0f) {
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAniso, 8.0f));
    }
#endif

    // Reservar espacio para m_MaxLayers slices
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, m_Width, m_Height, m_MaxLayers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    return true;
}

void SlipTextureArray::Clean()
{
    if (m_TextureId != 0) {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
        if (glad_glDeleteTextures)
#endif
        glDeleteTextures(1, &m_TextureId);
        m_TextureId = 0;
    }
    m_PathToLayer.clear();
}

static void ResizeRGBA(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH)
{
    for (int y = 0; y < dstH; ++y) {
        int srcY = (y * srcH) / dstH;
        for (int x = 0; x < dstW; ++x) {
            int srcX = (x * srcW) / dstW;
            const uint8_t* srcPx = src + (srcY * srcW + srcX) * 4;
            uint8_t* dstPx = dst + (y * dstW + x) * 4;
            dstPx[0] = srcPx[0];
            dstPx[1] = srcPx[1];
            dstPx[2] = srcPx[2];
            dstPx[3] = srcPx[3];
        }
    }
}

int SlipTextureArray::AddOrGetTextureLayer(const std::string& texturePath)
{
    if (texturePath.empty()) return -1;

    std::string norm = SlipFileSystem::NormalizePath(texturePath);
    auto it = m_PathToLayer.find(norm);
    if (it != m_PathToLayer.end()) {
        return it->second;
    }

    if (m_PathToLayer.size() >= m_MaxLayers) {
        SlipConsole::LogWarning("SlipTextureArray: Capacidad máxima de capas alcanzada (" + std::to_string(m_MaxLayers) + ")");
        return -1;
    }

    if (!Init()) return -1;

    std::string resolvedPath = norm;
    if (!SlipFileSystem::Exists(resolvedPath)) {
        std::string assets = SlipFileSystem::GetProjectAssetsPath();
        if (!assets.empty()) {
            std::string candidate = SlipFileSystem::CombinePath(assets, norm);
            if (SlipFileSystem::Exists(candidate)) resolvedPath = candidate;
        }
    }

    int w = 0, h = 0, comp = 0;
    uint8_t* imgData = nullptr;

    std::vector<uint8_t> fileBytes;
    if (SlipFileSystem::ReadAllBytes(resolvedPath, fileBytes) && !fileBytes.empty()) {
        imgData = stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &w, &h, &comp, 4);
    } else {
        imgData = stbi_load(resolvedPath.c_str(), &w, &h, &comp, 4);
    }

    if (!imgData) {
        return -1;
    }

    int newLayerIdx = static_cast<int>(m_PathToLayer.size());
    std::vector<uint8_t> resizedBuffer;

    const uint8_t* uploadPtr = imgData;
    if (w != static_cast<int>(m_Width) || h != static_cast<int>(m_Height)) {
        resizedBuffer.resize(m_Width * m_Height * 4);
        ResizeRGBA(imgData, w, h, resizedBuffer.data(), m_Width, m_Height);
        uploadPtr = resizedBuffer.data();
    }

    bool success = UploadLayer(newLayerIdx, uploadPtr);
    stbi_image_free(imgData);

    if (success) {
        m_PathToLayer[norm] = newLayerIdx;
        return newLayerIdx;
    }

    return -1;
}

bool SlipTextureArray::UploadLayer(int layer, const uint8_t* rgbaData)
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glBindTexture) return false;
#endif
    if (m_TextureId == 0 || !rgbaData || layer < 0 || layer >= static_cast<int>(m_MaxLayers)) {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureId);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, m_Width, m_Height, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgbaData);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    return true;
}

void SlipTextureArray::Bind(unsigned int unit) const
{
    if (m_TextureId == 0) return;
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glActiveTexture || !glad_glBindTexture) return;
#endif
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_TextureId);
}

void SlipTextureArray::Unbind(unsigned int unit) const
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glActiveTexture || !glad_glBindTexture) return;
#endif
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

