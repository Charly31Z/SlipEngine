#include "SlipEngine/editor/SlipThumbnailManager.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include <SDL2/SDL.h>

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace {

// Lienzo de píxeles simple para generación procedural de iconos nítidos (128x128 RGBA)
struct Canvas128 {
    static const int W = 128;
    static const int H = 128;
    unsigned char data[W * H * 4];

    Canvas128() {
        std::memset(data, 0, sizeof(data));
    }

    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        int idx = (y * W + x) * 4;
        if (a == 255) {
            data[idx] = r;
            data[idx + 1] = g;
            data[idx + 2] = b;
            data[idx + 3] = a;
        } else if (a > 0) {
            float alpha = a / 255.0f;
            float inv = 1.0f - alpha;
            data[idx] = (uint8_t)(r * alpha + data[idx] * inv);
            data[idx + 1] = (uint8_t)(g * alpha + data[idx + 1] * inv);
            data[idx + 2] = (uint8_t)(b * alpha + data[idx + 2] * inv);
            data[idx + 3] = (uint8_t)(std::min)(255.0f, data[idx + 3] + a * inv);
        }
    }

    void FillRect(int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, int cornerRadius = 0) {
        for (int y = ry; y < ry + rh; ++y) {
            for (int x = rx; x < rx + rw; ++x) {
                if (cornerRadius > 0) {
                    int dx = 0, dy = 0;
                    if (x < rx + cornerRadius) dx = (rx + cornerRadius) - x;
                    else if (x >= rx + rw - cornerRadius) dx = x - (rx + rw - cornerRadius - 1);
                    if (y < ry + cornerRadius) dy = (ry + cornerRadius) - y;
                    else if (y >= ry + rh - cornerRadius) dy = y - (ry + rh - cornerRadius - 1);
                    if (dx > 0 && dy > 0 && (dx * dx + dy * dy > cornerRadius * cornerRadius)) {
                        continue;
                    }
                }
                SetPixel(x, y, r, g, b, a);
            }
        }
    }

    void DrawLine(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, int thickness = 1) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        int half = thickness / 2;
        while (true) {
            for (int ty = -half; ty <= half; ++ty) {
                for (int tx = -half; tx <= half; ++tx) {
                    SetPixel(x0 + tx, y0 + ty, r, g, b, a);
                }
            }
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void FillCircle(int cx, int cy, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                int dx = x - cx;
                int dy = y - cy;
                if (dx * dx + dy * dy <= radius * radius) {
                    SetPixel(x, y, r, g, b, a);
                }
            }
        }
    }

    void FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        int minX = (std::min)({ x0, x1, x2 });
        int maxX = (std::max)({ x0, x1, x2 });
        int minY = (std::min)({ y0, y1, y2 });
        int maxY = (std::max)({ y0, y1, y2 });
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
                int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
                int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
                if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                    SetPixel(x, y, r, g, b, a);
                }
            }
        }
    }
};

} // namespace

SlipThumbnailManager& SlipThumbnailManager::Get() {
    static SlipThumbnailManager s_Instance;
    return s_Instance;
}

SlipThumbnailManager::~SlipThumbnailManager() {
    Shutdown();
}

void SlipThumbnailManager::Init() {
    if (m_Initialized) return;
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) return;
    GenerateBuiltinIcons();
    m_Initialized = true;
}

void SlipThumbnailManager::Shutdown() {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) {
        m_LoadedTextures.clear();
        m_Initialized = false;
        return;
    }
    ClearTextureCache();

    if (m_FolderIcon) { glDeleteTextures(1, &m_FolderIcon); m_FolderIcon = 0; }
    if (m_ScriptIcon) { glDeleteTextures(1, &m_ScriptIcon); m_ScriptIcon = 0; }
    if (m_AnimationIcon) { glDeleteTextures(1, &m_AnimationIcon); m_AnimationIcon = 0; }
    if (m_ShaderGraphIcon) { glDeleteTextures(1, &m_ShaderGraphIcon); m_ShaderGraphIcon = 0; }
    if (m_PrefabIcon) { glDeleteTextures(1, &m_PrefabIcon); m_PrefabIcon = 0; }
    if (m_SceneIcon) { glDeleteTextures(1, &m_SceneIcon); m_SceneIcon = 0; }
    if (m_ControllerIcon) { glDeleteTextures(1, &m_ControllerIcon); m_ControllerIcon = 0; }
    if (m_AudioIcon) { glDeleteTextures(1, &m_AudioIcon); m_AudioIcon = 0; }
    if (m_FileIcon) { glDeleteTextures(1, &m_FileIcon); m_FileIcon = 0; }

    if (m_SphereVAO) {
        glDeleteVertexArrays(1, &m_SphereVAO);
        glDeleteBuffers(1, &m_SphereVBO);
        glDeleteBuffers(1, &m_SphereEBO);
        m_SphereVAO = 0;
        m_SphereVBO = 0;
        m_SphereEBO = 0;
        m_SphereInitialized = false;
    }

    if (m_FBO) {
        glDeleteFramebuffers(1, &m_FBO);
        glDeleteTextures(1, &m_ColorTex);
        glDeleteRenderbuffers(1, &m_DepthRBO);
        m_FBO = 0;
        m_ColorTex = 0;
        m_DepthRBO = 0;
    }

    m_Initialized = false;
}

std::string SlipThumbnailManager::GetCacheDirectory() {
    std::string cacheDir = "cache";
    if (SlipProject::Get().HasActiveProject()) {
        std::string root = SlipProject::Get().GetCurrentProject().rootPath;
        if (!root.empty()) {
            cacheDir = SlipFileSystem::CombinePath(root, "cache");
        }
    }
    std::string thumbDir = SlipFileSystem::CombinePath(cacheDir, "thumbnails");
    SlipFileSystem::CreateDirectoryPath(thumbDir);
    return thumbDir;
}

std::string SlipThumbnailManager::GetThumbnailPngPath(const std::string& assetPath) {
    std::string guid = SlipModelExtract::GetGuidForAsset(assetPath);
    std::string key = guid;
    if (key.empty()) {
        std::hash<std::string> hasher;
        size_t h = hasher(SlipFileSystem::NormalizePath(assetPath));
        std::stringstream ss;
        ss << "th_" << std::hex << h;
        key = ss.str();
    }
    return SlipFileSystem::CombinePath(GetCacheDirectory(), key + ".png");
}

void SlipThumbnailManager::ClearTextureCache() {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) {
        m_LoadedTextures.clear();
        return;
    }
    for (auto& pair : m_LoadedTextures) {
        if (pair.second != 0) {
            glDeleteTextures(1, &pair.second);
        }
    }
    m_LoadedTextures.clear();
}

void SlipThumbnailManager::InvalidateThumbnail(const std::string& assetPath) {
    auto it = m_LoadedTextures.find(assetPath);
    if (it != m_LoadedTextures.end()) {
        if (it->second != 0 && SlipRHI::GetActiveAPI() == GraphicsAPI::OpenGL) {
            glDeleteTextures(1, &it->second);
        }
        m_LoadedTextures.erase(it);
    }
}

unsigned int SlipThumbnailManager::CreateTextureFromRGBA(const unsigned char* data, int width, int height) {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) return 0;
    if (!data || width <= 0 || height <= 0) return 0;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int SlipThumbnailManager::LoadTextureFromFile(const std::string& pngPath) {
    if (pngPath.empty() || !SlipFileSystem::Exists(pngPath)) return 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load(pngPath.c_str(), &w, &h, &comp, 4);
    if (!pixels) return 0;
    unsigned int tex = CreateTextureFromRGBA(pixels, w, h);
    stbi_image_free(pixels);
    return tex;
}

void SlipThumbnailManager::GenerateBuiltinIcons() {
    // 1. Icono de Carpeta (Dorado/Ámbar elegante con solapa)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 26, 28, 34, 255, 14); // Fondo contenedor
        c.FillRect(22, 28, 36, 18, 217, 119, 6, 255, 6);   // Pestaña superior
        c.FillRect(22, 38, 84, 60, 245, 158, 11, 255, 6);  // Cuerpo posterior
        c.FillRect(22, 48, 84, 50, 251, 191, 36, 255, 6);  // Solapa frontal
        c.DrawLine(26, 52, 102, 52, 254, 243, 199, 220, 2); // Resalte brillo
        m_FolderIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 2. Icono de Script Lua (.lua) (Azul oscuro / Cian con corchetes de código)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 20, 26, 42, 255, 14);
        c.FillRect(26, 20, 76, 88, 15, 23, 42, 255, 8);    // Hoja
        c.FillRect(26, 20, 76, 18, 2, 132, 199, 255, 6);   // Barra de cabecera azul
        // Símbolos de código < / >
        c.DrawLine(40, 52, 32, 60, 56, 189, 248, 255, 3);
        c.DrawLine(32, 60, 40, 68, 56, 189, 248, 255, 3);
        c.DrawLine(46, 70, 54, 50, 56, 189, 248, 255, 3);
        c.DrawLine(60, 52, 68, 60, 56, 189, 248, 255, 3);
        c.DrawLine(68, 60, 60, 68, 56, 189, 248, 255, 3);
        // Líneas de código
        c.DrawLine(36, 80, 80, 80, 148, 163, 184, 255, 3);
        c.DrawLine(36, 88, 68, 88, 148, 163, 184, 255, 3);
        c.DrawLine(36, 96, 88, 96, 56, 189, 248, 255, 3);
        m_ScriptIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 3. Icono de Animaciones / Clips (.anim) (Púrpura intenso con tira de celuloide y play)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 42, 16, 68, 255, 14);
        c.FillRect(24, 24, 80, 80, 76, 29, 149, 255, 10);
        // Perforaciones superiores
        for (int x = 28; x <= 92; x += 16) {
            c.FillRect(x, 28, 8, 8, 233, 213, 255, 255, 2);
            c.FillRect(x, 92, 8, 8, 233, 213, 255, 255, 2);
        }
        // Triángulo de Play brillante
        c.FillTriangle(50, 46, 50, 82, 84, 64, 216, 180, 254);
        m_AnimationIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 4. Icono de Shader Graph (.shadergraph) (Esmeralda / Menta con nodos y conexiones)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 12, 40, 32, 255, 14);
        // Nodo 1
        c.FillRect(24, 34, 38, 26, 5, 150, 105, 255, 5);
        // Nodo 2
        c.FillRect(66, 64, 38, 26, 16, 185, 129, 255, 5);
        // Cable de conexión
        c.DrawLine(62, 47, 66, 77, 110, 231, 183, 255, 3);
        // Pines de conexión
        c.FillCircle(62, 47, 4, 167, 243, 208);
        c.FillCircle(66, 77, 4, 167, 243, 208);
        m_ShaderGraphIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 5. Icono de Prefab (.prefab) (Teal / Turquesa con cubo 3D)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 17, 60, 58, 255, 14);
        // Caras de cubo isométrico
        c.FillTriangle(64, 30, 94, 46, 64, 62, 45, 212, 191);
        c.FillTriangle(64, 30, 64, 62, 34, 46, 45, 212, 191);
        c.FillTriangle(34, 46, 64, 62, 64, 98, 20, 184, 166);
        c.FillTriangle(34, 46, 64, 98, 34, 82, 20, 184, 166);
        c.FillTriangle(64, 62, 94, 46, 94, 82, 13, 148, 136);
        c.FillTriangle(64, 62, 94, 82, 64, 98, 13, 148, 136);
        m_PrefabIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 6. Icono de Escena (.scene) (Ámbar con paisaje montañoso y sol)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 58, 24, 6, 255, 14);
        c.FillCircle(84, 42, 12, 251, 191, 36); // Sol
        c.FillTriangle(24, 94, 56, 48, 88, 94, 217, 119, 6);  // Montaña 1
        c.FillTriangle(58, 94, 86, 58, 106, 94, 245, 158, 11); // Montaña 2
        m_SceneIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 7. Icono de Animator Controller (.controller) (Índigo con grafo de estados)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 55, 24, 102, 255, 14);
        c.FillRect(32, 32, 28, 20, 168, 85, 247, 255, 4);
        c.FillRect(68, 54, 28, 20, 192, 132, 252, 255, 4);
        c.FillRect(32, 76, 28, 20, 216, 180, 254, 255, 4);
        c.DrawLine(46, 52, 82, 54, 233, 213, 255, 255, 2);
        c.DrawLine(82, 74, 46, 76, 233, 213, 255, 255, 2);
        m_ControllerIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 8. Icono de Audio (.wav / .ogg / .mp3) (Rosa con barras de ecualizador)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 68, 8, 28, 255, 14);
        int barHeights[] = { 24, 48, 64, 38, 54 };
        int xPos[] = { 32, 46, 60, 74, 88 };
        for (int i = 0; i < 5; ++i) {
            int h = barHeights[i];
            int y = 64 - h / 2;
            c.FillRect(xPos[i], y, 8, h, 244, 63, 94, 255, 4);
        }
        m_AudioIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }

    // 9. Icono de Archivo Genérico (Gris azulado con esquina doblada)
    {
        Canvas128 c;
        c.FillRect(14, 14, 100, 100, 24, 32, 48, 255, 14);
        c.FillRect(28, 22, 72, 84, 51, 65, 85, 255, 6);
        c.FillTriangle(82, 22, 100, 22, 82, 40, 100, 116, 139); // Esquina doblada
        c.DrawLine(38, 48, 76, 48, 148, 163, 184, 255, 3);
        c.DrawLine(38, 60, 88, 60, 148, 163, 184, 255, 3);
        c.DrawLine(38, 72, 70, 72, 148, 163, 184, 255, 3);
        c.DrawLine(38, 84, 82, 84, 148, 163, 184, 255, 3);
        m_FileIcon = CreateTextureFromRGBA(c.data, 128, 128);
    }
}

void SlipThumbnailManager::EnsureFBO() {
    if (m_FBO != 0 || !SDL_GL_GetCurrentContext()) return;

    glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    glGenTextures(1, &m_ColorTex);
    glBindTexture(GL_TEXTURE_2D, m_ColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_ThumbWidth, m_ThumbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTex, 0);

    glGenRenderbuffers(1, &m_DepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_DepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_ThumbWidth, m_ThumbHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SlipThumbnailManager::EnsureSphereResources() {
    if (m_SphereInitialized) return;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    SlipPrimitive::GenerateSphere(0.6f, 32, 32, vertices, indices);
    m_SphereIndexCount = static_cast<unsigned int>(indices.size());

    for (auto& v : vertices) {
        if (glm::length(v.Tangent) < 1e-5f) {
            glm::vec3 up = (std::abs(v.Normal.y) < 0.98f) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
            v.Tangent = glm::normalize(glm::cross(up, v.Normal));
            v.Bitangent = glm::normalize(glm::cross(v.Normal, v.Tangent));
        }
    }

    glGenVertexArrays(1, &m_SphereVAO);
    glGenBuffers(1, &m_SphereVBO);
    glGenBuffers(1, &m_SphereEBO);

    glBindVertexArray(m_SphereVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_SphereVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));

    glBindVertexArray(0);
    m_SphereInitialized = true;
}

bool SlipThumbnailManager::GenerateMaterialThumbnail(const std::string& matPath, bool force) {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL || matPath.empty() || !SlipFileSystem::Exists(matPath) || !SDL_GL_GetCurrentContext()) return false;

    std::string outPng = GetThumbnailPngPath(matPath);
    if (!force && SlipFileSystem::Exists(outPng)) {
        std::error_code ec;
        auto srcTime = fs::last_write_time(matPath, ec);
        auto pngTime = fs::last_write_time(outPng, ec);
        if (!ec && pngTime >= srcTime) {
            return true;
        }
    }

    EnsureFBO();
    EnsureSphereResources();

    GLint prevFBO = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_ThumbWidth, m_ThumbHeight);
    glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    SlipMaterial mat(matPath);
    mat.init();

    glm::vec3 camPos(0.0f, 0.15f, 1.7f);
    glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 20.0f);
    glm::mat4 model = glm::mat4(1.0f);

    SlipShader* shader = mat.shader ? mat.shader : SlipShaderLibrary::Get("Lit");
    if (!shader) shader = SlipShaderLibrary::LoadOrGet("Lit");

    if (shader) {
        shader->use();
        shader->setMat4("u_Model", model);
        shader->setMat4("u_View", view);
        shader->setMat4("u_Projection", proj);
        shader->setVec3("u_CameraPos", camPos);
        shader->setFloat("u_Time", 0.0f);
        shader->setFloat("u_DeltaTime", 0.016f);
        shader->setBool("u_HasBones", false);
        shader->setInt("u_SkinningQuality", 0);
        shader->setBool("u_ShadowsEnabled", false);
        shader->setBool("u_DDGIEnabled", false);
        shader->setBool("u_HasEnvCube", false);
        shader->setInt("u_NumDirLights", 1);
        shader->setVec3("u_DirLights[0].direction", -0.5f, -1.0f, -0.6f);
        shader->setVec3("u_DirLights[0].color", 1.0f, 1.0f, 1.0f);
        shader->setFloat("u_DirLights[0].intensity", 2.2f);
        shader->setInt("u_NumPointLights", 0);
        shader->setInt("u_NumSpotLights", 0);

        mat.bindMaterialProperties(shader);

        glBindVertexArray(m_SphereVAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_SphereIndexCount), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    std::vector<unsigned char> raw(m_ThumbWidth * m_ThumbHeight * 4);
    glReadPixels(0, 0, m_ThumbWidth, m_ThumbHeight, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);

    std::vector<unsigned char> flipped(m_ThumbWidth * m_ThumbHeight * 4);
    for (int y = 0; y < m_ThumbHeight; ++y) {
        std::memcpy(&flipped[y * m_ThumbWidth * 4], &raw[(m_ThumbHeight - 1 - y) * m_ThumbWidth * 4], m_ThumbWidth * 4);
    }

    stbi_write_png(outPng.c_str(), m_ThumbWidth, m_ThumbHeight, 4, flipped.data(), m_ThumbWidth * 4);

    InvalidateThumbnail(matPath);
    unsigned int newTex = LoadTextureFromFile(outPng);
    if (newTex != 0) {
        m_LoadedTextures[matPath] = newTex;
    }
    return true;
}

bool SlipThumbnailManager::GenerateModelThumbnail(const std::string& modelPath, bool force) {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL || modelPath.empty() || !SlipFileSystem::Exists(modelPath) || !SDL_GL_GetCurrentContext()) return false;

    std::string outPng = GetThumbnailPngPath(modelPath);
    if (!force && SlipFileSystem::Exists(outPng)) {
        std::error_code ec;
        auto srcTime = fs::last_write_time(modelPath, ec);
        auto pngTime = fs::last_write_time(outPng, ec);
        if (!ec && pngTime >= srcTime) {
            return true;
        }
    }

    EnsureFBO();

    GLint prevFBO = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_ThumbWidth, m_ThumbHeight);
    glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    SlipMesh mesh(modelPath.c_str());
    mesh.init();
    mesh.computeBounds();

    if (!mesh.meshes.empty()) {
        glm::vec3 center = (mesh.aabbMin + mesh.aabbMax) * 0.5f;
        glm::vec3 ext = mesh.aabbMax - mesh.aabbMin;
        float maxD = (std::max)({ ext.x, ext.y, ext.z, 0.001f });
        float scale = 1.6f / maxD;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::rotate(model, glm::radians(25.0f), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(12.0f), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3(scale));
        model = glm::translate(model, -center);

        glm::vec3 camPos(0.0f, 0.4f, 2.3f);
        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 50.0f);

        SlipShader* litShader = SlipShaderLibrary::Get("Lit");
        if (!litShader) litShader = SlipShaderLibrary::LoadOrGet("Lit");

        if (litShader) {
            litShader->use();
            litShader->setMat4("u_Model", model);
            litShader->setMat4("model", model);
            litShader->setMat4("u_View", view);
            litShader->setMat4("u_Projection", proj);
            litShader->setVec3("u_CameraPos", camPos);
            litShader->setFloat("u_Time", 0.0f);
            litShader->setFloat("u_DeltaTime", 0.016f);
            litShader->setBool("u_HasBones", false);
            litShader->setInt("u_SkinningQuality", 0);
            litShader->setBool("u_ShadowsEnabled", false);
            litShader->setBool("u_DDGIEnabled", false);
            litShader->setBool("u_HasEnvCube", false);
            litShader->setInt("u_NumDirLights", 1);
            litShader->setVec3("u_DirLights[0].direction", -0.5f, -1.0f, -0.6f);
            litShader->setVec3("u_DirLights[0].color", 1.0f, 1.0f, 1.0f);
            litShader->setFloat("u_DirLights[0].intensity", 2.2f);
            litShader->setInt("u_NumPointLights", 0);
            litShader->setInt("u_NumSpotLights", 0);

            for (size_t i = 0; i < mesh.meshes.size(); ++i) {
                if (mesh.meshes[i].VAO == 0) continue;
                uint32_t count = mesh.isUnifiedBuffer ? mesh.meshes[i].indexCount : static_cast<uint32_t>(mesh.meshes[i].indices.size());
                if (count == 0) continue;
                void* offset = mesh.isUnifiedBuffer ? (void*)(uintptr_t)(mesh.meshes[i].firstIndex * sizeof(unsigned int)) : 0;

                int matIdx = mesh.meshes[i].materialIndex;
                if (matIdx >= 0 && matIdx < static_cast<int>(mesh.materials.size()) && mesh.materials[matIdx].mat != nullptr) {
                    mesh.materials[matIdx].mat->bindMaterialProperties(litShader);
                } else {
                    litShader->setVec3("u_Albedo", 0.75f, 0.75f, 0.75f);
                    litShader->setBool("u_HasAlbedoMap", false);
                    litShader->setFloat("u_Roughness", 0.5f);
                    litShader->setFloat("u_Metallic", 0.0f);
                }

                glBindVertexArray(mesh.meshes[i].VAO);
                glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                glBindVertexArray(0);
            }
            glUseProgram(0);
        }
    }

    std::vector<unsigned char> raw(m_ThumbWidth * m_ThumbHeight * 4);
    glReadPixels(0, 0, m_ThumbWidth, m_ThumbHeight, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);

    std::vector<unsigned char> flipped(m_ThumbWidth * m_ThumbHeight * 4);
    for (int y = 0; y < m_ThumbHeight; ++y) {
        std::memcpy(&flipped[y * m_ThumbWidth * 4], &raw[(m_ThumbHeight - 1 - y) * m_ThumbWidth * 4], m_ThumbWidth * 4);
    }

    stbi_write_png(outPng.c_str(), m_ThumbWidth, m_ThumbHeight, 4, flipped.data(), m_ThumbWidth * 4);

    InvalidateThumbnail(modelPath);
    unsigned int newTex = LoadTextureFromFile(outPng);
    if (newTex != 0) {
        m_LoadedTextures[modelPath] = newTex;
    }
    return true;
}

bool SlipThumbnailManager::GenerateTextureThumbnail(const std::string& texturePath, bool force) {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL || texturePath.empty() || !SlipFileSystem::Exists(texturePath)) return false;

    std::string outPng = GetThumbnailPngPath(texturePath);
    if (!force && SlipFileSystem::Exists(outPng)) {
        std::error_code ec;
        auto srcTime = fs::last_write_time(texturePath, ec);
        auto pngTime = fs::last_write_time(outPng, ec);
        if (!ec && pngTime >= srcTime) {
            return true;
        }
    }

    int srcW = 0, srcH = 0, channels = 0;
    unsigned char* srcData = stbi_load(texturePath.c_str(), &srcW, &srcH, &channels, 4);
    if (!srcData || srcW <= 0 || srcH <= 0) {
        if (srcData) stbi_image_free(srcData);
        return false;
    }

    std::vector<unsigned char> dstData(m_ThumbWidth * m_ThumbHeight * 4, 0);

    float scale = (std::min)((float)m_ThumbWidth / (float)srcW, (float)m_ThumbHeight / (float)srcH);
    int fitW = std::clamp((int)(srcW * scale), 1, m_ThumbWidth);
    int fitH = std::clamp((int)(srcH * scale), 1, m_ThumbHeight);
    int offX = (m_ThumbWidth - fitW) / 2;
    int offY = (m_ThumbHeight - fitH) / 2;

    for (int y = 0; y < fitH; ++y) {
        float srcYf = (y + 0.5f) / scale - 0.5f;
        int y0 = std::clamp((int)std::floor(srcYf), 0, srcH - 1);
        int y1 = std::clamp(y0 + 1, 0, srcH - 1);
        float fy = srcYf - y0;

        for (int x = 0; x < fitW; ++x) {
            float srcXf = (x + 0.5f) / scale - 0.5f;
            int x0 = std::clamp((int)std::floor(srcXf), 0, srcW - 1);
            int x1 = std::clamp(x0 + 1, 0, srcW - 1);
            float fx = srcXf - x0;

            const unsigned char* p00 = &srcData[(y0 * srcW + x0) * 4];
            const unsigned char* p10 = &srcData[(y0 * srcW + x1) * 4];
            const unsigned char* p01 = &srcData[(y1 * srcW + x0) * 4];
            const unsigned char* p11 = &srcData[(y1 * srcW + x1) * 4];

            int dstIdx = ((offY + y) * m_ThumbWidth + (offX + x)) * 4;
            for (int c = 0; c < 4; ++c) {
                float top = p00[c] * (1.0f - fx) + p10[c] * fx;
                float bot = p01[c] * (1.0f - fx) + p11[c] * fx;
                dstData[dstIdx + c] = static_cast<unsigned char>(std::clamp(top * (1.0f - fy) + bot * fy, 0.0f, 255.0f));
            }
        }
    }

    stbi_image_free(srcData);

    stbi_write_png(outPng.c_str(), m_ThumbWidth, m_ThumbHeight, 4, dstData.data(), m_ThumbWidth * 4);

    InvalidateThumbnail(texturePath);
    unsigned int newTex = LoadTextureFromFile(outPng);
    if (newTex != 0) {
        m_LoadedTextures[texturePath] = newTex;
    }
    return true;
}

unsigned int SlipThumbnailManager::GetThumbnail(const std::string& assetPath, bool isDirectory, const std::string& extension) {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) {
        return 0;
    }
    if (!m_Initialized) {
        Init();
    }

    if (isDirectory) {
        return m_FolderIcon;
    }

    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".lua") return m_ScriptIcon;
    if (ext == ".anim") return m_AnimationIcon;
    if (ext == ".shadergraph") return m_ShaderGraphIcon;
    if (ext == ".prefab") return m_PrefabIcon;
    if (ext == ".scene") return m_SceneIcon;
    if (ext == ".controller") return m_ControllerIcon;
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return m_AudioIcon;

    // Verificar si ya está en caché de memoria GL
    auto it = m_LoadedTextures.find(assetPath);
    if (it != m_LoadedTextures.end() && it->second != 0) {
        return it->second;
    }

    bool isModel = (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae");
    bool isMat = (ext == ".mat");
    bool isTex = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp");

    if (isModel || isMat || isTex) {
        std::string pngPath = GetThumbnailPngPath(assetPath);
        bool needsGen = true;
        if (SlipFileSystem::Exists(pngPath)) {
            std::error_code ec;
            auto srcTime = fs::last_write_time(assetPath, ec);
            auto pngTime = fs::last_write_time(pngPath, ec);
            if (!ec && pngTime >= srcTime) {
                needsGen = false;
            }
        }

        if (needsGen) {
            if (isModel) GenerateModelThumbnail(assetPath);
            else if (isMat) GenerateMaterialThumbnail(assetPath);
            else if (isTex) GenerateTextureThumbnail(assetPath);
        } else {
            unsigned int tex = LoadTextureFromFile(pngPath);
            if (tex != 0) {
                m_LoadedTextures[assetPath] = tex;
                return tex;
            }
        }

        auto itAfter = m_LoadedTextures.find(assetPath);
        if (itAfter != m_LoadedTextures.end() && itAfter->second != 0) {
            return itAfter->second;
        }
    }

    return m_FileIcon;
}
