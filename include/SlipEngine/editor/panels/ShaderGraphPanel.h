#pragma once

#ifndef SLIP_SHADER_GRAPH_PANEL_H
#define SLIP_SHADER_GRAPH_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderGraphTypes.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <imgui.h>
#include <imgui_internal.h>

class SlipShader;

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif

class ShaderGraphPanel : public EditorPanel {
public:
    ShaderGraphPanel();
    ~ShaderGraphPanel() override;

    void OnImGuiRender() override;

    void OpenGraph(const std::string& filepath);
    void NewGraph();
    void SaveGraph(const std::string& filepath);

    static ShaderGraphPanel* GetInstance() { return s_Instance; }

private:
    inline static ShaderGraphPanel* s_Instance = nullptr;

    // Secciones de la interfaz
    void RenderToolbar();
    void RenderBlackboard();
    void RenderGraphCanvas();
    void RenderNodeInspector(const std::string& assetsDir);
    void RenderPreviewOverlay(const ImVec2& canvasP0, const ImVec2& canvasSize);

    // Utilidades de dibujo del canvas
    float GetNodeWidth() const { return 190.0f; }
    float GetNodeHeightForPorts(int portCount) const;
    ImU32 TypeToColor(SlipShaderGraph::PinType type) const;

    ImVec2 NodeTopLeft(const ImVec2& canvasP0, const SlipShaderGraph::ShaderNode& node) const;
    ImVec2 NodeSize(const SlipShaderGraph::ShaderNode& node) const;
    ImVec2 InputPortPos(const ImVec2& canvasP0, const SlipShaderGraph::ShaderNode& node, int index) const;
    ImVec2 OutputPortPos(const ImVec2& canvasP0, const SlipShaderGraph::ShaderNode& node, int index) const;
    SlipShaderGraph::ShaderNode* FindNodeById(const std::string& id) const;

    // Editor de nodos concretos
    void RenderConstantNodeEditor(SlipShaderGraph::ShaderNode* node);
    void RenderPropertyNodeEditor(SlipShaderGraph::ShaderNode* node);
    void RenderUnconnectedPortDefaults(SlipShaderGraph::ShaderNode* node);

    // Preview de material
    void RefreshPreviewShader();
    void EnsurePreviewResources();
    void ResizePreviewFBO(int width, int height);
    void RenderPreviewFrame(const ImVec2& bodyMin, const ImVec2& bodyMax);
    void BindPreviewUniforms(SlipShader* shader, const glm::mat4& model, float aspect, float time);

    // --- Previews por nodo (estilo Unity Shader Graph) ---
    struct NodePreviewCache {
        SlipShader* shader = nullptr;   // programa GL del preview de este nodo
        GLuint tex = 0;
        GLuint fbo = 0;
        GLuint rbo = 0;
        int size = 128;
        bool valid = false;
        std::string error;
        int version = -1;
    };

    void InvalidateNodePreviews();
    void UpdateNextNodePreview();
    static void ReleaseNodePreview(NodePreviewCache& entry);
    void EnsureNodePreviewFBO(NodePreviewCache& entry);
    void RenderNodePreviews();
    void RenderExpandedPreviewWindow(const ImVec2& canvasP0, ImDrawList* drawList);

    // Ventana expandida de preview de un nodo (minimizada: swatch en la cabecera)
    std::string m_ExpandedPreviewNodeId;
    ImRect m_ExpandedPreviewRect = ImRect(0, 0, 0, 0);
    ImVec2 m_ExpandedPreviewOffset = ImVec2(0.0f, 0.0f);
    bool m_DraggingExpandedPreview = false;

    // --- Estado del Grafo ---
    SlipShaderGraph::ShaderGraph m_Graph;
    std::string m_CurrentFilePath;
    bool m_GraphDirty = false;

    // NavegaciÃ³n del canvas
    ImVec2 m_CanvasOffset = ImVec2(250.0f, 200.0f);
    float m_Zoom = 1.0f;
    bool m_IsPanning = false;
    ImVec2 m_LastCanvasOrigin = ImVec2(0.0f, 0.0f);
    ImVec2 m_LastCanvasSize = ImVec2(0.0f, 0.0f);

    // SelecciÃ³n / arrastre de nodos
    std::string m_SelectedNodeId;
    std::string m_DraggingNodeId;

    // CreaciÃ³n interactiva de links entre pines
    bool m_IsLinking = false;
    std::string m_LinkFromNodeId;
    std::string m_LinkFromPortId;
    bool m_LinkFromIsOutput = false;
    SlipShaderGraph::PinType m_LinkPinType = SlipShaderGraph::PinType::Float;

    // DetecciÃ³n de link bajo el cursor
    std::string m_HoveredLinkId;

    // Buffers temporales para UI
    char m_NodeSearchBuf[64] = "";
    int m_SelectedPropertyIndex = -1;

    // --- Preview de Material ---
    bool m_ShowPreview = true;
    float m_PreviewOffsetRight = 12.0f;
    float m_PreviewOffsetTop = 10.0f;
    ImVec2 m_PreviewSize = ImVec2(230.0f, 230.0f);
    const ImVec2 m_PreviewHeaderSize = ImVec2(0.0f, 22.0f);
    bool m_DraggingPreview = false;
    bool m_PreviewTurntable = true;
    float m_PreviewYaw = 40.0f;
    float m_PreviewPitch = 25.0f;
    float m_PreviewDist = 2.6f;
    float m_PreviewTime = 0.0f;

    GLuint m_PreviewFBO = 0;
    GLuint m_PreviewColorTex = 0;
    GLuint m_PreviewDepthRBO = 0;
    int m_PreviewW = 0;
    int m_PreviewH = 0;

    std::shared_ptr<SlipShader> m_PreviewShader;
    bool m_PreviewValid = false;
    std::string m_PreviewError;

    GLuint m_SphereVAO = 0;
    GLuint m_SphereVBO = 0;
    GLuint m_SphereEBO = 0;
    unsigned int m_SphereIndexCount = 0;
    bool m_SphereInitialized = false;

    GLuint m_WhiteTex = 0;
    bool m_WhiteTexInitialized = false;

    // Estado de previews por nodo
    std::unordered_map<std::string, NodePreviewCache> m_NodePreviews;
    bool m_ShowNodePreviews = true;
    bool m_NodePreviewsDirty = true;
    bool m_LastGraphDirtyState = false;
    int m_PreviewVersion = 0;
    float m_PreviewStableTime = 0.0f;
    float m_NodePreviewTime = 0.0f;
    float m_ExpandedPreviewSize = 170.0f;

    // RectÃ¡ngulo activo del preview (para evitar interacciÃ³n del canvas debajo)
    ImRect m_PreviewRect = ImRect(0, 0, 0, 0);
};

#endif // !SLIP_SHADER_GRAPH_PANEL_H
