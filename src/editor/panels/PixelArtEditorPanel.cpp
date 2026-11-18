#include "SlipEngine/editor/panels/PixelArtEditorPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/render/SlipTexture.h"
#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/editor/SlipFileDialog.h"
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <queue>
#include <algorithm>
#include <cstring>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {
// Empaquetar y desempaquetar RGBA
inline uint32_t ColorToUint(const glm::vec4& c) {
    uint8_t r = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(glm::clamp(c.a, 0.0f, 1.0f) * 255.0f);
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           (static_cast<uint32_t>(r));
}

inline glm::vec4 UintToColor(uint32_t u) {
    float r = static_cast<float>(u & 0xFF) / 255.0f;
    float g = static_cast<float>((u >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>((u >> 16) & 0xFF) / 255.0f;
    float a = static_cast<float>((u >> 24) & 0xFF) / 255.0f;
    return glm::vec4(r, g, b, a);
}

// Mezclar color sobre fondo con alfa
inline uint32_t BlendColors(uint32_t bg, uint32_t fg) {
    uint8_t fgA = (fg >> 24) & 0xFF;
    if (fgA == 0) return bg;
    if (fgA == 255) return fg;

    float alpha = static_cast<float>(fgA) / 255.0f;
    float invAlpha = 1.0f - alpha;

    float r = (fg & 0xFF) * alpha + (bg & 0xFF) * invAlpha;
    float g = ((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * invAlpha;
    float b = ((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * invAlpha;
    float a = std::min(255.0f, static_cast<float>(fgA) + ((bg >> 24) & 0xFF) * invAlpha);

    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           (static_cast<uint32_t>(r));
}

inline uint32_t TintColor(uint32_t col, const glm::vec4& tint) {
    uint8_t origA = (col >> 24) & 0xFF;
    if (origA == 0) return 0;
    float r = (col & 0xFF) / 255.0f * tint.r;
    float g = ((col >> 8) & 0xFF) / 255.0f * tint.g;
    float b = ((col >> 16) & 0xFF) / 255.0f * tint.b;
    float a = (origA / 255.0f) * tint.a;
    return ColorToUint(glm::vec4(r, g, b, a));
}
} // namespace

PixelArtEditorPanel::PixelArtEditorPanel()
{
    m_PanelTitle = "Editor de Sprites Pixel Art";
    m_IsOpen = false; // Se abre desde el menú "Window -> 2D -> Pixel Art Sprite Editor"

    // Paleta inicial clásica PICO-8 (16 colores emblemáticos de pixel art)
    m_Palette = {
        glm::vec4(0.00f, 0.00f, 0.00f, 1.0f), // 0: Negro
        glm::vec4(0.11f, 0.17f, 0.33f, 1.0f), // 1: Azul Oscuro
        glm::vec4(0.49f, 0.15f, 0.33f, 1.0f), // 2: Púrpura Oscuro
        glm::vec4(0.00f, 0.53f, 0.32f, 1.0f), // 3: Verde Oscuro
        glm::vec4(0.67f, 0.32f, 0.21f, 1.0f), // 4: Marrón
        glm::vec4(0.37f, 0.34f, 0.31f, 1.0f), // 5: Gris Oscuro
        glm::vec4(0.76f, 0.76f, 0.78f, 1.0f), // 6: Gris Claro
        glm::vec4(1.00f, 0.95f, 0.91f, 1.0f), // 7: Blanco
        glm::vec4(1.00f, 0.00f, 0.30f, 1.0f), // 8: Rojo
        glm::vec4(1.00f, 0.64f, 0.00f, 1.0f), // 9: Naranja
        glm::vec4(1.00f, 0.93f, 0.15f, 1.0f), // 10: Amarillo
        glm::vec4(0.00f, 0.90f, 0.21f, 1.0f), // 11: Verde Claro
        glm::vec4(0.16f, 0.68f, 1.00f, 1.0f), // 12: Azul Cielo
        glm::vec4(0.51f, 0.46f, 0.61f, 1.0f), // 13: Lavanda
        glm::vec4(1.00f, 0.47f, 0.66f, 1.0f), // 14: Rosa
        glm::vec4(1.00f, 0.80f, 0.67f, 1.0f)  // 15: Durazno
    };

    SetResolution(32, 32);
}

PixelArtEditorPanel::~PixelArtEditorPanel()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glDeleteTextures) return;
#endif
    if (m_CanvasTexID != 0) {
        glDeleteTextures(1, &m_CanvasTexID);
        m_CanvasTexID = 0;
    }
    if (m_PreviewTexID != 0) {
        glDeleteTextures(1, &m_PreviewTexID);
        m_PreviewTexID = 0;
    }
}

void PixelArtEditorPanel::SetResolution(int width, int height)
{
    m_CanvasWidth = std::clamp(width, 4, 512);
    m_CanvasHeight = std::clamp(height, 4, 512);
    m_Frames.clear();

    PixelArtFrame initialFrame;
    initialFrame.pixels.resize(m_CanvasWidth * m_CanvasHeight, 0); // Transparente por defecto
    m_Frames.push_back(initialFrame);
    m_CurrentFrameIdx = 0;

    m_DisplayBuffer.resize(m_CanvasWidth * m_CanvasHeight, 0);
    m_UndoStack.clear();

    UpdateTextureGL();
}

void PixelArtEditorPanel::AddFrame(bool duplicateCurrent)
{
    PixelArtFrame newFrame;
    newFrame.pixels.resize(m_CanvasWidth * m_CanvasHeight, 0);

    if (duplicateCurrent && m_CurrentFrameIdx >= 0 && m_CurrentFrameIdx < static_cast<int>(m_Frames.size())) {
        newFrame.pixels = m_Frames[m_CurrentFrameIdx].pixels;
    }

    int insertIdx = m_CurrentFrameIdx + 1;
    if (insertIdx > static_cast<int>(m_Frames.size())) insertIdx = static_cast<int>(m_Frames.size());
    m_Frames.insert(m_Frames.begin() + insertIdx, newFrame);
    m_CurrentFrameIdx = insertIdx;

    UpdateTextureGL();
}

void PixelArtEditorPanel::RemoveFrame(int index)
{
    if (m_Frames.size() <= 1) {
        ClearCurrentFrame();
        return;
    }
    if (index >= 0 && index < static_cast<int>(m_Frames.size())) {
        m_Frames.erase(m_Frames.begin() + index);
        if (m_CurrentFrameIdx >= static_cast<int>(m_Frames.size())) {
            m_CurrentFrameIdx = static_cast<int>(m_Frames.size()) - 1;
        }
        UpdateTextureGL();
    }
}

void PixelArtEditorPanel::ClearCurrentFrame()
{
    if (m_CurrentFrameIdx >= 0 && m_CurrentFrameIdx < static_cast<int>(m_Frames.size())) {
        PushUndo();
        std::fill(m_Frames[m_CurrentFrameIdx].pixels.begin(), m_Frames[m_CurrentFrameIdx].pixels.end(), 0);
        UpdateTextureGL();
    }
}

void PixelArtEditorPanel::PushUndo()
{
    if (m_CurrentFrameIdx >= 0 && m_CurrentFrameIdx < static_cast<int>(m_Frames.size())) {
        m_UndoStack.push_back(m_Frames[m_CurrentFrameIdx].pixels);
        if (m_UndoStack.size() > 30) {
            m_UndoStack.erase(m_UndoStack.begin());
        }
    }
}

void PixelArtEditorPanel::UpdateTextureGL()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) {
        m_CanvasDirty = true;
        return;
    }
#endif
    if (m_CurrentFrameIdx < 0 || m_CurrentFrameIdx >= static_cast<int>(m_Frames.size())) return;

    size_t totalPixels = static_cast<size_t>(m_CanvasWidth) * m_CanvasHeight;
    if (m_DisplayBuffer.size() != totalPixels) {
        m_DisplayBuffer.resize(totalPixels, 0);
    }

    // 1. Limpiar buffer de visualización
    std::fill(m_DisplayBuffer.begin(), m_DisplayBuffer.end(), 0);

    // 2. Componer Onion Skinning (Piel de Cebolla / Fantasmas de frames)
    if (m_OnionSkinning) {
        // Fantasma del Frame Anterior (Teñido rojizo/cálido)
        if (m_CurrentFrameIdx > 0) {
            const auto& prevPixels = m_Frames[m_CurrentFrameIdx - 1].pixels;
            for (size_t i = 0; i < totalPixels; ++i) {
                if (prevPixels[i] != 0) {
                    uint32_t ghost = TintColor(prevPixels[i], m_OnionPrevTint);
                    m_DisplayBuffer[i] = BlendColors(m_DisplayBuffer[i], ghost);
                }
            }
        }

        // Fantasma del Frame Posterior (Teñido cian/azulado)
        if (m_CurrentFrameIdx + 1 < static_cast<int>(m_Frames.size())) {
            const auto& nextPixels = m_Frames[m_CurrentFrameIdx + 1].pixels;
            for (size_t i = 0; i < totalPixels; ++i) {
                if (nextPixels[i] != 0) {
                    uint32_t ghost = TintColor(nextPixels[i], m_OnionNextTint);
                    m_DisplayBuffer[i] = BlendColors(m_DisplayBuffer[i], ghost);
                }
            }
        }
    }

    // 3. Dibujar el Frame Actual con 100% de opacidad encima
    const auto& currentPixels = m_Frames[m_CurrentFrameIdx].pixels;
    for (size_t i = 0; i < totalPixels; ++i) {
        if (currentPixels[i] != 0) {
            m_DisplayBuffer[i] = BlendColors(m_DisplayBuffer[i], currentPixels[i]);
        }
    }

    // 4. Subir a textura OpenGL
    if (m_CanvasTexID == 0) {
        glGenTextures(1, &m_CanvasTexID);
    }
    glBindTexture(GL_TEXTURE_2D, m_CanvasTexID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_CanvasWidth, m_CanvasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_DisplayBuffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PixelArtEditorPanel::UpdatePreviewTextureGL()
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glad_glGenTextures) {
        m_PreviewDirty = true;
        return;
    }
#endif
    if (m_Frames.empty()) return;
    int frameIdx = std::clamp(m_PreviewCurrentFrame, 0, static_cast<int>(m_Frames.size()) - 1);
    const auto& pixels = m_Frames[frameIdx].pixels;

    if (m_PreviewTexID == 0) {
        glGenTextures(1, &m_PreviewTexID);
    }
    glBindTexture(GL_TEXTURE_2D, m_PreviewTexID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_CanvasWidth, m_CanvasHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PixelArtEditorPanel::PaintAt(int px, int py)
{
    if (px < 0 || px >= m_CanvasWidth || py < 0 || py >= m_CanvasHeight) return;
    if (m_CurrentFrameIdx < 0 || m_CurrentFrameIdx >= static_cast<int>(m_Frames.size())) return;
    auto& pixels = m_Frames[m_CurrentFrameIdx].pixels;

    int half = m_BrushSize / 2;
    int minX = (std::max)(0, px - half);
    int maxX = std::min(m_CanvasWidth - 1, px + (m_BrushSize - 1 - half));
    int minY = (std::max)(0, py - half);
    int maxY = std::min(m_CanvasHeight - 1, py + (m_BrushSize - 1 - half));

    if (m_CurrentTool == PixelTool::Pencil) {
        uint32_t col = ColorToUint(m_CurrentColor);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                pixels[y * m_CanvasWidth + x] = col;
            }
        }
    } else if (m_CurrentTool == PixelTool::Eraser) {
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                pixels[y * m_CanvasWidth + x] = 0;
            }
        }
    }
}

void PixelArtEditorPanel::PaintLine(int x0, int y0, int x1, int y1)
{
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        PaintAt(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void PixelArtEditorPanel::ApplyTool(int px, int py, bool isFirstClick)
{
    if (px < 0 || px >= m_CanvasWidth || py < 0 || py >= m_CanvasHeight) return;
    if (m_CurrentFrameIdx < 0 || m_CurrentFrameIdx >= static_cast<int>(m_Frames.size())) return;

    if (isFirstClick) {
        PushUndo();
    }

    if (m_CurrentTool == PixelTool::BucketFill) {
        if (isFirstClick) {
            auto& pixels = m_Frames[m_CurrentFrameIdx].pixels;
            uint32_t target = pixels[py * m_CanvasWidth + px];
            uint32_t fillCol = ColorToUint(m_CurrentColor);
            if (target != fillCol) {
                FloodFill(px, py, target, fillCol);
                UpdateTextureGL();
            }
        }
    } else if (m_CurrentTool == PixelTool::Eyedropper) {
        if (isFirstClick) {
            auto& pixels = m_Frames[m_CurrentFrameIdx].pixels;
            uint32_t picked = pixels[py * m_CanvasWidth + px];
            if (picked != 0) {
                m_CurrentColor = UintToColor(picked);
            }
            m_CurrentTool = PixelTool::Pencil;
        }
    } else {
        // Lápiz o Goma: Trazado continuo suave (Bresenham) entre posiciones del ratón
        if (isFirstClick || m_LastPaintedX < 0 || m_LastPaintedY < 0) {
            PaintAt(px, py);
        } else {
            PaintLine(m_LastPaintedX, m_LastPaintedY, px, py);
        }
        m_LastPaintedX = px;
        m_LastPaintedY = py;
        UpdateTextureGL();
    }
}

void PixelArtEditorPanel::FloodFill(int startX, int startY, uint32_t targetCol, uint32_t fillCol)
{
    auto& pixels = m_Frames[m_CurrentFrameIdx].pixels;
    std::queue<std::pair<int, int>> q;
    q.push({ startX, startY });
    pixels[startY * m_CanvasWidth + startX] = fillCol;

    const int dx[] = { 1, -1, 0, 0 };
    const int dy[] = { 0, 0, 1, -1 };

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (nx >= 0 && nx < m_CanvasWidth && ny >= 0 && ny < m_CanvasHeight) {
                size_t idx = static_cast<size_t>(ny) * m_CanvasWidth + nx;
                if (pixels[idx] == targetCol) {
                    pixels[idx] = fillCol;
                    q.push({ nx, ny });
                }
            }
        }
    }
}

bool PixelArtEditorPanel::SaveProject(const std::string& filePath)
{
    if (filePath.empty()) return false;

    std::ofstream file(filePath);
    if (!file.is_open()) return false;

    file << "SlipPixelArtProject: 1.0\n";
    file << "AnimationName: " << m_AnimationName << "\n";
    file << "Width: " << m_CanvasWidth << "\n";
    file << "Height: " << m_CanvasHeight << "\n";
    file << "FPS: " << m_PreviewFPS << "\n";
    file << "ExportPngPath: " << m_ExportPngPath << "\n";
    file << "FrameCount: " << m_Frames.size() << "\n";

    for (size_t f = 0; f < m_Frames.size(); ++f) {
        file << "Frame_" << f << ": ";
        const auto& p = m_Frames[f].pixels;
        for (size_t i = 0; i < p.size(); ++i) {
            file << std::hex << std::setw(8) << std::setfill('0') << p[i] << " ";
        }
        file << std::dec << "\n";
    }

    file.close();
    m_CurrentProjectPath = filePath;
    return true;
}

bool PixelArtEditorPanel::LoadProject(const std::string& filePath)
{
    if (filePath.empty() || !SlipFileSystem::Exists(filePath)) return false;

    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::string line;
    int loadedW = 32, loadedH = 32, frameCount = 0;
    std::vector<PixelArtFrame> loadedFrames;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        size_t first = val.find_first_not_of(" \t");
        if (first != std::string::npos) val = val.substr(first);

        if (key == "AnimationName") m_AnimationName = val;
        else if (key == "Width") loadedW = std::stoi(val);
        else if (key == "Height") loadedH = std::stoi(val);
        else if (key == "FPS") m_PreviewFPS = std::stof(val);
        else if (key == "ExportPngPath") m_ExportPngPath = val;
        else if (key == "FrameCount") frameCount = std::stoi(val);
        else if (key.rfind("Frame_", 0) == 0) {
            PixelArtFrame f;
            f.pixels.resize(loadedW * loadedH, 0);
            std::stringstream ss(val);
            for (size_t i = 0; i < f.pixels.size(); ++i) {
                uint32_t valHex = 0;
                ss >> std::hex >> valHex;
                f.pixels[i] = valHex;
            }
            loadedFrames.push_back(f);
        }
    }

    if (!loadedFrames.empty()) {
        m_CanvasWidth = loadedW;
        m_CanvasHeight = loadedH;
        m_Frames = loadedFrames;
        m_CurrentFrameIdx = 0;
        m_CurrentProjectPath = filePath;
        UpdateTextureGL();
        return true;
    }

    return false;
}

bool PixelArtEditorPanel::BakeSpritesheet(const std::string& pngFilePath)
{
    if (m_Frames.empty() || m_CanvasWidth <= 0 || m_CanvasHeight <= 0) return false;

    std::string exportPath = pngFilePath;
    if (exportPath.empty()) {
        exportPath = m_ExportPngPath;
    }
    if (exportPath.empty()) {
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (assetsDir.empty()) assetsDir = "assets";
        exportPath = SlipFileSystem::CombinePath(assetsDir, m_AnimationName + "_spritesheet.png");
    }

    int frameCount = static_cast<int>(m_Frames.size());
    int sheetWidth = m_CanvasWidth * frameCount;
    int sheetHeight = m_CanvasHeight;

    std::vector<uint32_t> sheetPixels(sheetWidth * sheetHeight, 0);

    for (int f = 0; f < frameCount; ++f) {
        int xOffset = f * m_CanvasWidth;
        const auto& framePixels = m_Frames[f].pixels;

        for (int y = 0; y < m_CanvasHeight; ++y) {
            for (int x = 0; x < m_CanvasWidth; ++x) {
                size_t srcIdx = y * m_CanvasWidth + x;
                size_t dstIdx = y * sheetWidth + (xOffset + x);
                sheetPixels[dstIdx] = framePixels[srcIdx];
            }
        }
    }

    int success = stbi_write_png(exportPath.c_str(), sheetWidth, sheetHeight, 4, sheetPixels.data(), sheetWidth * 4);
    if (!success) return false;

    m_ExportPngPath = exportPath;

    // Guardar automáticamente el archivo de proyecto .slippixel al lado del PNG
    std::string projectPath = exportPath;
    size_t dotPos = projectPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        projectPath = projectPath.substr(0, dotPos);
    }
    projectPath += ".slippixel";
    SaveProject(projectPath);

    // Recargar textura en caché de SlipEngine para reflejar cambios instantáneamente
    SlipTexture* reloaded = SlipTexture::reload(exportPath.c_str());
    if (reloaded) {
        reloaded->SetFilterMode(true);
    }

    return true;
}

void PixelArtEditorPanel::AssignToSelectedEntity()
{
    if (m_ExportPngPath.empty()) {
        BakeSpritesheet("");
    }

    if (!m_ExportPngPath.empty() && m_SelectedEntity) {
        // Configurar SpriteRenderer con filtrado Point (Nearest)
        auto sr = m_SelectedEntity->GetComponent<SpriteRendererComponent>();
        if (!sr) {
            sr = m_SelectedEntity->AddComponent<SpriteRendererComponent>();
        }
        sr->isPixelArt = true;
        sr->SetTexture(m_ExportPngPath);
        if (sr->texture) {
            sr->texture->SetFilterMode(true);
        }
        sr->hFrames = static_cast<int>(m_Frames.size());
        sr->vFrames = 1;
        sr->currentFrame = 0;

        // Configurar SpriteAnimator
        auto sa = m_SelectedEntity->GetComponent<SpriteAnimatorComponent>();
        if (!sa) {
            sa = m_SelectedEntity->AddComponent<SpriteAnimatorComponent>();
        }
        sa->clips.clear();
        sa->AddClip(m_AnimationName, 0, static_cast<int>(m_Frames.size()) - 1, m_PreviewFPS, true);
        sa->playOnStart = true;
        sa->Play(m_AnimationName);
    }
}

void PixelArtEditorPanel::OnImGuiRender()
{
    if (!m_IsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(960, 680), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin(m_PanelTitle.c_str(), &m_IsOpen, windowFlags)) {
        // Menú superior
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Archivo")) {
                if (ImGui::MenuItem("Nuevo Proyecto (32x32)")) {
                    SetResolution(32, 32);
                }
                if (ImGui::MenuItem("Nuevo Proyecto (64x64)")) {
                    SetResolution(64, 64);
                }
                if (ImGui::MenuItem("Nuevo Proyecto (128x128)")) {
                    SetResolution(128, 128);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Guardar Proyecto (.slippixel)")) {
                    if (m_CurrentProjectPath.empty()) {
                        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                        m_CurrentProjectPath = SlipFileSystem::CombinePath(assetsDir, m_AnimationName + ".slippixel");
                    }
                    SaveProject(m_CurrentProjectPath);
                }
                if (ImGui::MenuItem("Abrir Proyecto (.slippixel)...")) {
                    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
                    auto files = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".slippixel");
                    if (!files.empty()) {
                        LoadProject(files[0].fullPath);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Bakear Spritesheet (.png)")) {
                    BakeSpritesheet("");
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Lienzo")) {
                if (ImGui::MenuItem("16 x 16")) SetResolution(16, 16);
                if (ImGui::MenuItem("24 x 24")) SetResolution(24, 24);
                if (ImGui::MenuItem("32 x 32")) SetResolution(32, 32);
                if (ImGui::MenuItem("48 x 48")) SetResolution(48, 48);
                if (ImGui::MenuItem("64 x 64")) SetResolution(64, 64);
                if (ImGui::MenuItem("128 x 128")) SetResolution(128, 128);
                ImGui::Separator();
                ImGui::MenuItem("Mostrar Cuadrícula", nullptr, &m_ShowGrid);
                ImGui::MenuItem("Fondo Damero Transparente", nullptr, &m_ShowCheckerboard);
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // 1. Barra de Herramientas Horizontal Superior
        RenderToolbar();
        ImGui::Separator();

        // 2. Área Central (Lienzo Grande a la Izquierda + Panel Lateral Derecho)
        float timelineHeight = 90.0f;
        ImVec2 fullAvail = ImGui::GetContentRegionAvail();
        float mainAreaHeight = (std::max)(120.0f, fullAvail.y - timelineHeight - 6.0f);
        float sidebarWidth = 240.0f;
        float canvasAreaWidth = (std::max)(200.0f, fullAvail.x - sidebarWidth - 8.0f);

        // Lienzo de Dibujo
        ImGui::BeginChild("CanvasContainer", ImVec2(canvasAreaWidth, mainAreaHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderCanvas();
        ImGui::EndChild();

        ImGui::SameLine();

        // Panel Lateral Derecho: Color, Paleta y Previsualización
        ImGui::BeginChild("SidebarContainer", ImVec2(sidebarWidth, mainAreaHeight), true);
        RenderColorPalette();
        ImGui::Separator();
        RenderPreviewWindow();
        ImGui::EndChild();

        // 3. Línea de Tiempo de Animación Inferior
        ImGui::Separator();
        ImGui::BeginChild("TimelineContainer", ImVec2(0.0f, timelineHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderTimeline();
        ImGui::EndChild();
    }
    ImGui::End();
}

void PixelArtEditorPanel::RenderToolbar()
{
    auto toolButton = [&](const char* label, PixelTool tool) {
        bool active = (m_CurrentTool == tool);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.58f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(56.0f, 26.0f))) {
            m_CurrentTool = tool;
        }
        if (active) ImGui::PopStyleColor(2);
    };

    auto sizeButton = [&](const char* label, int size) {
        bool active = (m_BrushSize == size);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.68f, 0.42f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(38.0f, 26.0f))) {
            m_BrushSize = size;
        }
        if (active) ImGui::PopStyleColor(2);
    };

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Herramientas:");
    ImGui::SameLine();
    toolButton("Lapiz", PixelTool::Pencil);
    ImGui::SameLine();
    toolButton("Goma", PixelTool::Eraser);
    ImGui::SameLine();
    toolButton("Cubo", PixelTool::BucketFill);
    ImGui::SameLine();
    toolButton("Gotero", PixelTool::Eyedropper);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Tam:");
    ImGui::SameLine();
    sizeButton("1px", 1);
    ImGui::SameLine();
    sizeButton("2px", 2);
    ImGui::SameLine();
    sizeButton("3px", 3);
    ImGui::SameLine();
    sizeButton("4px", 4);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Limpiar", ImVec2(60.0f, 26.0f))) {
        ClearCurrentFrame();
    }
    ImGui::SameLine();
    if (ImGui::Button("Deshacer", ImVec2(68.0f, 26.0f))) {
        if (!m_UndoStack.empty() && m_CurrentFrameIdx >= 0 && m_CurrentFrameIdx < static_cast<int>(m_Frames.size())) {
            m_Frames[m_CurrentFrameIdx].pixels = m_UndoStack.back();
            m_UndoStack.pop_back();
            UpdateTextureGL();
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    if (ImGui::Button("-", ImVec2(24.0f, 26.0f))) {
        m_Zoom = std::clamp(m_Zoom - 2.0f, 2.0f, 64.0f);
    }
    ImGui::SameLine();
    std::string zStr = std::to_string(static_cast<int>(m_Zoom)) + "x";
    ImGui::Text("%s", zStr.c_str());
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(24.0f, 26.0f))) {
        m_Zoom = std::clamp(m_Zoom + 2.0f, 2.0f, 64.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("Centrar", ImVec2(60.0f, 26.0f))) {
        m_PanOffset = glm::vec2(0.0f, 0.0f);
        m_Zoom = 16.0f;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Cuadricula", &m_ShowGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Damero", &m_ShowCheckerboard);
    ImGui::SameLine();
    if (ImGui::Checkbox("Piel Cebolla", &m_OnionSkinning)) {
        UpdateTextureGL();
    }
}

void PixelArtEditorPanel::RenderCanvas()
{
    if (m_CanvasDirty || m_CanvasTexID == 0) {
        UpdateTextureGL();
        m_CanvasDirty = false;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("CanvasRegion", avail, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Widget invisible interactivo que captura todos los clics y arrastres dentro del lienzo.
    // Esto garantiza que ImGui NUNCA arrastre la ventana al mantener presionado el ratón.
    ImGui::InvisibleButton("##CanvasMouseTrap", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool isCanvasHovered = ImGui::IsItemHovered();
    bool isCanvasActive = ImGui::IsItemActive();
    ImGui::SetCursorScreenPos(canvasOrigin);

    // Zoom con rueda del ratón enfocado en el cursor
    if (isCanvasHovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float oldZoom = m_Zoom;
            float step = (m_Zoom >= 16.0f ? 2.0f : 1.0f) * wheel;
            m_Zoom = std::clamp(m_Zoom + step, 2.0f, 64.0f);
            
            ImVec2 m = ImGui::GetIO().MousePos;
            float scale = m_Zoom / oldZoom;
            m_PanOffset.x = (m_PanOffset.x - (m.x - canvasOrigin.x - avail.x * 0.5f)) * scale + (m.x - canvasOrigin.x - avail.x * 0.5f);
            m_PanOffset.y = (m_PanOffset.y - (m.y - canvasOrigin.y - avail.y * 0.5f)) * scale + (m.y - canvasOrigin.y - avail.y * 0.5f);
        }
    }

    // Paneo con botón derecho o botón central arrastrado
    if (isCanvasActive && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_PanOffset.x += delta.x;
        m_PanOffset.y += delta.y;
    }

    float totalW = m_CanvasWidth * m_Zoom;
    float totalH = m_CanvasHeight * m_Zoom;
    ImVec2 renderPos(canvasOrigin.x + m_PanOffset.x + (avail.x - totalW) * 0.5f,
                     canvasOrigin.y + m_PanOffset.y + (avail.y - totalH) * 0.5f);

    // 1. Fondo de Damero (Checkerboard para transparencia alfa)
    if (m_ShowCheckerboard) {
        float checkSize = (std::max)(6.0f, m_Zoom);
        int cols = static_cast<int>(totalW / checkSize) + 1;
        int rows = static_cast<int>(totalH / checkSize) + 1;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                ImU32 col = ((r + c) % 2 == 0) ? IM_COL32(48, 48, 54, 255) : IM_COL32(32, 32, 38, 255);
                ImVec2 pMin(renderPos.x + c * checkSize, renderPos.y + r * checkSize);
                ImVec2 pMax(std::min(renderPos.x + totalW, pMin.x + checkSize),
                            std::min(renderPos.y + totalH, pMin.y + checkSize));
                if (pMin.x < renderPos.x + totalW && pMin.y < renderPos.y + totalH) {
                    drawList->AddRectFilled(pMin, pMax, col);
                }
            }
        }
    } else {
        drawList->AddRectFilled(renderPos, ImVec2(renderPos.x + totalW, renderPos.y + totalH), IM_COL32(28, 28, 33, 255));
    }

    // 2. Renderizado de Píxeles 100% Nítido y Preciso (Sin filtrado ni suavizado)
    for (int y = 0; y < m_CanvasHeight; ++y) {
        for (int x = 0; x < m_CanvasWidth; ++x) {
            uint32_t col = m_DisplayBuffer[y * m_CanvasWidth + x];
            if ((col >> 24) != 0) { // Si tiene opacidad
                ImVec2 pMin(renderPos.x + x * m_Zoom, renderPos.y + y * m_Zoom);
                ImVec2 pMax(pMin.x + m_Zoom, pMin.y + m_Zoom);
                uint8_t r = col & 0xFF;
                uint8_t g = (col >> 8) & 0xFF;
                uint8_t b = (col >> 16) & 0xFF;
                uint8_t a = (col >> 24) & 0xFF;
                drawList->AddRectFilled(pMin, pMax, IM_COL32(r, g, b, a));
            }
        }
    }

    // 3. Cuadrícula de píxeles (Pixel Grid) cuando hay suficiente zoom
    if (m_ShowGrid && m_Zoom >= 4.0f) {
        ImU32 gridColor = IM_COL32(110, 110, 125, 60);
        for (int x = 0; x <= m_CanvasWidth; ++x) {
            float lx = renderPos.x + x * m_Zoom;
            drawList->AddLine(ImVec2(lx, renderPos.y), ImVec2(lx, renderPos.y + totalH), gridColor);
        }
        for (int y = 0; y <= m_CanvasHeight; ++y) {
            float ly = renderPos.y + y * m_Zoom;
            drawList->AddLine(ImVec2(renderPos.x, ly), ImVec2(renderPos.x + totalW, ly), gridColor);
        }
    }

    // Borde exterior del lienzo cuadrado
    drawList->AddRect(renderPos, ImVec2(renderPos.x + totalW, renderPos.y + totalH), IM_COL32(200, 200, 215, 230), 0.0f, 0, 1.5f);

    // 4. Interacción con el ratón: Pintado continuo con clic izquierdo
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool inCanvasBounds = (mousePos.x >= renderPos.x && mousePos.x < renderPos.x + totalW &&
                           mousePos.y >= renderPos.y && mousePos.y < renderPos.y + totalH);

    if (inCanvasBounds) {
        int px = static_cast<int>((mousePos.x - renderPos.x) / m_Zoom);
        int py = static_cast<int>((mousePos.y - renderPos.y) / m_Zoom);
        px = glm::clamp(px, 0, m_CanvasWidth - 1);
        py = glm::clamp(py, 0, m_CanvasHeight - 1);

        // Cursor resaltado del tamaño de brocha
        int half = m_BrushSize / 2;
        int minX = (std::max)(0, px - half);
        int maxX = std::min(m_CanvasWidth - 1, px + (m_BrushSize - 1 - half));
        int minY = (std::max)(0, py - half);
        int maxY = std::min(m_CanvasHeight - 1, py + (m_BrushSize - 1 - half));

        ImVec2 cursorMin(renderPos.x + minX * m_Zoom, renderPos.y + minY * m_Zoom);
        ImVec2 cursorMax(renderPos.x + (maxX + 1) * m_Zoom, renderPos.y + (maxY + 1) * m_Zoom);
        drawList->AddRect(cursorMin, cursorMax, IM_COL32(255, 255, 255, 220), 0.0f, 0, 1.5f);

        if (isCanvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            bool firstClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            ApplyTool(px, py, firstClick);
        }
    } else if (isCanvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_LastPaintedX >= 0 && m_LastPaintedY >= 0) {
        // Sujetar al borde si se arrastra fuera del lienzo mientras se mantiene pulsado
        int px = static_cast<int>((mousePos.x - renderPos.x) / m_Zoom);
        int py = static_cast<int>((mousePos.y - renderPos.y) / m_Zoom);
        px = glm::clamp(px, 0, m_CanvasWidth - 1);
        py = glm::clamp(py, 0, m_CanvasHeight - 1);
        ApplyTool(px, py, false);
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_LastPaintedX = -1;
        m_LastPaintedY = -1;
    }

    ImGui::EndChild();
}

void PixelArtEditorPanel::RenderTimeline()
{
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Animacion (%d frames):", static_cast<int>(m_Frames.size()));

    ImGui::SameLine();
    if (ImGui::Button("+ Frame", ImVec2(70, 24))) {
        AddFrame(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicar", ImVec2(70, 24))) {
        AddFrame(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Eliminar", ImVec2(70, 24))) {
        RemoveFrame(m_CurrentFrameIdx);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| Fotogramas:");

    // Tira horizontal de frames
    ImGui::BeginChild("TimelineStrip", ImVec2(0, 48), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (size_t i = 0; i < m_Frames.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        bool isCurrent = (m_CurrentFrameIdx == static_cast<int>(i));

        if (isCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.45f, 1.0f));
        }

        std::string frameLabel = "F " + std::to_string(i);
        if (ImGui::Button(frameLabel.c_str(), ImVec2(48, 40))) {
            m_CurrentFrameIdx = static_cast<int>(i);
            UpdateTextureGL();
        }

        if (isCurrent) {
            ImGui::PopStyleColor(2);
        }

        ImGui::SameLine();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void PixelArtEditorPanel::RenderColorPalette()
{
    ImGui::Text("Color y Paleta");
    ImGui::Separator();

    ImGui::ColorEdit4("Actual", &m_CurrentColor.r, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

    ImGui::Separator();
    ImGui::Text("Paleta Retro:");

    int swatchesPerRow = 8;
    for (size_t i = 0; i < m_Palette.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::ColorButton("##swatch", ImVec4(m_Palette[i].r, m_Palette[i].g, m_Palette[i].b, m_Palette[i].a), 0, ImVec2(22, 22))) {
            m_CurrentColor = m_Palette[i];
        }
        if ((i + 1) % swatchesPerRow != 0 && (i + 1) < m_Palette.size()) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }
}

void PixelArtEditorPanel::RenderPreviewWindow()
{
    if (m_PreviewDirty || m_PreviewTexID == 0) {
        UpdatePreviewTextureGL();
        m_PreviewDirty = false;
    }

    ImGui::Text("Previsualizacion en Vivo");
    ImGui::Separator();

    // Actualizar animación de previsualización
    if (m_IsPlayingPreview && !m_Frames.empty()) {
        float dt = ImGui::GetIO().DeltaTime;
        m_PreviewTimer += dt;
        float frameDuration = 1.0f / (m_PreviewFPS > 0.0f ? m_PreviewFPS : 12.0f);

        if (m_PreviewTimer >= frameDuration) {
            m_PreviewTimer = 0.0f;
            m_PreviewCurrentFrame++;
            if (m_PreviewCurrentFrame >= static_cast<int>(m_Frames.size())) {
                m_PreviewCurrentFrame = m_LoopPreview ? 0 : static_cast<int>(m_Frames.size()) - 1;
            }
            UpdatePreviewTextureGL();
        }
    } else {
        m_PreviewCurrentFrame = m_CurrentFrameIdx;
        UpdatePreviewTextureGL();
    }

    // Ventana de preview cuadrada y nítida (140x140)
    ImVec2 previewSize(140.0f, 140.0f);
    ImVec2 pPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(pPos, ImVec2(pPos.x + previewSize.x, pPos.y + previewSize.y), IM_COL32(25, 25, 30, 255));

    // Fondo damero en el preview
    float checkSize = 10.0f;
    for (float cy = 0; cy < previewSize.y; cy += checkSize) {
        for (float cx = 0; cx < previewSize.x; cx += checkSize) {
            int ix = static_cast<int>(cx / checkSize);
            int iy = static_cast<int>(cy / checkSize);
            if ((ix + iy) % 2 == 0) {
                drawList->AddRectFilled(ImVec2(pPos.x + cx, pPos.y + cy),
                                        ImVec2(std::min(pPos.x + cx + checkSize, pPos.x + previewSize.x),
                                               std::min(pPos.y + cy + checkSize, pPos.y + previewSize.y)),
                                        IM_COL32(35, 35, 42, 255));
            }
        }
    }

    // Dibujar píxeles del frame actual en el preview de forma 100% nítida
    if (!m_Frames.empty() && m_PreviewCurrentFrame >= 0 && m_PreviewCurrentFrame < static_cast<int>(m_Frames.size())) {
        const auto& pixels = m_Frames[m_PreviewCurrentFrame].pixels;
        float pScale = previewSize.x / static_cast<float>(m_CanvasWidth);
        for (int y = 0; y < m_CanvasHeight; ++y) {
            for (int x = 0; x < m_CanvasWidth; ++x) {
                uint32_t col = pixels[y * m_CanvasWidth + x];
                if ((col >> 24) != 0) {
                    ImVec2 pMin(pPos.x + x * pScale, pPos.y + y * pScale);
                    ImVec2 pMax(pMin.x + pScale, pMin.y + pScale);
                    uint8_t r = col & 0xFF;
                    uint8_t g = (col >> 8) & 0xFF;
                    uint8_t b = (col >> 16) & 0xFF;
                    uint8_t a = (col >> 24) & 0xFF;
                    drawList->AddRectFilled(pMin, pMax, IM_COL32(r, g, b, a));
                }
            }
        }
    }
    drawList->AddRect(pPos, ImVec2(pPos.x + previewSize.x, pPos.y + previewSize.y), IM_COL32(110, 110, 125, 255));

    ImGui::Dummy(previewSize);

    // Controles de reproducción
    if (ImGui::Button(m_IsPlayingPreview ? "Pausar" : "Play", ImVec2(60, 24))) {
        m_IsPlayingPreview = !m_IsPlayingPreview;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_LoopPreview);

    ImGui::SliderFloat("FPS", &m_PreviewFPS, 1.0f, 60.0f, "%.0f");

    ImGui::Separator();
    ImGui::Text("Exportar a SlipEngine:");

    char animBuf[64];
    std::strncpy(animBuf, m_AnimationName.c_str(), sizeof(animBuf) - 1);
    animBuf[sizeof(animBuf) - 1] = '\0';
    if (ImGui::InputText("Nombre", animBuf, sizeof(animBuf))) {
        m_AnimationName = animBuf;
    }

    if (ImGui::Button("Bakear Spritesheet (.png)", ImVec2(210.0f, 30.0f))) {
        BakeSpritesheet("");
    }

    if (ImGui::Button("Asignar a Entidad Seleccionada", ImVec2(210.0f, 30.0f))) {
        AssignToSelectedEntity();
    }
}
