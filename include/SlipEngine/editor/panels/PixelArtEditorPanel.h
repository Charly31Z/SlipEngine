#pragma once

#ifndef PIXEL_ART_EDITOR_PANEL_H
#define PIXEL_ART_EDITOR_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

enum class PixelTool {
    Pencil = 0,
    Eraser = 1,
    BucketFill = 2,
    Eyedropper = 3
};

struct PixelArtFrame {
    std::vector<uint32_t> pixels; // Formato RGBA 0xAABBGGRR o 0xRRGGBBAA
};

class PixelArtEditorPanel : public EditorPanel
{
public:
    PixelArtEditorPanel();
    ~PixelArtEditorPanel() override;

    void OnImGuiRender() override;

    // Gestión de resolución y frames
    void SetResolution(int width, int height);
    void AddFrame(bool duplicateCurrent = false);
    void RemoveFrame(int index);
    void ClearCurrentFrame();

    // Guardado y Carga de Proyecto / Metadatos (.slippixel)
    bool SaveProject(const std::string& filePath);
    bool LoadProject(const std::string& filePath);

    // Bake y Exportación de Spritesheet (.png)
    bool BakeSpritesheet(const std::string& pngFilePath);
    void AssignToSelectedEntity();

private:
    void RenderCanvas();
    void RenderToolbar();
    void RenderColorPalette();
    void RenderTimeline();
    void RenderPreviewWindow();

    void ApplyTool(int px, int py, bool isFirstClick);
    void PaintAt(int px, int py);
    void PaintLine(int x0, int y0, int x1, int y1);
    void FloodFill(int startX, int startY, uint32_t targetCol, uint32_t fillCol);

    void UpdateTextureGL();
    void UpdatePreviewTextureGL();

    // Propiedades del lienzo
    int m_CanvasWidth = 32;
    int m_CanvasHeight = 32;
    float m_Zoom = 16.0f;
    glm::vec2 m_PanOffset = glm::vec2(0.0f, 0.0f);
    bool m_ShowGrid = true;
    bool m_ShowCheckerboard = true;

    // Herramientas y color
    PixelTool m_CurrentTool = PixelTool::Pencil;
    int m_BrushSize = 1;
    glm::vec4 m_CurrentColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    std::vector<glm::vec4> m_Palette;
    bool m_IsPainting = false;
    int m_LastPaintedX = -1;
    int m_LastPaintedY = -1;

    // Animación por dibujo y frames
    std::vector<PixelArtFrame> m_Frames;
    int m_CurrentFrameIdx = 0;

    // Onion Skinning (Piel de Cebolla / Fantasmas)
    bool m_OnionSkinning = true;
    float m_OnionPrevOpacity = 0.35f;
    float m_OnionNextOpacity = 0.35f;
    glm::vec4 m_OnionPrevTint = glm::vec4(1.0f, 0.25f, 0.25f, 0.35f); // Fantasma anterior (Rojizo)
    glm::vec4 m_OnionNextTint = glm::vec4(0.25f, 0.65f, 1.0f, 0.35f); // Fantasma posterior (Cian/Azul)

    // Reproducción en tiempo real
    bool m_IsPlayingPreview = true;
    float m_PreviewFPS = 12.0f;
    float m_PreviewTimer = 0.0f;
    int m_PreviewCurrentFrame = 0;
    bool m_LoopPreview = true;

    // Rutas de archivo y caché
    std::string m_PanelTitle = "Editor de Sprites Pixel Art";
    std::string m_CurrentProjectPath;
    std::string m_ExportPngPath;
    std::string m_AnimationName = "PixelAnim";

    // Texturas OpenGL para visualización ImGui
    uint32_t m_CanvasTexID = 0;
    uint32_t m_PreviewTexID = 0;
    bool m_CanvasDirty = true;
    bool m_PreviewDirty = true;
    std::vector<uint32_t> m_DisplayBuffer; // Buffer RGBA compuesto con onion skinning

    // Undo / Redo básico
    std::vector<std::vector<uint32_t>> m_UndoStack;
    void PushUndo();
};

#endif // !PIXEL_ART_EDITOR_PANEL_H
