#include "SlipTexture.h"

#include "SlipGL.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static std::unordered_map<std::string, SlipTexture*> s_TextureCache;

static std::string ResolveTexturePath(const std::string& rawPath)
{
    if (rawPath.empty()) return "";

    std::string norm = rawPath;
    std::replace(norm.begin(), norm.end(), '\\', '/');

    if (SlipFileSystem::Exists(norm)) return norm;

    std::string assets = SlipFileSystem::GetProjectAssetsPath();
    std::string root = SlipFileSystem::GetProjectRootPath();

    std::vector<std::string> candidates;
    if (!assets.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(assets, norm));
        if (norm.rfind("assets/", 0) == 0) {
            candidates.push_back(SlipFileSystem::CombinePath(assets, norm.substr(7)));
        }
    }
    if (!root.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(root, norm));
        candidates.push_back(SlipFileSystem::CombinePath(root, "assets/" + norm));
    }
    candidates.push_back("assets/" + norm);

    for (const auto& c : candidates) {
        if (SlipFileSystem::Exists(c)) {
            std::string found = c;
            std::replace(found.begin(), found.end(), '\\', '/');
            return found;
        }
    }

    return norm;
}

void SlipTexture::clearCache()
{
    for (auto& kv : s_TextureCache) {
        if (kv.second) {
            if (kv.second->ID != 0
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                && glad_glDeleteTextures
#endif
            ) {
                glDeleteTextures(1, &kv.second->ID);
            }
            delete kv.second;
        }
    }
    s_TextureCache.clear();
}

SlipTexture* SlipTexture::findInCache(const char* image)
{
    if (!image || std::strlen(image) == 0) return nullptr;
    std::string resolvedPath = ResolveTexturePath(image);
    if (resolvedPath.empty()) return nullptr;

    auto it = s_TextureCache.find(resolvedPath);
    if (it != s_TextureCache.end() && it->second != nullptr) {
        if (it->second->ID != 0) {
            return it->second;
        }
    }
    return nullptr;
}

SlipTexture* SlipTexture::reload(const char* image)
{
    if (!image || std::strlen(image) == 0) return nullptr;
    std::string resolvedPath = ResolveTexturePath(image);
    if (resolvedPath.empty()) return nullptr;

    auto it = s_TextureCache.find(resolvedPath);
    if (it != s_TextureCache.end() && it->second != nullptr) {
        if (it->second->ID != 0) {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
            if (glad_glDeleteTextures)
#endif
            glDeleteTextures(1, &it->second->ID);
            it->second->ID = 0;
        }
    }
    return load(image);
}

SlipTexture* SlipTexture::load(const char* image)
{
    if (!image || std::strlen(image) == 0) return nullptr;

    std::string resolvedPath = ResolveTexturePath(image);
    if (resolvedPath.empty()) return nullptr;

    auto it = s_TextureCache.find(resolvedPath);
    if (it != s_TextureCache.end() && it->second != nullptr) {
        if (it->second->ID != 0) {
            return it->second;
        }
    }

    SlipTexture* returned = (it != s_TextureCache.end() && it->second != nullptr) ? it->second : new SlipTexture();
    returned->ID = 0;
    returned->width = 0;
    returned->height = 0;
    returned->nrComponents = 0;
    returned->format = GL_RGBA;
    returned->path = resolvedPath;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) {
        s_TextureCache[resolvedPath] = returned;
        return returned;
    }
#endif

    std::vector<uint8_t> bytes;
    unsigned char* data = nullptr;

    if (SlipFileSystem::ReadAllBytes(resolvedPath, bytes) && !bytes.empty()) {
        data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &returned->width, &returned->height, &returned->nrComponents, STBI_rgb_alpha);
    } else {
        data = stbi_load(resolvedPath.c_str(), &returned->width, &returned->height, &returned->nrComponents, STBI_rgb_alpha);
    }

    if (data)
    {
        returned->format = GL_RGBA;

        // Calcular color promedio representativo y detectar transparencia real
        if (returned->width > 0 && returned->height > 0) {
            int stepX = std::max(1, returned->width / 32);
            int stepY = std::max(1, returned->height / 32);
            glm::vec3 sumColor(0.0f);
            int sampleCount = 0;
            int transparentSamples = 0;
            int totalAlphaSamples = 0;

            for (int y = 0; y < returned->height; y += stepY) {
                for (int x = 0; x < returned->width; x += stepX) {
                    size_t idx = (static_cast<size_t>(y) * returned->width + x) * 4;
                    unsigned char r = data[idx + 0];
                    unsigned char g = data[idx + 1];
                    unsigned char b = data[idx + 2];
                    unsigned char a = data[idx + 3];
                    totalAlphaSamples++;
                    if (a < 200) {
                        transparentSamples++;
                    }
                    if (a > 16) {
                        sumColor += glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
                        sampleCount++;
                    }
                }
            }
            if (sampleCount > 0) {
                returned->averageColor = sumColor / static_cast<float>(sampleCount);
            }
            if (returned->nrComponents == 4 && totalAlphaSamples > 0 && transparentSamples > (totalAlphaSamples / 50)) {
                returned->hasTransparency = true;
            }
        }

        glGenTextures(1, &returned->ID);
        glBindTexture(GL_TEXTURE_2D, returned->ID);
        glTexImage2D(GL_TEXTURE_2D, 0, returned->format, returned->width, returned->height, 0, returned->format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#if defined(GL_TEXTURE_MAX_ANISOTROPY)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
#elif defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
#endif

        stbi_image_free(data);
        s_TextureCache[resolvedPath] = returned;
    }
    else
    {
        printf("Texture failed to load at path: %s\n", image);
    }

    return returned;
}

void SlipTexture::init()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) return;
#endif
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
#if defined(GL_TEXTURE_MAX_ANISOTROPY)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
#elif defined(GL_TEXTURE_MAX_ANISOTROPY_EXT)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
#endif

        stbi_image_free(data);
    }
    else
    {
        printf("Texture failed to load at path: %s", path.c_str());
        stbi_image_free(data);
    }
}

void SlipTexture::bind(unsigned int unit)
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glActiveTexture || !glad_glBindTexture) return;
#endif
    if (ID == 0 && !path.empty()) {
        // Intentar carga diferida si se creó antes de inicializar el contexto GL
        SlipTexture* loaded = SlipTexture::load(path.c_str());
        if (loaded && loaded != this) {
            ID = loaded->ID;
            width = loaded->width;
            height = loaded->height;
            nrComponents = loaded->nrComponents;
            averageColor = loaded->averageColor;
            hasTransparency = loaded->hasTransparency;
        }
    }
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, ID);
}

void SlipTexture::draw()
{
    bind(0);
}

void SlipTexture::SetFilterMode(bool pointFilter)
{
    if (ID == 0) return;
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glBindTexture) return;
#endif
    glBindTexture(GL_TEXTURE_2D, ID);
    if (pointFilter) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}
