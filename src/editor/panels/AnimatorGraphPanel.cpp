#include "SlipEngine/editor/panels/AnimatorGraphPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/editor/SlipFileDialog.h"
#include "SlipEngine/editor/panels/ContentBrowserPanel.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include <algorithm>
#include <cmath>

AnimatorGraphPanel::AnimatorGraphPanel() {
    s_Instance = this;
    m_Controller = std::make_shared<SlipAnimatorController>();
    SetOpen(false);
}

AnimatorGraphPanel::~AnimatorGraphPanel() {
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
}

void AnimatorGraphPanel::OpenController(const std::string& filePath) {
    if (filePath.empty()) return;
    auto newCtrl = std::make_shared<SlipAnimatorController>();
    if (newCtrl->LoadFromFile(filePath)) {
        m_Controller = newCtrl;
        m_CurrentFilePath = filePath;
        m_IsDirty = false;
        m_SelectedStateIndex = -1;
        m_SelectedTransIdx = -1;
        m_SelectedTransSourceIdx = -1;
        SetOpen(true);
        SlipConsole::LogSuccess("AnimatorController cargado en editor: " + filePath);
    } else {
        SlipConsole::LogError("Error al cargar AnimatorController: " + filePath);
    }
}

void AnimatorGraphPanel::SetTargetEntity(SlipEntity* entity) {
    SetSelectedEntity(entity);
    if (!m_SyncWithEntity || !entity) return;

    if (auto anim = entity->GetComponent<AnimatorComponent>()) {
        if (anim->controller) {
            m_Controller = anim->controller;
            m_CurrentFilePath = anim->controllerPath;
        } else if (!anim->controllerPath.empty()) {
            OpenController(anim->controllerPath);
        }
    }
}

void AnimatorGraphPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    // Sincronizar automáticamente con la entidad seleccionada en la jerarquía si está habilitado
    if (m_SyncWithEntity && m_SelectedEntity) {
        if (auto anim = m_SelectedEntity->GetComponent<AnimatorComponent>()) {
            if (anim->controller && anim->controller != m_Controller) {
                m_Controller = anim->controller;
                m_CurrentFilePath = anim->controllerPath;
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(950, 600), ImGuiCond_FirstUseEver);
    std::string winTitle = "Controlador de Animacion (Animator)";
    if (!m_CurrentFilePath.empty()) {
        winTitle += " - " + SlipFileSystem::GetFileName(m_CurrentFilePath);
        if (m_IsDirty) winTitle += "*";
    }
    winTitle += "###AnimatorGraphWindow";

    if (!ImGui::Begin(winTitle.c_str(), &m_IsOpen)) {
        ImGui::End();
        return;
    }

    RenderToolbar();
    ImGui::Separator();

    // Disposición principal en 3 columnas: [Parámetros] | [Grafo de Nodos] | [Inspector de Propiedades]
    float totalWidth = ImGui::GetContentRegionAvail().x;
    float sidebarWidth = 220.0f;
    float inspectorWidth = 270.0f;
    float canvasWidth = totalWidth - sidebarWidth - inspectorWidth - 16.0f;
    if (canvasWidth < 300.0f) {
        inspectorWidth = 220.0f;
        canvasWidth = totalWidth - sidebarWidth - inspectorWidth - 16.0f;
    }

    // 1. Columna Izquierda: Parámetros y Capas
    ImGui::BeginChild("ParamsSidebar", ImVec2(sidebarWidth, 0), true);
    RenderParametersSidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // 2. Columna Central: Grafo de Nodos (Canvas interactivo)
    ImGui::BeginChild("GraphCanvasArea", ImVec2(canvasWidth, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    RenderGraphCanvas();
    ImGui::EndChild();

    ImGui::SameLine();

    // 3. Columna Derecha: Inspector de Estado / Transición Seleccionada
    ImGui::BeginChild("InspectorSidebar", ImVec2(inspectorWidth, 0), true);
    RenderPropertiesInspector();
    ImGui::EndChild();

    ImGui::End();
}

void AnimatorGraphPanel::RenderToolbar() {
    if (ImGui::Button("[ Guardar ]")) {
        if (m_Controller) {
            std::string savePath = m_CurrentFilePath;
            if (savePath.empty()) {
                savePath = "assets/Animations/" + m_Controller->name + ".controller";
            }
            if (m_Controller->SaveToFile(savePath)) {
                m_CurrentFilePath = savePath;
                m_IsDirty = false;
                SlipConsole::LogSuccess("AnimatorController guardado: " + savePath);
            }
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Abrir...")) {
        SlipFileDialog::Instance().Open("OpenAnimatorController", "Seleccionar Animator Controller", ".controller", "assets/Animations", SlipFileDialogType::OpenFile);
    }
    if (SlipFileDialog::Instance().HasSelected() && SlipFileDialog::Instance().GetCurrentDialogKey() == "OpenAnimatorController") {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        if (!selected.empty()) {
            OpenController(selected);
        }
        SlipFileDialog::Instance().ClearSelection();
    }
    ImGui::SameLine();

    if (ImGui::Button("+ Nuevo")) {
        m_Controller = std::make_shared<SlipAnimatorController>();
        m_Controller->name = "NuevoController";
        m_Controller->AddState("Idle");
        m_CurrentFilePath = "assets/Animations/NuevoController.controller";
        m_IsDirty = true;
    }
    ImGui::SameLine();

    ImGui::Checkbox("Sincronizar con Seleccion", &m_SyncWithEntity);
    ImGui::SameLine();

    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (!m_CurrentFilePath.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", SlipFileSystem::GetFileName(m_CurrentFilePath).c_str());
    } else {
        ImGui::TextDisabled("Sin archivo guardado");
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
    if (ImGui::SmallButton("Centrar Vista")) {
        m_CanvasOffset = ImVec2(250.0f, 200.0f);
        m_Zoom = 1.0f;
    }
}

void AnimatorGraphPanel::RenderParametersSidebar() {
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "PARAMETROS");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
    if (ImGui::SmallButton("+##AddParamBtn")) {
        ImGui::OpenPopup("AddParamPopup");
    }

    if (ImGui::BeginPopup("AddParamPopup")) {
        if (ImGui::MenuItem("Float")) {
            std::string pName = "NuevoFloat";
            int count = 1;
            while (m_Controller->HasParameter(pName)) { pName = "NuevoFloat_" + std::to_string(count++); }
            m_Controller->AddParameter(pName, AnimatorParamType::Float);
            m_IsDirty = true;
        }
        if (ImGui::MenuItem("Int")) {
            std::string pName = "NuevoInt";
            int count = 1;
            while (m_Controller->HasParameter(pName)) { pName = "NuevoInt_" + std::to_string(count++); }
            m_Controller->AddParameter(pName, AnimatorParamType::Int);
            m_IsDirty = true;
        }
        if (ImGui::MenuItem("Bool")) {
            std::string pName = "NuevoBool";
            int count = 1;
            while (m_Controller->HasParameter(pName)) { pName = "NuevoBool_" + std::to_string(count++); }
            m_Controller->AddParameter(pName, AnimatorParamType::Bool);
            m_IsDirty = true;
        }
        if (ImGui::MenuItem("Trigger")) {
            std::string pName = "NuevoTrigger";
            int count = 1;
            while (m_Controller->HasParameter(pName)) { pName = "NuevoTrigger_" + std::to_string(count++); }
            m_Controller->AddParameter(pName, AnimatorParamType::Trigger);
            m_IsDirty = true;
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    std::string paramToDelete = "";
    for (auto& pair : m_Controller->parameters) {
        auto& p = pair.second;
        ImGui::PushID(p.name.c_str());

        // Badge de tipo con colores intuitivos estilo Unity
        ImVec4 badgeColor(0.2f, 0.6f, 1.0f, 1.0f);
        const char* typeTag = "F";
        if (p.type == AnimatorParamType::Int) { badgeColor = ImVec4(0.3f, 0.8f, 0.4f, 1.0f); typeTag = "I"; }
        else if (p.type == AnimatorParamType::Bool) { badgeColor = ImVec4(0.8f, 0.4f, 0.9f, 1.0f); typeTag = "B"; }
        else if (p.type == AnimatorParamType::Trigger) { badgeColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f); typeTag = "T"; }

        ImGui::TextColored(badgeColor, "[%s]", typeTag);
        ImGui::SameLine();

        // Nombre del parámetro
        ImGui::Text("%s", p.name.c_str());

        // Controles de valor
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 65.0f);
        ImGui::SetNextItemWidth(45.0f);
        if (p.type == AnimatorParamType::Float) {
            if (ImGui::DragFloat("##fval", &p.floatValue, 0.05f, 0.0f, 0.0f, "%.2f")) m_IsDirty = true;
        } else if (p.type == AnimatorParamType::Int) {
            if (ImGui::DragInt("##ival", &p.intValue, 1.0f)) m_IsDirty = true;
        } else if (p.type == AnimatorParamType::Bool) {
            if (ImGui::Checkbox("##bval", &p.boolValue)) m_IsDirty = true;
        } else if (p.type == AnimatorParamType::Trigger) {
            if (ImGui::RadioButton("##tval", p.boolValue)) {
                p.boolValue = !p.boolValue;
                m_IsDirty = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("x##del")) {
            paramToDelete = p.name;
        }

        ImGui::PopID();
    }

    if (!paramToDelete.empty()) {
        m_Controller->RemoveParameter(paramToDelete);
        m_IsDirty = true;
    }
}

void AnimatorGraphPanel::RenderGraphCanvas() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);
    ImGuiIO& io = ImGui::GetIO();

    // Recortar dibujo al área del canvas
    drawList->PushClipRect(canvasP0, canvasP1, true);

    // 1. Dibujar Cuadrícula de Fondo estilo Blueprint / Unity Animator
    DrawGrid(drawList, canvasP0, canvasSize);

    // 2. Manejo de Panning (Clic derecho o Clic central arrastrando)
    bool isCanvasHovered = ImGui::IsWindowHovered();
    if (isCanvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        m_CanvasOffset.x += io.MouseDelta.x;
        m_CanvasOffset.y += io.MouseDelta.y;
    }

    // 3. Zoom con la rueda del ratón
    if (isCanvasHovered && io.MouseWheel != 0.0f) {
        float prevZoom = m_Zoom;
        m_Zoom = std::clamp(m_Zoom + io.MouseWheel * 0.08f, 0.5f, 1.8f);
        ImVec2 mousePosCanvas = ImVec2((io.MousePos.x - canvasP0.x - m_CanvasOffset.x) / prevZoom,
                                       (io.MousePos.y - canvasP0.y - m_CanvasOffset.y) / prevZoom);
        m_CanvasOffset.x = io.MousePos.x - canvasP0.x - mousePosCanvas.x * m_Zoom;
        m_CanvasOffset.y = io.MousePos.y - canvasP0.y - mousePosCanvas.y * m_Zoom;
    }

    // 4. Determinar estado y transición activos en runtime (si está en Play Mode)
    int runtimeActiveStateIdx = m_Controller ? m_Controller->currentStateIndex : -1;
    bool isTransitioning = m_Controller ? m_Controller->isTransitioning : false;
    int runtimeTargetStateIdx = m_Controller ? m_Controller->targetStateIndex : -1;
    float runtimeProgress = m_Controller ? m_Controller->transitionProgress : 0.0f;

    // 5. Dibujar Transiciones entre estados (Líneas y flechas direccionales)
    for (size_t sIdx = 0; sIdx < m_Controller->states.size(); ++sIdx) {
        const auto& state = m_Controller->states[sIdx];
        ImVec2 startCenter = ImVec2(canvasP0.x + m_CanvasOffset.x + (state.posX + m_NodeBaseSize.x * 0.5f) * m_Zoom,
                                    canvasP0.y + m_CanvasOffset.y + (state.posY + m_NodeBaseSize.y * 0.5f) * m_Zoom);

        for (size_t tIdx = 0; tIdx < state.transitions.size(); ++tIdx) {
            const auto& trans = state.transitions[tIdx];
            int destIdx = m_Controller->GetStateIndex(trans.destinationState);
            if (destIdx < 0 || destIdx >= static_cast<int>(m_Controller->states.size())) continue;

            const auto& destState = m_Controller->states[destIdx];
            ImVec2 endCenter = ImVec2(canvasP0.x + m_CanvasOffset.x + (destState.posX + m_NodeBaseSize.x * 0.5f) * m_Zoom,
                                      canvasP0.y + m_CanvasOffset.y + (destState.posY + m_NodeBaseSize.y * 0.5f) * m_Zoom);

            bool isSelected = (m_SelectedTransSourceIdx == static_cast<int>(sIdx) && m_SelectedTransIdx == static_cast<int>(tIdx));
            bool isActive = (isTransitioning && static_cast<int>(sIdx) == runtimeActiveStateIdx && destIdx == runtimeTargetStateIdx);

            DrawTransitionArrow(drawList, startCenter, endCenter, isSelected, isActive);

            // Marcador interactivo en el punto medio de la flecha
            ImVec2 mid = ImVec2((startCenter.x + endCenter.x) * 0.5f, (startCenter.y + endCenter.y) * 0.5f);
            float markerR = 10.0f * m_Zoom;
            bool isHoveredMarker = (io.MousePos.x >= mid.x - markerR && io.MousePos.x <= mid.x + markerR &&
                                    io.MousePos.y >= mid.y - markerR && io.MousePos.y <= mid.y + markerR);

            if (isHoveredMarker && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_SelectedTransSourceIdx = static_cast<int>(sIdx);
                m_SelectedTransIdx = static_cast<int>(tIdx);
                m_SelectedStateIndex = -1;
            }
        }
    }

    // 6. Dibujar Nodo Especial ENTRY
    ImVec2 entryPos = ImVec2(canvasP0.x + m_CanvasOffset.x + 30.0f * m_Zoom, canvasP0.y + m_CanvasOffset.y + 150.0f * m_Zoom);
    ImVec2 defaultStatePos = entryPos;
    int defIdx = m_Controller->GetStateIndex(m_Controller->defaultState);
    if (defIdx >= 0 && defIdx < static_cast<int>(m_Controller->states.size())) {
        defaultStatePos = ImVec2(canvasP0.x + m_CanvasOffset.x + m_Controller->states[defIdx].posX * m_Zoom,
                                 canvasP0.y + m_CanvasOffset.y + (m_Controller->states[defIdx].posY + m_NodeBaseSize.y * 0.5f) * m_Zoom);
    }
    DrawEntryNode(drawList, canvasP0, entryPos, defaultStatePos);

    // 7. Dibujar Nodos de Estados
    int clickedStateIdx = -1;
    for (size_t i = 0; i < m_Controller->states.size(); ++i) {
        auto& state = m_Controller->states[i];
        bool isActiveRuntime = (runtimeActiveStateIdx == static_cast<int>(i));

        DrawStateNode(drawList, canvasP0, static_cast<int>(i), state, isActiveRuntime, runtimeProgress);

        // Detección de selección y arrastre sobre el nodo
        ImVec2 nodeMin = ImVec2(canvasP0.x + m_CanvasOffset.x + state.posX * m_Zoom,
                                canvasP0.y + m_CanvasOffset.y + state.posY * m_Zoom);
        ImVec2 nodeMax = ImVec2(nodeMin.x + m_NodeBaseSize.x * m_Zoom, nodeMin.y + m_NodeBaseSize.y * m_Zoom);

        if (io.MousePos.x >= nodeMin.x && io.MousePos.x <= nodeMax.x &&
            io.MousePos.y >= nodeMin.y && io.MousePos.y <= nodeMax.y) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                clickedStateIdx = static_cast<int>(i);
                if (m_IsMakingTransition) {
                    // Completar creación de transición interactiva
                    if (m_TransitionSourceStateIdx >= 0 && m_TransitionSourceStateIdx != static_cast<int>(i)) {
                        m_Controller->AddTransition(m_Controller->states[m_TransitionSourceStateIdx].name, state.name);
                        m_IsDirty = true;
                    }
                    m_IsMakingTransition = false;
                    m_TransitionSourceStateIdx = -1;
                } else {
                    m_SelectedStateIndex = static_cast<int>(i);
                    m_SelectedTransIdx = -1;
                    m_SelectedTransSourceIdx = -1;
                    m_DraggingStateIndex = static_cast<int>(i);
                }
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_SelectedStateIndex = static_cast<int>(i);
                ImGui::OpenPopup(("StateContext_" + std::to_string(i)).c_str());
            }
        }

        // Menú contextual sobre nodo de estado
        if (ImGui::BeginPopup(("StateContext_" + std::to_string(i)).c_str())) {
            if (ImGui::MenuItem("Crear Transicion (Make Transition)")) {
                m_IsMakingTransition = true;
                m_TransitionSourceStateIdx = static_cast<int>(i);
            }
            if (ImGui::MenuItem("Establecer como Estado por Defecto")) {
                m_Controller->defaultState = state.name;
                m_IsDirty = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar Estado")) {
                m_Controller->RemoveState(state.name);
                m_SelectedStateIndex = -1;
                m_IsDirty = true;
            }
            ImGui::EndPopup();
        }
    }

    // 8. Manejo de arrastre de nodo
    if (m_DraggingStateIndex >= 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_Controller->states[m_DraggingStateIndex].posX += io.MouseDelta.x / m_Zoom;
            m_Controller->states[m_DraggingStateIndex].posY += io.MouseDelta.y / m_Zoom;
            m_IsDirty = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_DraggingStateIndex = -1;
        }
    }

    // 9. Si estamos creando una transición interactiva, dibujar cable hasta el ratón
    if (m_IsMakingTransition && m_TransitionSourceStateIdx >= 0 && m_TransitionSourceStateIdx < static_cast<int>(m_Controller->states.size())) {
        const auto& src = m_Controller->states[m_TransitionSourceStateIdx];
        ImVec2 srcCenter = ImVec2(canvasP0.x + m_CanvasOffset.x + (src.posX + m_NodeBaseSize.x * 0.5f) * m_Zoom,
                                  canvasP0.y + m_CanvasOffset.y + (src.posY + m_NodeBaseSize.y * 0.5f) * m_Zoom);
        drawList->AddLine(srcCenter, io.MousePos, IM_COL32(255, 200, 50, 240), 2.5f * m_Zoom);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_IsMakingTransition = false;
            m_TransitionSourceStateIdx = -1;
        }
    }

    // 10. Clic derecho en espacio vacío del canvas: Menú contextual del Grafo
    if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_SelectedStateIndex < 0 && !m_IsMakingTransition) {
        ImGui::OpenPopup("CanvasContextMenu");
    }
    if (ImGui::BeginPopup("CanvasContextMenu")) {
        if (ImGui::MenuItem("Crear Estado Vacio")) {
            ImVec2 spawnPos = ImVec2((io.MousePos.x - canvasP0.x - m_CanvasOffset.x) / m_Zoom,
                                     (io.MousePos.y - canvasP0.y - m_CanvasOffset.y) / m_Zoom);
            std::string sName = "NuevoEstado";
            int count = 1;
            while (m_Controller->GetState(sName)) { sName = "NuevoEstado_" + std::to_string(count++); }
            int newIdx = m_Controller->AddState(sName);
            m_Controller->states[newIdx].posX = spawnPos.x;
            m_Controller->states[newIdx].posY = spawnPos.y;
            m_SelectedStateIndex = newIdx;
            m_IsDirty = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Centrar Vista")) {
            m_CanvasOffset = ImVec2(250.0f, 200.0f);
            m_Zoom = 1.0f;
        }
        ImGui::EndPopup();
    }

    drawList->PopClipRect();
}

void AnimatorGraphPanel::DrawGrid(ImDrawList* drawList, const ImVec2& canvasP0, const ImVec2& canvasSize) {
    ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);
    drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(28, 30, 36, 255));

    float gridSize = 64.0f * m_Zoom;
    for (float x = std::fmod(m_CanvasOffset.x, gridSize); x < canvasSize.x; x += gridSize) {
        drawList->AddLine(ImVec2(canvasP0.x + x, canvasP0.y), ImVec2(canvasP0.x + x, canvasP1.y), IM_COL32(40, 44, 52, 200), 1.0f);
    }
    for (float y = std::fmod(m_CanvasOffset.y, gridSize); y < canvasSize.y; y += gridSize) {
        drawList->AddLine(ImVec2(canvasP0.x, canvasP0.y + y), ImVec2(canvasP1.x, canvasP0.y + y), IM_COL32(40, 44, 52, 200), 1.0f);
    }
}

void AnimatorGraphPanel::DrawEntryNode(ImDrawList* drawList, const ImVec2& canvasP0, const ImVec2& entryPos, const ImVec2& defaultStatePos) {
    ImVec2 sz = ImVec2(80.0f * m_Zoom, 32.0f * m_Zoom);
    ImVec2 pMax = ImVec2(entryPos.x + sz.x, entryPos.y + sz.y);

    // Fondo verde estilo Unity Entry Node
    drawList->AddRectFilled(entryPos, pMax, IM_COL32(40, 140, 80, 255), 4.0f * m_Zoom);
    drawList->AddRect(entryPos, pMax, IM_COL32(60, 200, 120, 255), 4.0f * m_Zoom, 0, 1.5f);

    ImVec2 textPos = ImVec2(entryPos.x + 16.0f * m_Zoom, entryPos.y + 8.0f * m_Zoom);
    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), "Entry");

    // Flecha naranja apuntando al DefaultState
    if (defaultStatePos.x != entryPos.x || defaultStatePos.y != entryPos.y) {
        ImVec2 start = ImVec2(entryPos.x + sz.x, entryPos.y + sz.y * 0.5f);
        drawList->AddLine(start, defaultStatePos, IM_COL32(230, 140, 40, 255), 2.5f * m_Zoom);

        // Punta de flecha
        glm::vec2 dir(defaultStatePos.x - start.x, defaultStatePos.y - start.y);
        float len = std::hypot(dir.x, dir.y);
        if (len > 1e-4f) {
            dir.x /= len; dir.y /= len;
            glm::vec2 perp(-dir.y, dir.x);
            float arrowSz = 8.0f * m_Zoom;
            ImVec2 p1 = defaultStatePos;
            ImVec2 p2 = ImVec2(defaultStatePos.x - dir.x * arrowSz + perp.x * (arrowSz * 0.6f),
                               defaultStatePos.y - dir.y * arrowSz + perp.y * (arrowSz * 0.6f));
            ImVec2 p3 = ImVec2(defaultStatePos.x - dir.x * arrowSz - perp.x * (arrowSz * 0.6f),
                               defaultStatePos.y - dir.y * arrowSz - perp.y * (arrowSz * 0.6f));
            drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(230, 140, 40, 255));
        }
    }
}

void AnimatorGraphPanel::DrawStateNode(ImDrawList* drawList, const ImVec2& canvasP0, int stateIndex, AnimatorState& state, bool isActiveRuntime, float runtimeProgress) {
    ImVec2 nodeMin = ImVec2(canvasP0.x + m_CanvasOffset.x + state.posX * m_Zoom,
                            canvasP0.y + m_CanvasOffset.y + state.posY * m_Zoom);
    ImVec2 nodeMax = ImVec2(nodeMin.x + m_NodeBaseSize.x * m_Zoom, nodeMin.y + m_NodeBaseSize.y * m_Zoom);
    float rounding = 6.0f * m_Zoom;

    bool isDefault = (state.name == m_Controller->defaultState);
    bool isSelected = (m_SelectedStateIndex == stateIndex);

    // Color del encabezado: Naranja si es Default State (estilo Unity), Azul pizarra si es normal
    ImU32 headerCol = isDefault ? IM_COL32(200, 110, 30, 255) : IM_COL32(45, 60, 85, 255);
    ImU32 bodyCol = IM_COL32(35, 38, 46, 250);

    // Fondo cuerpo
    drawList->AddRectFilled(nodeMin, nodeMax, bodyCol, rounding);

    // Encabezado
    ImVec2 headerMax = ImVec2(nodeMax.x, nodeMin.y + 24.0f * m_Zoom);
    drawList->AddRectFilled(nodeMin, headerMax, headerCol, rounding, ImDrawFlags_RoundCornersTop);

    // Título / Nombre del estado
    ImVec2 titlePos = ImVec2(nodeMin.x + 8.0f * m_Zoom, nodeMin.y + 4.0f * m_Zoom);
    drawList->AddText(titlePos, IM_COL32(255, 255, 255, 255), state.name.c_str());

    // Clip asignado y velocidad
    std::string clipStr = state.clipName.empty() ? "(Ninguno)" : state.clipName;
    if (clipStr.size() > 18) clipStr = clipStr.substr(0, 16) + "..";
    ImVec2 clipPos = ImVec2(nodeMin.x + 8.0f * m_Zoom, nodeMin.y + 28.0f * m_Zoom);
    drawList->AddText(clipPos, IM_COL32(180, 190, 205, 255), clipStr.c_str());

    std::string speedStr = std::to_string(state.speed).substr(0, 3) + "x";
    if (state.isLooping) speedStr += " [L]";
    ImVec2 speedPos = ImVec2(nodeMin.x + 8.0f * m_Zoom, nodeMin.y + 45.0f * m_Zoom);
    drawList->AddText(speedPos, IM_COL32(140, 150, 165, 255), speedStr.c_str());

    // Borde: Amarillo brillante si está seleccionado, verde neón si está activo en runtime
    ImU32 borderCol = IM_COL32(65, 70, 85, 255);
    float borderThick = 1.5f * m_Zoom;
    if (isActiveRuntime) {
        borderCol = IM_COL32(60, 240, 120, 255);
        borderThick = 2.5f * m_Zoom;

        // Barra de progreso de animación en la base del nodo
        float progressWidth = (nodeMax.x - nodeMin.x) * std::clamp(runtimeProgress, 0.05f, 1.0f);
        drawList->AddRectFilled(ImVec2(nodeMin.x, nodeMax.y - 4.0f * m_Zoom),
                                ImVec2(nodeMin.x + progressWidth, nodeMax.y),
                                IM_COL32(60, 240, 120, 255), rounding, ImDrawFlags_RoundCornersBottom);
    } else if (isSelected) {
        borderCol = IM_COL32(255, 210, 40, 255);
        borderThick = 2.2f * m_Zoom;
    }
    drawList->AddRect(nodeMin, nodeMax, borderCol, rounding, 0, borderThick);
}

void AnimatorGraphPanel::DrawTransitionArrow(ImDrawList* drawList, const ImVec2& start, const ImVec2& end, bool isSelected, bool isActive, const char* label) {
    ImU32 col = IM_COL32(160, 170, 185, 220);
    float thickness = 1.8f * m_Zoom;

    if (isActive) {
        col = IM_COL32(60, 240, 120, 255);
        thickness = 3.0f * m_Zoom;
    } else if (isSelected) {
        col = IM_COL32(70, 180, 255, 255);
        thickness = 2.5f * m_Zoom;
    }

    // Dibujar línea principal
    drawList->AddLine(start, end, col, thickness);

    // Calcular dirección y punta de flecha en el centro de la línea
    glm::vec2 dir(end.x - start.x, end.y - start.y);
    float len = std::hypot(dir.x, dir.y);
    if (len > 1e-4f) {
        dir.x /= len; dir.y /= len;
        glm::vec2 perp(-dir.y, dir.x);
        ImVec2 mid = ImVec2((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
        float arrowSz = 10.0f * m_Zoom;

        ImVec2 p1 = ImVec2(mid.x + dir.x * (arrowSz * 0.5f), mid.y + dir.y * (arrowSz * 0.5f));
        ImVec2 p2 = ImVec2(mid.x - dir.x * (arrowSz * 0.5f) + perp.x * (arrowSz * 0.6f),
                           mid.y - dir.y * (arrowSz * 0.5f) + perp.y * (arrowSz * 0.6f));
        ImVec2 p3 = ImVec2(mid.x - dir.x * (arrowSz * 0.5f) - perp.x * (arrowSz * 0.6f),
                           mid.y - dir.y * (arrowSz * 0.5f) - perp.y * (arrowSz * 0.6f));

        drawList->AddTriangleFilled(p1, p2, p3, col);

        // Marcador circular interactivo
        drawList->AddCircleFilled(mid, 7.0f * m_Zoom, IM_COL32(40, 44, 52, 255));
        drawList->AddCircle(mid, 7.0f * m_Zoom, col, 0, 1.5f * m_Zoom);
    }
}

void AnimatorGraphPanel::RenderPropertiesInspector() {
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "INSPECTOR DE NODO");
    ImGui::Separator();

    // 1. Si hay una Transición seleccionada
    if (m_SelectedTransSourceIdx >= 0 && m_SelectedTransSourceIdx < static_cast<int>(m_Controller->states.size())) {
        auto& srcState = m_Controller->states[m_SelectedTransSourceIdx];
        if (m_SelectedTransIdx >= 0 && m_SelectedTransIdx < static_cast<int>(srcState.transitions.size())) {
            auto& trans = srcState.transitions[m_SelectedTransIdx];

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s -> %s", srcState.name.c_str(), trans.destinationState.c_str());
            ImGui::Separator();

            ImGui::Text("Configuracion de Transicion:");
            if (ImGui::DragFloat("Duracion (s)", &trans.duration, 0.05f, 0.0f, 5.0f, "%.2f s")) m_IsDirty = true;
            if (ImGui::Checkbox("Tiene Tiempo de Salida (Exit Time)", &trans.hasExitTime)) m_IsDirty = true;
            if (trans.hasExitTime) {
                if (ImGui::SliderFloat("Exit Time (0..1)", &trans.exitTime, 0.0f, 1.0f, "%.2f")) m_IsDirty = true;
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Condiciones:");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
            if (ImGui::SmallButton("+##AddCond")) {
                AnimatorCondition newCond;
                if (!m_Controller->parameters.empty()) {
                    newCond.parameter = m_Controller->parameters.begin()->first;
                }
                trans.conditions.push_back(newCond);
                m_IsDirty = true;
            }

            int condToDelete = -1;
            for (size_t c = 0; c < trans.conditions.size(); ++c) {
                auto& cond = trans.conditions[c];
                ImGui::PushID(static_cast<int>(c));

                // Selector de parámetro
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::BeginCombo("##pName", cond.parameter.c_str())) {
                    for (const auto& pPair : m_Controller->parameters) {
                        bool isSel = (pPair.first == cond.parameter);
                        if (ImGui::Selectable(pPair.first.c_str(), isSel)) {
                            cond.parameter = pPair.first;
                            m_IsDirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();

                // Selector de modo
                const char* modes[] = { ">", "<", "==", "!=", "True", "False" };
                int currentMode = static_cast<int>(cond.mode);
                ImGui::SetNextItemWidth(60.0f);
                if (ImGui::Combo("##mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                    cond.mode = static_cast<AnimatorConditionMode>(currentMode);
                    m_IsDirty = true;
                }
                ImGui::SameLine();

                // Valor de umbral si es modo numérico
                if (cond.mode != AnimatorConditionMode::IfTrue && cond.mode != AnimatorConditionMode::IfFalse) {
                    ImGui::SetNextItemWidth(50.0f);
                    if (ImGui::DragFloat("##thresh", &cond.thresholdFloat, 0.1f, 0.0f, 0.0f, "%.1f")) m_IsDirty = true;
                    ImGui::SameLine();
                }

                if (ImGui::SmallButton("x##delCond")) {
                    condToDelete = static_cast<int>(c);
                }

                ImGui::PopID();
            }

            if (condToDelete >= 0) {
                trans.conditions.erase(trans.conditions.begin() + condToDelete);
                m_IsDirty = true;
            }

            ImGui::Separator();
            if (ImGui::Button("Eliminar Transicion", ImVec2(-1, 26))) {
                srcState.transitions.erase(srcState.transitions.begin() + m_SelectedTransIdx);
                m_SelectedTransIdx = -1;
                m_SelectedTransSourceIdx = -1;
                m_IsDirty = true;
            }
            return;
        }
    }

    // 2. Si hay un Estado seleccionado
    if (m_SelectedStateIndex >= 0 && m_SelectedStateIndex < static_cast<int>(m_Controller->states.size())) {
        auto& state = m_Controller->states[m_SelectedStateIndex];

        // Nombre del Estado
        char nameBuf[64];
        strncpy(nameBuf, state.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
            m_Controller->RenameState(state.name, nameBuf);
            m_IsDirty = true;
        }

        // Clip de Animación asignado
        char clipBuf[128];
        strncpy(clipBuf, state.clipName.c_str(), sizeof(clipBuf) - 1);
        clipBuf[sizeof(clipBuf) - 1] = '\0';
        if (ImGui::InputText("Clip (.anim)", clipBuf, sizeof(clipBuf))) {
            state.clipName = clipBuf;
            m_IsDirty = true;
        }

        // Velocidad y Repetición en bucle
        if (ImGui::DragFloat("Velocidad", &state.speed, 0.05f, -5.0f, 5.0f, "%.2fx")) m_IsDirty = true;
        if (ImGui::Checkbox("Loop (Repetir)", &state.isLooping)) m_IsDirty = true;

        bool isDefault = (state.name == m_Controller->defaultState);
        if (ImGui::Checkbox("Estado por Defecto", &isDefault)) {
            if (isDefault) m_Controller->defaultState = state.name;
            m_IsDirty = true;
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Transiciones Salientes:");
        for (size_t t = 0; t < state.transitions.size(); ++t) {
            auto& tr = state.transitions[t];
            ImGui::PushID(static_cast<int>(t));
            if (ImGui::Selectable(("-> " + tr.destinationState).c_str(), false)) {
                m_SelectedTransSourceIdx = m_SelectedStateIndex;
                m_SelectedTransIdx = static_cast<int>(t);
                m_SelectedStateIndex = -1;
                ImGui::PopID();
                return;
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("+ Nueva Transicion", ImVec2(-1, 26))) {
            m_IsMakingTransition = true;
            m_TransitionSourceStateIdx = m_SelectedStateIndex;
        }
        if (ImGui::Button("Eliminar Estado", ImVec2(-1, 26))) {
            m_Controller->RemoveState(state.name);
            m_SelectedStateIndex = -1;
            m_IsDirty = true;
        }
        return;
    }

    ImGui::TextDisabled("Selecciona un Estado o Transicion para editar sus propiedades.");
}
