#pragma once

#ifndef SLIP_ANIMATOR_GRAPH_PANEL_H
#define SLIP_ANIMATOR_GRAPH_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/render/SlipAnimatorController.h"
#include "SlipEngine/scene/SlipComponent.h"
#include <memory>
#include <string>
#include <vector>
#include <imgui.h>

class AnimatorGraphPanel : public EditorPanel {
public:
    AnimatorGraphPanel();
    ~AnimatorGraphPanel() override;

    void OnImGuiRender() override;

    void OpenController(const std::string& filePath);
    void SetTargetEntity(SlipEntity* entity);

    static AnimatorGraphPanel* GetInstance() { return s_Instance; }

private:
    inline static AnimatorGraphPanel* s_Instance = nullptr;

    void RenderToolbar();
    void RenderParametersSidebar();
    void RenderGraphCanvas();
    void RenderPropertiesInspector();

    void DrawGrid(ImDrawList* drawList, const ImVec2& canvasP0, const ImVec2& canvasSize);
    void DrawStateNode(ImDrawList* drawList, const ImVec2& canvasP0, int stateIndex, AnimatorState& state, bool isActiveRuntime, float runtimeProgress);
    void DrawEntryNode(ImDrawList* drawList, const ImVec2& canvasP0, const ImVec2& entryPos, const ImVec2& defaultStatePos);
    void DrawTransitionArrow(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, bool isSelected, bool isActive, const char* label = nullptr);

    // Estado del Controlador
    std::shared_ptr<SlipAnimatorController> m_Controller;
    std::string m_CurrentFilePath;
    bool m_IsDirty = false;
    bool m_SyncWithEntity = true;

    // Navegación en el Canvas de Nodos
    ImVec2 m_CanvasOffset = ImVec2(250.0f, 200.0f);
    float m_Zoom = 1.0f;
    bool m_IsPanning = false;

    // Selección
    int m_SelectedStateIndex = -1;
    int m_SelectedTransSourceIdx = -1;
    int m_SelectedTransIdx = -1;

    // Arrastre de Nodos
    int m_DraggingStateIndex = -1;
    ImVec2 m_DragNodeStartPos = ImVec2(0.0f, 0.0f);

    // Creación interactiva de Transición con cable de conexión
    bool m_IsMakingTransition = false;
    int m_TransitionSourceStateIdx = -1;

    // Buffers temporales para UI
    char m_NewParamName[64] = "NuevoParametro";
    int m_NewParamType = 0; // 0=Float, 1=Int, 2=Bool, 3=Trigger
    char m_RenameBuffer[64] = "";
    int m_RenamingStateIndex = -1;

    // Tamaño base de nodos de estado
    const ImVec2 m_NodeBaseSize = ImVec2(180.0f, 65.0f);
};

#endif // !SLIP_ANIMATOR_GRAPH_PANEL_H

