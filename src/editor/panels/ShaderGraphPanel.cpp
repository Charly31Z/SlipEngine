#include "SlipEngine/editor/panels/ShaderGraphPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/render/shadergraph/ShaderGraphSerializer.h"
#include "SlipEngine/render/shadergraph/ShaderGraphCompiler.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/render/SlipTexture.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/editor/SlipFileDialog.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

// ============================================================
// Helper: distancia de un punto a un segmento (para hit-test de links)
// ============================================================
static float DistPointToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    glm::vec2 ap(p.x - a.x, p.y - a.y);
    glm::vec2 ab(b.x - a.x, b.y - a.y);
    float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 1e-6f) return glm::length(ap);
    float t = glm::clamp((ap.x * ab.x + ap.y * ab.y) / lenSq, 0.0f, 1.0f);
    glm::vec2 closest(a.x + ab.x * t, a.y + ab.y * t);
    return glm::length(glm::vec2(p.x, p.y) - closest);
}

static float DistPointToBezier(const ImVec2& p, const ImVec2& p0, const ImVec2& cp1, const ImVec2& cp2, const ImVec2& p3) {
    float minDist = 1e9f;
    for (int s = 0; s <= 24; ++s) {
        float t = static_cast<float>(s) / 24.0f;
        float it = 1.0f - t;
        float w0 = it * it * it;
        float w1 = 3.0f * it * it * t;
        float w2 = 3.0f * it * t * t;
        float w3 = t * t * t;
        ImVec2 pt(w0 * p0.x + w1 * cp1.x + w2 * cp2.x + w3 * p3.x,
                  w0 * p0.y + w1 * cp1.y + w2 * cp2.y + w3 * p3.y);
        float dx = p.x - pt.x;
        float dy = p.y - pt.y;
        minDist = (std::min)(minDist, std::sqrt(dx * dx + dy * dy));
    }
    return minDist;
}

ShaderGraphPanel::ShaderGraphPanel() {
    s_Instance = this;
    SlipShaderGraph::ShaderNodeRegistry::Get().InitializeDefaultNodes();
    // El grafo por defecto (Master Output) ya se crea en el constructor de ShaderGraph
    m_GraphDirty = false;
    SetOpen(false);
}

ShaderGraphPanel::~ShaderGraphPanel() {
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
    // Liberar recursos GL del preview
    if (m_SphereVAO != 0) glDeleteVertexArrays(1, &m_SphereVAO);
    if (m_SphereVBO != 0) glDeleteBuffers(1, &m_SphereVBO);
    if (m_SphereEBO != 0) glDeleteBuffers(1, &m_SphereEBO);
    if (m_WhiteTex != 0) glDeleteTextures(1, &m_WhiteTex);
    if (m_PreviewDepthRBO != 0) glDeleteRenderbuffers(1, &m_PreviewDepthRBO);
    if (m_PreviewColorTex != 0) glDeleteTextures(1, &m_PreviewColorTex);
    if (m_PreviewFBO != 0) glDeleteFramebuffers(1, &m_PreviewFBO);
    for (auto& [nodeId, entry] : m_NodePreviews) {
        ReleaseNodePreview(entry);
    }
    m_NodePreviews.clear();
}

// ============================================================
// API pÃºblica
// ============================================================
void ShaderGraphPanel::OpenGraph(const std::string& filepath) {
    if (filepath.empty()) return;

    std::string errorMsg;
    if (SlipShaderGraph::ShaderGraphSerializer::DeserializeFromFile(m_Graph, filepath, &errorMsg)) {
        m_CurrentFilePath = filepath;
        m_GraphDirty = false;
        m_SelectedNodeId.clear();
        m_DraggingNodeId.clear();
        m_SelectedPropertyIndex = -1;
        m_IsLinking = false;
        RefreshPreviewShader();
        SetOpen(true);
        SlipConsole::LogSuccess("ShaderGraph cargado en el editor: " + filepath);
    } else {
        SlipConsole::LogError("Error al cargar ShaderGraph: " + (errorMsg.empty() ? filepath : errorMsg));
    }
}

void ShaderGraphPanel::NewGraph() {
    m_Graph.Clear();
    m_Graph.InitializeDefaultGraph();
    // Sin ruta asignada: el usuario decide la ubicacion con el dialogo de archivos
    m_CurrentFilePath.clear();
    m_GraphDirty = true;
    m_SelectedNodeId.clear();
    m_SelectedPropertyIndex = -1;
    m_IsLinking = false;
    RefreshPreviewShader();
}

void ShaderGraphPanel::SaveGraph(const std::string& filepath) {
    std::string savePath = filepath.empty() ? m_CurrentFilePath : filepath;

    if (savePath.empty()) {
        // Sin ruta: pedir al usuario el destino con el dialogo nativo del editor
        SlipFileDialog::Instance().Open("SaveNewShaderGraph", "Guardar Shader Graph Como",
                                        ".shadergraph", "assets/materials", SlipFileDialogType::SaveFile);
        return;
    }

    if (m_Graph.HasCycles()) {
        SlipConsole::LogError("El grafo tiene ciclos: no se puede compilar. Guardando de todos modos...");
    }

    if (SlipShaderGraph::ShaderGraphSerializer::SerializeToFile(m_Graph, savePath)) {
        m_CurrentFilePath = savePath;
        m_GraphDirty = false;
        SlipModelExtract::EnsureMetaForFile(savePath);
        SlipConsole::LogSuccess("ShaderGraph guardado: " + savePath);
        RefreshPreviewShader();
    } else {
        SlipConsole::LogError("Fallo al guardar ShaderGraph: " + savePath);
    }
}

// ============================================================
// Frame principal
// ============================================================
void ShaderGraphPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(1100, 640), ImGuiCond_FirstUseEver);
    std::string winTitle = "Editor de Shader Graph";
    if (m_CurrentFilePath.empty()) {
        winTitle += " - NuevoGrafo.shadergraph*";
    } else {
        winTitle += " - " + SlipFileSystem::GetFileName(m_CurrentFilePath);
        if (m_GraphDirty) winTitle += "*";
    }
    winTitle += "###ShaderGraphWindow";

    if (!ImGui::Begin(winTitle.c_str(), &m_IsOpen)) {
        ImGui::End();
        return;
    }

    RenderToolbar();
    ImGui::Separator();

    // Deteccion de cambios del grafo -> invalidar previews por nodo
    if (m_GraphDirty != m_LastGraphDirtyState) {
        InvalidateNodePreviews();
        m_LastGraphDirtyState = m_GraphDirty;
    }
    ImGuiIO& frameIO = ImGui::GetIO();
    m_PreviewStableTime += frameIO.DeltaTime;
    m_NodePreviewTime += frameIO.DeltaTime;
    if (m_NodePreviewsDirty && m_PreviewStableTime > 0.45f) {
        UpdateNextNodePreview();
        m_PreviewStableTime = 0.0f;
    }

    // Atajos de teclado del editor (Suprimir = eliminar nodo seleccionado)
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !frameIO.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            SlipShaderGraph::ShaderNode* selNode = FindNodeById(m_SelectedNodeId);
            if (selNode) {
                bool selIsMaster = (dynamic_cast<SlipShaderGraph::MasterNode*>(selNode) != nullptr);
                int masterCount = 0;
                for (const auto& n : m_Graph.nodes) {
                    if (dynamic_cast<SlipShaderGraph::MasterNode*>(n.get()) != nullptr) ++masterCount;
                }
                if (!selIsMaster || masterCount > 1) {
                    m_Graph.RemoveNode(selNode->id);
                    if (m_ExpandedPreviewNodeId == selNode->id) m_ExpandedPreviewNodeId.clear();
                    m_SelectedNodeId.clear();
                    m_GraphDirty = true;
                    SlipConsole::LogInfo("Nodo eliminado del Shader Graph (Suprimir).");
                } else {
                    SlipConsole::LogError("El nodo Master unico del grafo no puede eliminarse.");
                }
            }
        }
    }

    float totalWidth = ImGui::GetContentRegionAvail().x;
    float blackboardWidth = 210.0f;
    float inspectorWidth = 240.0f;
    float canvasWidth = totalWidth - blackboardWidth - inspectorWidth - 16.0f;
    if (canvasWidth < 300.0f) {
        inspectorWidth = 200.0f;
        canvasWidth = totalWidth - blackboardWidth - inspectorWidth - 16.0f;
    }

    ImGui::BeginChild("BlackboardSidebar", ImVec2(blackboardWidth, 0), true);
    RenderBlackboard();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ShaderGraphCanvasArea", ImVec2(canvasWidth, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    RenderGraphCanvas();
    ImGui::EndChild();

    ImGui::SameLine();

    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
    if (assetsDir.empty()) assetsDir = "assets";
    ImGui::BeginChild("NodeInspectorSidebar", ImVec2(inspectorWidth, 0), true);
    RenderNodeInspector(assetsDir);
    ImGui::EndChild();

    ImGui::End();
}

// ============================================================
// Toolbar
// ============================================================
void ShaderGraphPanel::RenderToolbar() {
    if (ImGui::Button("[ Guardar ]")) {
        if (m_CurrentFilePath.empty()) {
            SlipFileDialog::Instance().Open("SaveNewShaderGraph", "Guardar Shader Graph Como", ".shadergraph", "assets/materials", SlipFileDialogType::SaveFile);
        } else {
            SaveGraph(m_CurrentFilePath);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Guardar grafo (y refrescar el preview)");

    ImGui::SameLine();
    if (ImGui::Button("Abrir...")) {
        SlipFileDialog::Instance().Open("OpenShaderGraphFile", "Seleccionar Shader Graph", ".shadergraph", "assets/materials", SlipFileDialogType::OpenFile);
    }
    if (SlipFileDialog::Instance().HasSelected() &&
        SlipFileDialog::Instance().GetCurrentDialogKey() == "OpenShaderGraphFile") {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        if (!selected.empty()) {
            OpenGraph(selected);
        }
        SlipFileDialog::Instance().ClearSelection();
    }
    if (SlipFileDialog::Instance().HasSelected() &&
        SlipFileDialog::Instance().GetCurrentDialogKey() == "SaveNewShaderGraph") {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        if (!selected.empty()) {
            std::string ext = SlipFileSystem::GetFileExtension(selected);
            if (ext != ".shadergraph") selected += ".shadergraph";
            SaveGraph(selected);
            SetOpen(true);
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("+ Nuevo")) {
        NewGraph();
        // Pedir al usuario la ubicacion del nuevo grafo de inmediato
        SlipFileDialog::Instance().Open("SaveNewShaderGraph", "Guardar Shader Graph Como", ".shadergraph", "assets/materials", SlipFileDialogType::SaveFile);
    }
    ImGui::SameLine();

    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Selector de tipo de material (Lit / Unlit)
    const char* typeOptions[] = { "Lit", "Unlit" };
    int currentType = static_cast<int>(m_Graph.materialType);
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::Combo("##MaterialType", &currentType, typeOptions, IM_ARRAYSIZE(typeOptions))) {
        m_Graph.SetMaterialType(static_cast<SlipShaderGraph::ShaderGraphType>(currentType));
        // Invalidar links a puertos que dejaron de existir con el nuevo layout
        std::vector<std::string> invalidLinks;
        for (const auto& link : m_Graph.links) {
            SlipShaderGraph::ShaderNode* toNode = m_Graph.FindNode(link.toNodeId);
            if (!toNode || !toNode->FindInput(link.toPortId)) {
                invalidLinks.push_back(link.id);
            }
        }
        for (const auto& id : invalidLinks) m_Graph.RemoveLink(id);
        m_GraphDirty = true;
        RefreshPreviewShader();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tipo de material del grafo (cambia los entradas del nodo Master)");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::Checkbox("Preview", &m_ShowPreview)) {
        if (m_ShowPreview) RefreshPreviewShader();
    }

    if (!m_CurrentFilePath.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s",
                           SlipFileSystem::GetFileName(m_CurrentFilePath).c_str());
    }

    // Ctrl+S guarda el grafo
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_S, false) && ImGui::GetIO().KeyCtrl) {
        SaveGraph(m_CurrentFilePath);
    }
}

// ============================================================
// Blackboard de propiedades expuestas
// ============================================================
void ShaderGraphPanel::RenderBlackboard() {
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "PROPIEDADES");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
    if (ImGui::SmallButton("+##AddShaGraphProp")) {
        ImGui::OpenPopup("AddShaderGraphPropPopup");
    }

    if (ImGui::BeginPopup("AddShaderGraphPropPopup")) {
        struct TypeEntry { const char* label; SlipShaderGraph::PinType type; const char* baseName; };
        const TypeEntry entries[] = {
            { "Float",     SlipShaderGraph::PinType::Float,     "NuevoFloat" },
            { "Vector 2",  SlipShaderGraph::PinType::Vec2,      "NuevoVec2" },
            { "Vector 3",  SlipShaderGraph::PinType::Vec3,      "NuevoVec3" },
            { "Color/Vec 4", SlipShaderGraph::PinType::Vec4,    "NuevoVec4" },
            { "Texture 2D", SlipShaderGraph::PinType::Texture2D, "NuevaTextura" },
        };
        for (const auto& entry : entries) {
            if (ImGui::MenuItem(entry.label)) {
                std::string name = entry.baseName;
                int count = 1;
                while (m_Graph.FindPropertyByName(name)) {
                    name = std::string(entry.baseName) + "_" + std::to_string(count++);
                }
                m_Graph.AddProperty(name, entry.type);
                m_SelectedPropertyIndex = static_cast<int>(m_Graph.properties.size()) - 1;
                m_GraphDirty = true;
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Anadir propiedad expuesta del shader");
    ImGui::Separator();

    int propertyToDelete = -1;
    for (size_t i = 0; i < m_Graph.properties.size(); ++i) {
        auto& prop = m_Graph.properties[i];
        ImGui::PushID(prop.id.c_str());

        ImVec4 badgeColor;
        const char* typeTag = "F";
        switch (prop.type) {
            case SlipShaderGraph::PinType::Float:     badgeColor = ImVec4(0.3f, 0.8f, 0.5f, 1.0f); typeTag = "F"; break;
            case SlipShaderGraph::PinType::Vec2:      badgeColor = ImVec4(0.3f, 0.7f, 0.95f, 1.0f); typeTag = "V2"; break;
            case SlipShaderGraph::PinType::Vec3:      badgeColor = ImVec4(0.55f, 0.55f, 1.0f, 1.0f); typeTag = "V3"; break;
            case SlipShaderGraph::PinType::Vec4:      badgeColor = ImVec4(1.0f, 0.6f, 0.3f, 1.0f); typeTag = "V4"; break;
            case SlipShaderGraph::PinType::Texture2D: badgeColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f); typeTag = "T"; break;
            default:                             badgeColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); typeTag = "?"; break;
        }
        ImGui::TextColored(badgeColor, "[%s]", typeTag);
        ImGui::SameLine();

        bool isSelected = (m_SelectedPropertyIndex == static_cast<int>(i));
        if (ImGui::Selectable(prop.name.c_str(), isSelected)) {
            m_SelectedPropertyIndex = static_cast<int>(i);
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 16.0f);
        if (ImGui::SmallButton("x##delProp")) {
            propertyToDelete = static_cast<int>(i);
        }

        ImGui::PopID();
    }

    if (propertyToDelete >= 0) {
        m_Graph.RemoveProperty(m_Graph.properties[propertyToDelete].id);
        m_SelectedPropertyIndex = -1;
        m_GraphDirty = true;
    }

    // ---------- Editor de la propiedad seleccionada ----------
    if (m_SelectedPropertyIndex >= 0 && m_SelectedPropertyIndex < static_cast<int>(m_Graph.properties.size())) {
        auto& prop = m_Graph.properties[m_SelectedPropertyIndex];
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Editar Propiedad");
        ImGui::TextDisabled("Uniform: u_%s (ID: %s)", prop.name.c_str(), prop.id.c_str());

        char nameBuf[64];
        std::strncpy(nameBuf, prop.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Nombre##propName", nameBuf, sizeof(nameBuf))) {
            std::string newName = nameBuf;
            if (!newName.empty() && newName != prop.name && !m_Graph.FindPropertyByName(newName)) {
                std::string oldName = prop.name;
                prop.name = newName;
                prop.displayName = newName;
                // Renombrar en todos los nodos Property que referencian esta propiedad
                for (auto& node : m_Graph.nodes) {
                    if (auto* pNode = dynamic_cast<SlipShaderGraph::PropertyNode*>(node.get())) {
                        if (pNode->propertyId == prop.id) {
                            pNode->customProperties["propertyName"] = newName;
                            pNode->OnCustomPropertiesUpdated();
                        }
                    }
                }
                m_GraphDirty = true;
            }
        }

        const char* typeOptions[] = { "Float", "Vec2", "Vec3", "Vec4", "Texture2D", "Matrix4", "Boolean" };
        int typeIdx = static_cast<int>(prop.type);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Tipo##propType", &typeIdx, typeOptions, IM_ARRAYSIZE(typeOptions))) {
            // El tipo solo se puede cambiar si no hay nodos conectados que dependan del layout
            prop.type = static_cast<SlipShaderGraph::PinType>(typeIdx);
            m_GraphDirty = true;
        }

        switch (prop.type) {
            case SlipShaderGraph::PinType::Float:
                if (ImGui::DragFloat("Por defecto", &prop.defaultValue.x, 0.05f)) m_GraphDirty = true;
                break;
            case SlipShaderGraph::PinType::Vec2:
                if (ImGui::DragFloat2("Por defecto", &prop.defaultValue.x, 0.05f)) m_GraphDirty = true;
                break;
            case SlipShaderGraph::PinType::Vec3:
                if (ImGui::DragFloat3("Por defecto", &prop.defaultValue.x, 0.05f)) m_GraphDirty = true;
                break;
            case SlipShaderGraph::PinType::Vec4:
                if (ImGui::ColorEdit4("Color por defecto", &prop.defaultValue.x)) m_GraphDirty = true;
                break;
            case SlipShaderGraph::PinType::Texture2D: {
                char pathBuf[192];
                std::strncpy(pathBuf, prop.defaultTexturePath.c_str(), sizeof(pathBuf) - 1);
                pathBuf[sizeof(pathBuf) - 1] = '\0';
                if (ImGui::InputText("Ruta##propTex", pathBuf, sizeof(pathBuf))) {
                    prop.defaultTexturePath = pathBuf;
                    m_GraphDirty = true;
                }
                ImGui::TextDisabled("(O arrastra textura desde el explorador)");
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string droppedPath = reinterpret_cast<const char*>(payload->Data);
                        std::string ext = SlipFileSystem::GetFileExtension(droppedPath);
                        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                            prop.defaultTexturePath = droppedPath;
                            m_GraphDirty = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                break;
            }
            default: break;
        }
    } else {
        ImGui::Separator();
        ImGui::TextDisabled("Selecciona una propiedad.");
    }
}

// ============================================================
// Utilidades de geometrÃ­a de nodos
// ============================================================
ImU32 ShaderGraphPanel::TypeToColor(SlipShaderGraph::PinType type) const {
    switch (type) {
        case SlipShaderGraph::PinType::Float:     return IM_COL32(90, 210, 130, 255);
        case SlipShaderGraph::PinType::Vec2:      return IM_COL32(80, 180, 240, 255);
        case SlipShaderGraph::PinType::Vec3:      return IM_COL32(120, 120, 240, 255);
        case SlipShaderGraph::PinType::Vec4:      return IM_COL32(235, 150, 80, 255);
        case SlipShaderGraph::PinType::Texture2D: return IM_COL32(230, 90, 90, 255);
        case SlipShaderGraph::PinType::Matrix4:   return IM_COL32(190, 90, 230, 255);
        case SlipShaderGraph::PinType::Boolean:   return IM_COL32(90, 230, 230, 255);
        default:                             return IM_COL32(150, 150, 150, 255);
    }
}

float ShaderGraphPanel::GetNodeHeightForPorts(int portCount) const {
    return 28.0f + (static_cast<float>(std::max(1, portCount)) * 20.0f);
}

ImVec2 ShaderGraphPanel::NodeTopLeft(const ImVec2& canvasP0, const SlipShaderGraph::ShaderNode& node) const {
    return ImVec2(canvasP0.x + m_CanvasOffset.x + node.posX * m_Zoom,
                  canvasP0.y + m_CanvasOffset.y + node.posY * m_Zoom);
}

ImVec2 ShaderGraphPanel::NodeSize(const SlipShaderGraph::ShaderNode& node) const {
    float w = GetNodeWidth() * m_Zoom;
    int rows = (std::max)(std::max(static_cast<int>(node.inputs.size()), static_cast<int>(node.outputs.size())), 1);
    float h = GetNodeHeightForPorts(rows) * m_Zoom;
    return ImVec2(w, h);
}

ImVec2 ShaderGraphPanel::InputPortPos(const ImVec2& canvasP0, const SlipShaderGraph::ShaderNode& node, int index) const {
    ImVec2 topLeft = NodeTopLeft(canvasP0, node);
    float headerH = 26.0f * m_Zoom;
    float rowH = 20.0f * m_Zoom;
    return ImVec2(10.0f * m_Zoom + topLeft.x, topLeft.y + headerH + (static_cast<float>(index) + 0.5f) * rowH);
}

ImVec2 ShaderGraphPanel::OutputPortPos(const ImVec2& canvasP0, const SlipShaderGraph::ShaderNode& node, int index) const {
    ImVec2 size = NodeSize(node);
    ImVec2 pos = InputPortPos(canvasP0, node, index);
    return ImVec2(pos.x + (GetNodeWidth() * m_Zoom) - (20.0f * m_Zoom), pos.y);
}

SlipShaderGraph::ShaderNode* ShaderGraphPanel::FindNodeById(const std::string& id) const {
    return m_Graph.FindNode(id);
}

// ============================================================
// Canvas del grafo
// ============================================================
void ShaderGraphPanel::RenderGraphCanvas() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);
    m_LastCanvasOrigin = canvasP0;
    m_LastCanvasSize = canvasSize;
    ImGuiIO& io = ImGui::GetIO();

    // RectÃ¡ngulo del preview (para ignorar la interacciÃ³n del grafo debajo)
    bool previewShown = (m_ShowPreview && m_PreviewSize.x > 1.0f && canvasSize.x > m_PreviewSize.x + 40.0f);
    if (previewShown) {
        ImVec2 previewPos = ImVec2(canvasP1.x - m_PreviewSize.x - m_PreviewOffsetRight,
                                   canvasP0.y + m_PreviewOffsetTop);
        m_PreviewRect = ImRect(previewPos, ImVec2(previewPos.x + m_PreviewSize.x, previewPos.y + m_PreviewSize.y + m_PreviewHeaderSize.y));
    } else {
        m_PreviewRect = ImRect(0, 0, 0, 0);
    }

    drawList->PushClipRect(canvasP0, canvasP1, true);

    // Bandera: el ratÃ³n estÃ¡ sobre el preview (la interacciÃ³n del canvas es ignorada ahÃ­)
    auto mouseInPreview = [&]() -> bool {
        return previewShown && m_PreviewRect.Contains(io.MousePos);
    };

    // 1. Grid de fondo
    // (replicando DrawGrid del AnimatorGraphPanel)
    drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(24, 26, 32, 255));
    float gridSize = 64.0f * m_Zoom;
    for (float x = std::fmod(m_CanvasOffset.x, gridSize); x < canvasSize.x; x += gridSize) {
        drawList->AddLine(ImVec2(canvasP0.x + x, canvasP0.y),
                          ImVec2(canvasP0.x + x, canvasP1.y), IM_COL32(38, 42, 52, 200), 1.0f);
    }
    for (float y = std::fmod(m_CanvasOffset.y, gridSize); y < canvasSize.y; y += gridSize) {
        drawList->AddLine(ImVec2(canvasP0.x, canvasP0.y + y),
                          ImVec2(canvasP1.x, canvasP0.y + y), IM_COL32(38, 42, 52, 200), 1.0f);
    }

    // 2. Panning (clic derecho / central)
    bool isCanvasHovered = ImGui::IsWindowHovered() && !mouseInPreview() &&
        !(m_ExpandedPreviewRect.Min.x != 0.0f && m_ExpandedPreviewRect.Contains(io.MousePos));
    if (isCanvasHovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        m_CanvasOffset.x += io.MouseDelta.x;
        m_CanvasOffset.y += io.MouseDelta.y;
    }

    // 3. Zoom con la rueda
    if (isCanvasHovered && io.MouseWheel != 0.0f) {
        float prevZoom = m_Zoom;
        m_Zoom = std::clamp(m_Zoom + io.MouseWheel * 0.08f, 0.5f, 1.8f);
        ImVec2 mousePosCanvas = ImVec2((io.MousePos.x - canvasP0.x - m_CanvasOffset.x) / prevZoom,
                                       (io.MousePos.y - canvasP0.y - m_CanvasOffset.y) / prevZoom);
        m_CanvasOffset.x = io.MousePos.x - canvasP0.x - mousePosCanvas.x * m_Zoom;
        m_CanvasOffset.y = io.MousePos.y - canvasP0.y - mousePosCanvas.y * m_Zoom;
    }

    // 4. DetecciÃ³n de nodo bajo el cursor + pines
    SlipShaderGraph::ShaderNode* hoveredNode = nullptr;
    int hoveredInputIdx = -1;
    int hoveredOutputIdx = -1;
    for (const auto& node : m_Graph.nodes) {
        if (!node) continue;
        ImVec2 topLeft = NodeTopLeft(canvasP0, *node);
        ImVec2 size = NodeSize(*node);
        ImVec2 bottomRight = ImVec2(topLeft.x + size.x, topLeft.y + size.y);

        if (io.MousePos.x >= topLeft.x && io.MousePos.x <= bottomRight.x &&
            io.MousePos.y >= topLeft.y && io.MousePos.y <= bottomRight.y) {
            hoveredNode = node.get();

            // Â¿Sobre un pin?
            float portRadius = 8.0f * m_Zoom;
            for (int i = 0; i < static_cast<int>(node->inputs.size()); ++i) {
                ImVec2 pos = InputPortPos(canvasP0, *node, i);
                if (io.MousePos.x >= pos.x - portRadius && io.MousePos.x <= pos.x + portRadius &&
                    io.MousePos.y >= pos.y - portRadius && io.MousePos.y <= pos.y + portRadius) {
                    hoveredInputIdx = i;
                    break;
                }
            }
            for (int i = 0; i < static_cast<int>(node->outputs.size()); ++i) {
                ImVec2 pos = OutputPortPos(canvasP0, *node, i);
                if (io.MousePos.x >= pos.x - portRadius && io.MousePos.x <= pos.x + portRadius &&
                    io.MousePos.y >= pos.y - portRadius && io.MousePos.y <= pos.y + portRadius) {
                    hoveredOutputIdx = i;
                    break;
                }
            }
            break;
        }
    }

    // 5. Hit-test y dibujo de links existentes
    m_HoveredLinkId.clear();
    for (const auto& link : m_Graph.links) {
        SlipShaderGraph::ShaderNode* fromNode = m_Graph.FindNode(link.fromNodeId);
        SlipShaderGraph::ShaderNode* toNode = m_Graph.FindNode(link.toNodeId);
        if (!fromNode || !toNode) continue;

        int outIdx = 0;
        for (int i = 0; i < static_cast<int>(fromNode->outputs.size()); ++i) {
            if (fromNode->outputs[i].id == link.fromPortId) { outIdx = i; break; }
        }
        int inIdx = 0;
        for (int i = 0; i < static_cast<int>(toNode->inputs.size()); ++i) {
            if (toNode->inputs[i].id == link.toPortId) { inIdx = i; break; }
        }

        ImVec2 start = OutputPortPos(canvasP0, *fromNode, outIdx);
        ImVec2 end = InputPortPos(canvasP0, *toNode, inIdx);

        float offX = 40.0f * m_Zoom;
        ImVec2 cp1 = ImVec2(start.x + offX, start.y);
        ImVec2 cp2 = ImVec2(end.x - offX, end.y);

        // Color segÃºn el tipo de salida
        const SlipShaderGraph::ShaderPort* outPort = fromNode->FindOutput(link.fromPortId);
        ImU32 baseCol = outPort ? TypeToColor(outPort->type) : IM_COL32(150, 160, 175, 220);
        float thickness = 2.2f * m_Zoom;

        bool linkHovered = false;
        if (!hoveredNode && isCanvasHovered) {
            float dist = DistPointToBezier(io.MousePos, start, cp1, cp2, end);
            if (dist <= 6.0f * m_Zoom) {
                linkHovered = true;
                m_HoveredLinkId = link.id;
                thickness = 3.4f * m_Zoom;
            }
        }

        drawList->AddBezierCubic(start, cp1, cp2, end, baseCol, thickness);
        if (linkHovered) {
            drawList->AddCircle(ImVec2((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f),
                                7.0f * m_Zoom, IM_COL32(255, 220, 80, 255), 0, 2.0f * m_Zoom);
        }
    }

    // Eliminar link sobre el que se clicÃ³ (cuando no hay nodo u otro elemento debajo)
    if (!m_HoveredLinkId.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !hoveredNode && hoveredInputIdx < 0 && hoveredOutputIdx < 0 && !m_IsLinking) {
        m_Graph.RemoveLink(m_HoveredLinkId);
        m_HoveredLinkId.clear();
        m_GraphDirty = true;
    }

    // BotÃ³n tambiÃ©n para borrar el link con clic derecho cuando se arrastra un elemento
    if (!m_HoveredLinkId.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !hoveredNode && !m_IsLinking) {
        m_Graph.RemoveLink(m_HoveredLinkId);
        m_HoveredLinkId.clear();
        m_GraphDirty = true;
    }

    // 6. Dibujo de nodos
    for (const auto& node : m_Graph.nodes) {
        if (!node) continue;

        ImVec2 topLeft = NodeTopLeft(canvasP0, *node);
        ImVec2 size = NodeSize(*node);
        ImVec2 bottomRight = ImVec2(topLeft.x + size.x, topLeft.y + size.y);
        float rounding = 7.0f * m_Zoom;

        bool isSelected = (m_SelectedNodeId == node->id);
        bool isMaster = (dynamic_cast<SlipShaderGraph::MasterNode*>(node.get()) != nullptr);
        bool isHovered = (hoveredNode == node.get());

        // Cuerpo
        drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(34, 37, 45, 250), rounding);
        // Cabecera
        ImU32 headerCol = isMaster ? IM_COL32(190, 150, 45, 255) : IM_COL32(48, 62, 92, 255);
        ImVec2 headerMax = ImVec2(bottomRight.x, topLeft.y + 26.0f * m_Zoom);
        drawList->AddRectFilled(topLeft, headerMax, headerCol, rounding, ImDrawFlags_RoundCornersTop);
        drawList->AddText(ImVec2(topLeft.x + 8.0f * m_Zoom, topLeft.y + 4.0f * m_Zoom),
                          IM_COL32(255, 255, 255, 255), node->title.c_str());

        // Swatch de preview del nodo (cabecera derecha, estilo Unity)
        if (m_ShowNodePreviews && !isMaster) {
            auto entryIt = m_NodePreviews.find(node->id);
            bool haveTex = (entryIt != m_NodePreviews.end() && entryIt->second.valid && entryIt->second.tex != 0);
            float sw = 20.0f * m_Zoom;
            ImVec2 swMin(bottomRight.x - sw - 4.0f * m_Zoom, topLeft.y + 3.0f * m_Zoom);
            ImVec2 swMax(bottomRight.x - 4.0f * m_Zoom, topLeft.y + 3.0f * m_Zoom + sw);
            ImRect swatchRect(swMin, swMax);
            if (haveTex) {
                ImTextureID swTex = static_cast<ImTextureID>(static_cast<uintptr_t>(entryIt->second.tex));
                drawList->AddImageRounded(swTex, swMin, swMax, ImVec2(0, 1), ImVec2(1, 0),
                                          IM_COL32(255, 255, 255, 255), 2.0f * m_Zoom);
            } else {
                drawList->AddRectFilled(swMin, swMax, IM_COL32(58, 62, 74, 255), 2.0f * m_Zoom);
            }

            // Clic sobre el swatch: expandir el preview del nodo en una ventana
            bool isExpanded = (m_ExpandedPreviewNodeId == node->id);
            if (swatchRect.Contains(io.MousePos) && ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                m_ExpandedPreviewNodeId = isExpanded ? std::string() : node->id;
                if (!m_ExpandedPreviewNodeId.empty()) {
                    m_ExpandedPreviewOffset = ImVec2(size.x + 12.0f, -6.0f);
                }
                m_DraggingExpandedPreview = false;
            }
            if (swatchRect.Contains(io.MousePos)) {
                ImGui::SetTooltip(isExpanded ? "Minimizar preview" : "Ampliar preview del nodo");
            }
        }

        // Puerto de salida + etiquetas
        for (int i = 0; i < static_cast<int>(node->outputs.size()); ++i) {
            const auto& port = node->outputs[i];
            ImVec2 pos = OutputPortPos(canvasP0, *node, i);
            bool filling = false;
            if (m_IsLinking) {
                if (m_LinkFromIsOutput == false) {
                    filling = SlipShaderGraph::ArePinsCompatible(port.type, m_LinkPinType);
                } else {
                    filling = false;
                }
            }
            ImU32 col = TypeToColor(port.type);
            drawList->AddCircleFilled(pos, 4.5f * m_Zoom, filling ? IM_COL32(120, 255, 150, 255) : col);
            if (isHovered && hoveredOutputIdx == i) {
                drawList->AddCircle(pos, 7.5f * m_Zoom, IM_COL32(255, 255, 255, 240), 0, 2.0f * m_Zoom);
            }
            std::string label = port.name + " out";
            (void)label;
        }

        // Pines de entrada + etiquetas
        for (int i = 0; i < static_cast<int>(node->inputs.size()); ++i) {
            const auto& port = node->inputs[i];
            ImVec2 pos = InputPortPos(canvasP0, *node, i);
            const SlipShaderGraph::ShaderLink* existing = m_Graph.FindLinkToInput(node->id, port.id);

            bool receiving = false;
            if (m_IsLinking && m_LinkFromIsOutput) {
                receiving = SlipShaderGraph::ArePinsCompatible(m_LinkPinType, port.type);
            }

            ImU32 col = TypeToColor(port.type);
            drawList->AddCircleFilled(pos, 4.5f * m_Zoom, receiving ? IM_COL32(120, 255, 150, 255) : col);
            if (existing) drawList->AddCircleFilled(pos, 2.2f * m_Zoom, IM_COL32(240, 240, 240, 255));
            if (isHovered && hoveredInputIdx == i) {
                drawList->AddCircle(pos, 7.5f * m_Zoom, IM_COL32(255, 255, 255, 240), 0, 2.0f * m_Zoom);
            }

            // Etiquetas de los pines (derechas al interior)
            drawList->AddText(ImVec2(pos.x + 9.0f * m_Zoom, pos.y - 9.0f * m_Zoom),
                              IM_COL32(170, 180, 195, 255), port.name.c_str());
        }
        // Etiquetas de salida
        for (int i = 0; i < static_cast<int>(node->outputs.size()); ++i) {
            const auto& port = node->outputs[i];
            ImVec2 pos = OutputPortPos(canvasP0, *node, i);
            std::string text = port.name + " out";
            ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            drawList->AddText(ImVec2(pos.x - textSize.x - 9.0f * m_Zoom, pos.y - 9.0f * m_Zoom),
                              IM_COL32(170, 180, 195, 255), text.c_str());
        }

        // Borde: selecciÃ³n / hover
        ImU32 borderCol = IM_COL32(65, 70, 85, 255);
        float borderThick = 1.5f * m_Zoom;
        if (isMaster) borderCol = IM_COL32(235, 200, 90, 160);
        if (isHovered) {
            borderCol = IM_COL32(160, 200, 255, 220);
            borderThick = 2.2f * m_Zoom;
        }
        if (isSelected) {
            borderCol = IM_COL32(255, 210, 40, 255);
            borderThick = 2.6f * m_Zoom;
        }
        drawList->AddRect(topLeft, bottomRight, borderCol, rounding, 0, borderThick);
    }

    // 6b. Ventana expandida de preview del nodo seleccionado/minimizado
    if (m_ShowNodePreviews) {
        RenderExpandedPreviewWindow(canvasP0, drawList);
    }
    // 7. Interacciones con nodos / pines
    if (m_IsLinking && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        m_IsLinking = false;
    }

    if (isCanvasHovered && hoveredNode && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        bool needsDragging = false;

        // Prioridad: interactivo de pin > arrastrar nodo
        if (hoveredOutputIdx >= 0 && !m_IsLinking) {
            // Iniciar un link desde la salida
            const auto& port = hoveredNode->outputs[hoveredOutputIdx];
            m_IsLinking = true;
            m_LinkFromNodeId = hoveredNode->id;
            m_LinkFromPortId = port.id;
            m_LinkFromIsOutput = true;
            m_LinkPinType = port.type;
        } else if (hoveredInputIdx >= 0) {
            const auto& port = hoveredNode->inputs[hoveredInputIdx];
            if (m_IsLinking && m_LinkFromIsOutput) {
                // Completar link de salida a entrada
                if (SlipShaderGraph::ArePinsCompatible(m_LinkPinType, port.type) && m_LinkFromNodeId != hoveredNode->id) {
                    // Reemplazar el link previo a esta entrada (1 link por entrada)
                    if (const SlipShaderGraph::ShaderLink* prev = m_Graph.FindLinkToInput(hoveredNode->id, port.id)) {
                        m_Graph.RemoveLink(prev->id);
                    }
                    m_Graph.AddLink(m_LinkFromNodeId, m_LinkFromPortId, hoveredNode->id, port.id);
                    m_GraphDirty = true;
                }
                m_IsLinking = false;
            } else if (!m_IsLinking) {
                // Iniciar un link desde la entrada (hacia una salida permitida)
                m_IsLinking = true;
                m_LinkFromNodeId = hoveredNode->id;
                m_LinkFromPortId = port.id;
                m_LinkFromIsOutput = false;
                m_LinkPinType = port.type;
            }
        } else if (hoveredOutputIdx < 0) {
            needsDragging = true;
        } else if (hoveredOutputIdx >= 0 && m_IsLinking && !m_LinkFromIsOutput) {
            // Completar link de entrada a salida
            const auto& port = hoveredNode->outputs[hoveredOutputIdx];
            if (SlipShaderGraph::ArePinsCompatible(port.type, m_LinkPinType)) {
                SlipShaderGraph::ShaderNode* target = FindNodeById(m_LinkFromNodeId);
                if (target && target->id != hoveredNode->id) {
                    if (const SlipShaderGraph::ShaderLink* prev = m_Graph.FindLinkToInput(m_LinkFromNodeId, m_LinkFromPortId)) {
                        m_Graph.RemoveLink(prev->id);
                    }
                    m_Graph.AddLink(hoveredNode->id, port.id, m_LinkFromNodeId, m_LinkFromPortId);
                    m_GraphDirty = true;
                }
            }
            m_IsLinking = false;
        }

        if (needsDragging) {
            m_SelectedNodeId = hoveredNode->id;
            m_DraggingNodeId = hoveredNode->id;
            m_SelectedPropertyIndex = -1;
        }
    }

    // 8. Arrastre de nodo
    if (!m_DraggingNodeId.empty()) {
        SlipShaderGraph::ShaderNode* draggingNode = FindNodeById(m_DraggingNodeId);
        if (draggingNode && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            draggingNode->posX += io.MouseDelta.x / m_Zoom;
            draggingNode->posY += io.MouseDelta.y / m_Zoom;
            m_GraphDirty = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_DraggingNodeId.clear();
        }
    }

    // 9. MenÃº contextual de nodos (clic derecho sobre el nodo)
    if (isCanvasHovered && hoveredNode && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_SelectedNodeId = hoveredNode->id;
        ImGui::OpenPopup("ShaderGraphNodeContext");
    }
    if (ImGui::BeginPopup("ShaderGraphNodeContext")) {
        SlipShaderGraph::ShaderNode* ctxNode = FindNodeById(m_SelectedNodeId);
        bool ctxIsMaster = ctxNode && (dynamic_cast<SlipShaderGraph::MasterNode*>(ctxNode) != nullptr);

        // Contar cuantos nodos Master existen (solo se conserva 1)
        int masterCount = 0;
        for (const auto& node : m_Graph.nodes) {
            if (dynamic_cast<SlipShaderGraph::MasterNode*>(node.get()) != nullptr) ++masterCount;
        }

        if (!ctxIsMaster) {
            if (ImGui::MenuItem("Eliminar Nodo")) {
                m_Graph.RemoveNode(m_SelectedNodeId);
                m_SelectedNodeId.clear();
                m_GraphDirty = true;
            }
        } else if (masterCount > 1) {
            // Master duplicado: se permite eliminarlo para recuperar el grafo
            if (ImGui::MenuItem("Eliminar Nodo (Master sobrante)")) {
                m_Graph.RemoveNode(m_SelectedNodeId);
                m_SelectedNodeId.clear();
                m_GraphDirty = true;
                SlipConsole::LogSuccess("Master sobrante eliminado del Shader Graph");
            }
        } else {
            ImGui::TextDisabled("Nodo Master (no se puede eliminar)");
        }
        ImGui::EndPopup();
    }

    // 10. Clic derecho en espacio vacÃ­o: paleta de nodos
    if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !hoveredNode) {
        ImGui::OpenPopup("ShaderGraphNodePalette");
        m_NodeSearchBuf[0] = '\0';
    }
    if (ImGui::BeginPopup("ShaderGraphNodePalette")) {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##NodeSearch", "Buscar nodo...", m_NodeSearchBuf, sizeof(m_NodeSearchBuf));
        ImGui::Separator();

        auto& registry = SlipShaderGraph::ShaderNodeRegistry::Get();
        const auto& descriptors = registry.GetRegisteredNodes();

        std::string filter = m_NodeSearchBuf;
        std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return std::tolower(c); });

        std::map<std::string, std::vector<const SlipShaderGraph::NodeDescriptor*>> byCategory;
        for (const auto& desc : descriptors) {
            // El Master Output es unico (se crea con el grafo); excluirlo de la paleta
            if (desc.typeName == "MasterOutput") continue;
            if (!filter.empty()) {
                std::string titleLower = desc.title;
                std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), [](unsigned char c) { return std::tolower(c); });
                if (titleLower.find(filter) == std::string::npos) continue;
            }
            byCategory[desc.category].push_back(&desc);
        }

        ImVec2 spawnPos(io.MousePos.x - canvasP0.x - m_CanvasOffset.x - (GetNodeWidth() * 0.5f),
                        io.MousePos.y - canvasP0.y - m_CanvasOffset.y - 13.0f);
        spawnPos.x /= m_Zoom;
        spawnPos.y /= m_Zoom;

        for (auto& [category, nodeList] : byCategory) {
            if (ImGui::BeginMenu(category.c_str())) {
                for (const auto* desc : nodeList) {
                    if (ImGui::MenuItem(desc->title.c_str())) {
                        m_Graph.AddNode(desc->typeName, spawnPos.x, spawnPos.y);
                        m_GraphDirty = true;
                    }
                    if (ImGui::IsItemHovered() && !desc->description.empty()) {
                        ImGui::SetTooltip("%s", desc->description.c_str());
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    // 11. Cable pendiente hasta el ratÃ³n mientras se crea un link
    if (m_IsLinking) {
        ImU32 wireCol = TypeToColor(m_LinkPinType);
        if (m_LinkFromIsOutput) {
            SlipShaderGraph::ShaderNode* fromNode = FindNodeById(m_LinkFromNodeId);
            if (fromNode) {
                ImVec2 start(0, 0);
                bool foundPos = false;
                for (int i = 0; i < static_cast<int>(fromNode->outputs.size()); ++i) {
                    if (fromNode->outputs[i].id == m_LinkFromPortId) {
                        start = OutputPortPos(canvasP0, *fromNode, i);
                        foundPos = true;
                        break;
                    }
                }
                if (!foundPos) start = OutputPortPos(canvasP0, *fromNode, 0);
                drawList->AddLine(start, io.MousePos, wireCol, 2.4f * m_Zoom);
            }
        } else {
            SlipShaderGraph::ShaderNode* fromNode = FindNodeById(m_LinkFromNodeId);
            if (fromNode) {
                ImVec2 start(0, 0);
                bool foundPos = false;
                for (int i = 0; i < static_cast<int>(fromNode->inputs.size()); ++i) {
                    if (fromNode->inputs[i].id == m_LinkFromPortId) {
                        start = InputPortPos(canvasP0, *fromNode, i);
                        foundPos = true;
                        break;
                    }
                }
                if (!foundPos) start = InputPortPos(canvasP0, *fromNode, 0);
                drawList->AddLine(start, io.MousePos, wireCol, 2.4f * m_Zoom);
            }
        }
    }

    // 12. Preview de material (ventana flotante dentro del canvas)
    if (previewShown) {
        RenderPreviewOverlay(canvasP0, canvasP1);
    }

    // 13. Render offscreen de los previews por nodo (estilo Unity)
    if (m_ShowNodePreviews) {
        RenderNodePreviews();
    }

    drawList->PopClipRect();
}

// ============================================================
// Inspector del nodo seleccionado
// ============================================================
void ShaderGraphPanel::RenderNodeInspector(const std::string& assetsDir) {
    (void)assetsDir;

    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "INSPECTOR DE NODO");
    ImGui::Separator();

    SlipShaderGraph::ShaderNode* node = FindNodeById(m_SelectedNodeId);
    if (!node) {
        ImGui::TextDisabled("Selecciona un nodo para editarlo.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "Estado de compilacion:");
        if (m_PreviewValid) {
            ImGui::TextColored(ImVec4(0.35f, 0.8f, 0.35f, 1.0f), "  OK (%s)", m_Graph.materialType == SlipShaderGraph::ShaderGraphType::Lit ? "Lit" : "Unlit");
        } else if (!m_PreviewError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", m_PreviewError.substr(0, 600).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("  (sin compilar todavia)");
        }
        return;
    }

    // TÃ­tulo editable
    char titleBuf[64];
    std::strncpy(titleBuf, node->title.c_str(), sizeof(titleBuf) - 1);
    titleBuf[sizeof(titleBuf) - 1] = '\0';
    if (ImGui::InputText("Titulo##nodeTitle", titleBuf, sizeof(titleBuf))) {
        // Respecto al master/property, el tÃ­tulo puede cambiar libremente en la vista
        if (dynamic_cast<SlipShaderGraph::PropertyNode*>(node)) {
            node->title = titleBuf;
        } else {
            node->title = titleBuf;
        }
        m_GraphDirty = true;
    }
    ImGui::TextDisabled("Tipo: %s", node->typeName.c_str());

    // Editor de nodo pre-vido segÃºn su tipo
    RenderConstantNodeEditor(node);
    RenderPropertyNodeEditor(node);

    ImGui::Separator();

    // Valores por defecto de pines sin conectar
    RenderUnconnectedPortDefaults(node);

    // Cambiar entre Lit / Unlit cuando el nodo seleccionado es el Master
    if (dynamic_cast<SlipShaderGraph::MasterNode*>(node)) {
        auto* masterNode = static_cast<SlipShaderGraph::MasterNode*>(node);
        int curType = (masterNode->materialType == SlipShaderGraph::ShaderGraphType::Lit) ? 0 : 1;
        const char* typeLabels[2] = { "Lit", "Unlit" };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Tipo de Material", &curType, typeLabels, 2)) {
            SlipShaderGraph::ShaderGraphType newType = (curType == 0) ? SlipShaderGraph::ShaderGraphType::Lit
                                                                     : SlipShaderGraph::ShaderGraphType::Unlit;
            m_Graph.SetMaterialType(newType);  // sincroniza grafo + master + puertos
            m_GraphDirty = true;
            RefreshPreviewShader();
        }
    }

    ImGui::Separator();

    // Centrar la vista sobre el nodo seleccionado (o sobre el Master si no hay seleccion)
    if (ImGui::Button("Centrar Vista", ImVec2(-1.0f, 0.0f))) {
        SlipShaderGraph::ShaderNode* target = node ? node : m_Graph.GetMasterNode();
        if (target && m_LastCanvasSize.x > 1.0f) {
            ImVec2 nodeSize = NodeSize(*target);
            ImVec2 centerGraph((target->posX + nodeSize.x * 0.5f), (target->posY + nodeSize.y * 0.5f));
            m_Zoom = 1.0f;
            m_CanvasOffset = ImVec2(m_LastCanvasSize.x * 0.5f - centerGraph.x,
                                    m_LastCanvasSize.y * 0.5f - centerGraph.y);
        } else {
            m_CanvasOffset = ImVec2(250.0f, 200.0f);
            m_Zoom = 1.0f;
        }
    }
}

void ShaderGraphPanel::RenderConstantNodeEditor(SlipShaderGraph::ShaderNode* node) {
    if (auto* floatNode = dynamic_cast<SlipShaderGraph::FloatConstantNode*>(node)) {
        if (ImGui::DragFloat("Valor", &floatNode->value, 0.05f)) {
            floatNode->customProperties["value"] = std::to_string(floatNode->value);
            m_GraphDirty = true;
        }
    } else if (auto* vec2Node = dynamic_cast<SlipShaderGraph::Vec2ConstantNode*>(node)) {
        if (ImGui::DragFloat2("Valor", &vec2Node->value.x, 0.05f)) {
            vec2Node->customProperties["x"] = std::to_string(vec2Node->value.x);
            vec2Node->customProperties["y"] = std::to_string(vec2Node->value.y);
            m_GraphDirty = true;
        }
    } else if (auto* vec3Node = dynamic_cast<SlipShaderGraph::Vec3ConstantNode*>(node)) {
        bool changed = ImGui::DragFloat3("Valor", &vec3Node->value.x, 0.05f);
        changed |= ImGui::ColorEdit3("Color", &vec3Node->value.x);
        if (changed) {
            vec3Node->customProperties["x"] = std::to_string(vec3Node->value.x);
            vec3Node->customProperties["y"] = std::to_string(vec3Node->value.y);
            vec3Node->customProperties["z"] = std::to_string(vec3Node->value.z);
            m_GraphDirty = true;
        }
    } else if (auto* vec4Node = dynamic_cast<SlipShaderGraph::Vec4ConstantNode*>(node)) {
        if (ImGui::ColorEdit4("Color", &vec4Node->value.x)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.5f %.5f %.5f %.5f",
                     vec4Node->value.x, vec4Node->value.y, vec4Node->value.z, vec4Node->value.w);
            vec4Node->customProperties["color"] = buf;
            m_GraphDirty = true;
        }
    }
}

void ShaderGraphPanel::RenderPropertyNodeEditor(SlipShaderGraph::ShaderNode* node) {
    auto* propNode = dynamic_cast<SlipShaderGraph::PropertyNode*>(node);
    if (!propNode) return;

    ImGui::Separator();
    ImGui::Text("Propiedad expuesta:");
    if (ImGui::BeginCombo("##propNodeCombo", propNode->propertyName.empty() ? "[Ninguna]" : propNode->propertyName.c_str())) {
        for (const auto& prop : m_Graph.properties) {
            bool isSel = (prop.id == propNode->propertyId);
            std::string label = "[" + std::string(SlipShaderGraph::PinTypeToString(prop.type)) + "] " + prop.name;
            if (ImGui::Selectable(label.c_str(), isSel)) {
                propNode->SetProperty(prop);
                m_GraphDirty = true;
            }
        }
        ImGui::EndCombo();
    }
}

void ShaderGraphPanel::RenderUnconnectedPortDefaults(SlipShaderGraph::ShaderNode* node) {
    if (!node) return;

    bool anyShown = false;
    for (auto& port : node->inputs) {
        if (m_Graph.FindLinkToInput(node->id, port.id)) continue;  // conectado: se ignora el valor por defecto

        if (!anyShown) {
            ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Valores por defecto:");
            anyShown = true;
        }
        ImGui::PushID(port.id.c_str());
        ImGui::Text("%s", port.name.c_str());
        switch (port.type) {
            case SlipShaderGraph::PinType::Float:
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("##portDefault", &port.defaultValue.x, 0.05f)) {
                    m_GraphDirty = true;
                }
                break;
            case SlipShaderGraph::PinType::Vec2:
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat2("##portDefault", &port.defaultValue.x, 0.05f)) {
                    m_GraphDirty = true;
                }
                break;
            case SlipShaderGraph::PinType::Vec3:
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat3("##portDefault", &port.defaultValue.x, 0.05f)) {
                    m_GraphDirty = true;
                }
                break;
            case SlipShaderGraph::PinType::Vec4:
                if (ImGui::ColorEdit4("##portDefault", &port.defaultValue.x)) {
                    m_GraphDirty = true;
                }
                break;
            case SlipShaderGraph::PinType::Texture2D: {
                // Ruta de textura por defecto del pin
                ImGui::SetNextItemWidth(-1.0f);
                char buf[192];
                std::strncpy(buf, port.defaultTexturePath.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                if (ImGui::InputText("##portDefault", buf, sizeof(buf))) {
                    port.defaultTexturePath = buf;
                    m_GraphDirty = true;
                }
                ImGui::TextDisabled("(Uniform interno del pin)");
                break;
            }
            default:
                ImGui::TextDisabled("%s (sin editor)", port.name.c_str());
                break;
        }
        ImGui::PopID();
    }
}

// ============================================================
// Preview de material
// ============================================================
void ShaderGraphPanel::RefreshPreviewShader() {
    m_PreviewValid = false;
    m_PreviewError.clear();
    m_PreviewShader.reset();

    SlipShaderGraph::ShaderCompilerOptions options;
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    options.targetVersion = SlipShaderGraph::GLSLTargetVersion::Mobile_GLES300_ES;
#endif

    SlipShaderGraph::GeneratedShaderCode code = SlipShaderGraph::ShaderGraphCompiler::Compile(m_Graph, options);
    if (!code.success) {
        m_PreviewError = code.errorMessage.empty()
                             ? "Error de compilacion del grafo (revisa conexiones / ciclos)"
                             : code.errorMessage;
        return;
    }

    SlipShader* newShader = SlipShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    if (!newShader || newShader->ID == 0) {
        if (newShader) {
            newShader->destroy();
            delete newShader;
        }
        m_PreviewError = "Fallo GL del GLSL generado (detalles en la consola)";
        return;
    }

    m_PreviewShader = std::shared_ptr<SlipShader>(newShader, [](SlipShader* s) {
        if (s) {
            s->destroy();
            delete s;
        }
    });
    m_PreviewValid = true;
}

void ShaderGraphPanel::EnsurePreviewResources() {
    if (!m_SphereInitialized) {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        SlipPrimitive::GenerateSphere(0.5f, 24, 24, vertices, indices);
        m_SphereIndexCount = static_cast<unsigned int>(indices.size());

        // Rellenar tangentes/bitangentes a partir de las normales (uso del shader compilado)
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

    if (!m_WhiteTexInitialized) {
        unsigned char whitePixel[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &m_WhiteTex);
        glBindTexture(GL_TEXTURE_2D, m_WhiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_WhiteTexInitialized = true;
    }

    if (!m_PreviewShader && m_PreviewError.empty()) {
        RefreshPreviewShader();
    }
}

void ShaderGraphPanel::ResizePreviewFBO(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_PreviewFBO != 0 && m_PreviewW == width && m_PreviewH == height) return;

    m_PreviewW = width;
    m_PreviewH = height;

    if (m_PreviewFBO == 0) glGenFramebuffers(1, &m_PreviewFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_PreviewFBO);

    if (m_PreviewColorTex == 0) glGenTextures(1, &m_PreviewColorTex);
    glBindTexture(GL_TEXTURE_2D, m_PreviewColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PreviewColorTex, 0);

    if (m_PreviewDepthRBO == 0) glGenRenderbuffers(1, &m_PreviewDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_PreviewDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_PreviewDepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShaderGraphPanel::RenderPreviewOverlay(const ImVec2& canvasP0, const ImVec2& canvasP1) {
    (void)canvasP0;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = m_PreviewRect.Min;

    // Arrastre de la ventana de preview por su cabecera
    ImRect headerRect(pos, ImVec2(pos.x + m_PreviewSize.x, pos.y + m_PreviewHeaderSize.y));
    if (headerRect.Contains(io.MousePos) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DraggingPreview = true;
    }
    if (m_DraggingPreview && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
        m_PreviewOffsetRight -= io.MouseDelta.x;
        m_PreviewOffsetTop += io.MouseDelta.y;
        if (m_PreviewOffsetRight < 4.0f) m_PreviewOffsetRight = 4.0f;
        if (m_PreviewOffsetTop < 4.0f) m_PreviewOffsetTop = 4.0f;
    }
    if (m_DraggingPreview && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_DraggingPreview = false;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.13f, 0.97f));
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginChild("##ShaderGraphPreviewChild", ImVec2(m_PreviewSize.x, m_PreviewSize.y + m_PreviewHeaderSize.y), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();

    // --- Cabecera (arrastrable + controles) ---
    if (ImGui::Button("x##CloseShaderPreview")) {
        m_ShowPreview = false;
        ImGui::EndChild();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("A##RefreshShaderPreview")) {
        RefreshPreviewShader();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recompilar y actualizar preview");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "Preview");

    ImGui::Separator();

    // --- Cuerpo (render del material) ---
    ImVec2 bodyMin = ImGui::GetCursorScreenPos();
    float headerUsed = (bodyMin.y - pos.y);
    float bodyHeight = (m_PreviewSize.y + m_PreviewHeaderSize.y) - headerUsed - 24.0f;
    if (bodyHeight < 40.0f) bodyHeight = 40.0f;
    ImVec2 bodyMax = ImVec2(bodyMin.x + ImGui::GetContentRegionAvail().x, bodyMin.y + bodyHeight);

    EnsurePreviewResources();

    // Guardar cursor, renderizar (offscreen) y presentar la textura
    ImGui::GetWindowDrawList()->AddRectFilled(bodyMin, bodyMax, IM_COL32(18, 19, 24, 255));
    RenderPreviewFrame(bodyMin, bodyMax);

    // --- Estado bajo el cuerpo ---
    ImGui::SetCursorScreenPos(ImVec2(bodyMin.x, bodyMax.y));
    if (m_PreviewValid) {
        ImGui::Checkbox("Turntable", &m_PreviewTurntable);
    } else if (!m_PreviewError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::TextWrapped("%s", m_PreviewError.substr(0, 400).c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Compilando...");
    }

    ImGui::EndChild();
}

void ShaderGraphPanel::RenderPreviewFrame(const ImVec2& bodyMin, const ImVec2& bodyMax) {
    int width = static_cast<int>(bodyMax.x - bodyMin.x);
    int height = static_cast<int>(bodyMax.y - bodyMin.y);
    if (width < 32) width = 32;
    if (height < 32) height = 32;

    ImGuiIO& io = ImGui::GetIO();
    bool previewBodyHovered =
                              io.MousePos.x >= bodyMin.x && io.MousePos.x <= bodyMax.x &&
                              io.MousePos.y >= bodyMin.y && io.MousePos.y <= bodyMax.y;

    // Ã“rbita y zoom con el ratÃ³n dentro del cuerpo del preview
    if (previewBodyHovered) {
        if (io.MouseWheel != 0.0f) {
            m_PreviewDist = std::clamp(m_PreviewDist - io.MouseWheel * 0.25f, 1.2f, 8.0f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            m_PreviewYaw += io.MouseDelta.x * 0.5f;
            m_PreviewPitch = std::clamp(m_PreviewPitch + io.MouseDelta.y * 0.5f, -85.0f, 85.0f);
            m_PreviewTurntable = false;
        }
    }

    // ---------- Render offscreen ----------
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLfloat prevClearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLsizei prevCullMode = 0;
    GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    if (cullWasEnabled) glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);

    ResizePreviewFBO(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_PreviewFBO);
    glViewport(0, 0, m_PreviewW, m_PreviewH);
    glClearColor(0.14f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_PreviewTime += SlipGlobals::Get().GetDeltaTime();

    if (m_PreviewValid && m_PreviewShader) {
        glBindTexture(GL_TEXTURE_2D, 0);
        SlipShader* shader = m_PreviewShader.get();
        shader->use();

        // Animate Turntable en el propio modelo (esfera centrada en el origen)
        float yRot = m_PreviewTurntable ? (m_PreviewTime * 0.6f) : glm::radians(m_PreviewYaw);
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), yRot + glm::radians(m_PreviewYaw), glm::vec3(0, 1, 0));

        BindPreviewUniforms(shader, model, static_cast<float>(m_PreviewW) / static_cast<float>(m_PreviewH), m_PreviewTime);

        // Dibujar la esfera
        glBindVertexArray(m_SphereVAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_SphereIndexCount), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // ---------- Restaurar estado GL ----------
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) { glEnable(GL_CULL_FACE); glCullFace(static_cast<GLenum>(prevCullMode)); } else glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);

    // ---------- Mostrar en ImGui ----------
    ImTextureID texId = static_cast<ImTextureID>(static_cast<uintptr_t>(m_PreviewColorTex));
    ImGui::Image(texId, ImVec2(static_cast<float>(width), static_cast<float>(height)), ImVec2(0, 1), ImVec2(1, 0));
}

// ============================================================
// Preview por nodo (estilo Unity Shader Graph)
// ============================================================
void ShaderGraphPanel::BindPreviewUniforms(SlipShader* shader, const glm::mat4& model, float aspect, float time) {
    float yawRad = glm::radians(m_PreviewYaw);
    float pitchRad = glm::radians(m_PreviewPitch);
    glm::vec3 camPos = m_PreviewDist * glm::vec3(
        cos(pitchRad) * cos(yawRad),
        sin(pitchRad),
        cos(pitchRad) * sin(yawRad));
    glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

    shader->setMat4("u_Model", model);
    shader->setMat4("u_View", view);
    shader->setMat4("u_Projection", proj);
    shader->setVec3("u_CameraPos", camPos);
    shader->setFloat("u_Time", time);
    shader->setFloat("u_DeltaTime", ImGui::GetIO().DeltaTime);
    shader->setBool("u_HasBones", false);
    shader->setInt("u_SkinningQuality", 0);

    if (m_Graph.materialType == SlipShaderGraph::ShaderGraphType::Lit) {
        shader->setBool("u_ShadowsEnabled", false);
        shader->setBool("u_DDGIEnabled", false);
        shader->setBool("u_HasEnvCube", false);
        shader->setInt("u_NumDirLights", 1);
        shader->setVec3("u_DirLights[0].direction", -0.5f, -1.0f, -0.5f);
        shader->setVec3("u_DirLights[0].color", 1.0f, 1.0f, 1.0f);
        shader->setFloat("u_DirLights[0].intensity", 2.0f);
        shader->setInt("u_NumPointLights", 0);
        shader->setInt("u_NumSpotLights", 0);
    }

    // u_MainTexture: textura blanca de 1x1 en la unidad 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTex);
    shader->setInt("u_MainTexture", 0);

    // Propiedades expuestas (uniforms u_<Nombre>)
    unsigned int texUnit = 20;
    if (texUnit < 32) {
        for (const auto& prop : m_Graph.properties) {
            std::string uniformName = prop.GetUniformName();
            switch (prop.type) {
                case SlipShaderGraph::PinType::Float:
                    shader->setFloat(uniformName, prop.defaultValue.x);
                    break;
                case SlipShaderGraph::PinType::Vec2:
                    shader->setVec2(uniformName, glm::vec2(prop.defaultValue.x, prop.defaultValue.y));
                    break;
                case SlipShaderGraph::PinType::Vec3:
                    shader->setVec3(uniformName, glm::vec3(prop.defaultValue.x, prop.defaultValue.y, prop.defaultValue.z));
                    break;
                case SlipShaderGraph::PinType::Vec4:
                    shader->setVec4(uniformName, prop.defaultValue);
                    break;
                case SlipShaderGraph::PinType::Texture2D: {
                    if (texUnit >= 32) break;
                    if (!prop.defaultTexturePath.empty() && SlipFileSystem::Exists(prop.defaultTexturePath)) {
                        SlipTexture* tex = SlipTexture::load(prop.defaultTexturePath.c_str());
                        if (tex && tex->getID() != 0) {
                            shader->setInt(uniformName, static_cast<int>(texUnit));
                            tex->bind(texUnit);
                            texUnit++;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void ShaderGraphPanel::ReleaseNodePreview(NodePreviewCache& entry) {
    if (entry.shader) {
        entry.shader->destroy();
        delete entry.shader;
        entry.shader = nullptr;
    }
    if (entry.rbo != 0) { glDeleteRenderbuffers(1, &entry.rbo); entry.rbo = 0; }
    if (entry.fbo != 0) { glDeleteFramebuffers(1, &entry.fbo); entry.fbo = 0; }
    if (entry.tex != 0) { glDeleteTextures(1, &entry.tex); entry.tex = 0; }
    entry.valid = false;
    entry.error.clear();
}

void ShaderGraphPanel::InvalidateNodePreviews() {
    ++m_PreviewVersion;
    m_NodePreviewsDirty = true;
    m_PreviewStableTime = 0.0f;
}

void ShaderGraphPanel::EnsureNodePreviewFBO(NodePreviewCache& entry) {
    if (entry.tex != 0 && entry.fbo != 0 && entry.rbo != 0) return;

    if (entry.tex == 0) glGenTextures(1, &entry.tex);
    if (entry.fbo == 0) glGenFramebuffers(1, &entry.fbo);
    if (entry.rbo == 0) glGenRenderbuffers(1, &entry.rbo);

    glBindTexture(GL_TEXTURE_2D, entry.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, entry.size, entry.size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, entry.tex, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, entry.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, entry.size, entry.size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, entry.rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShaderGraphPanel::UpdateNextNodePreview() {
    if (!m_ShowNodePreviews) return;

    // Buscar un nodo con version desactualizada (el seleccionado tiene prioridad)
    SlipShaderGraph::ShaderNode* target = nullptr;
    SlipShaderGraph::ShaderNode* selected = FindNodeById(m_SelectedNodeId);
    if (selected && (dynamic_cast<SlipShaderGraph::MasterNode*>(selected) == nullptr)) {
        auto it = m_NodePreviews.find(selected->id);
        if (it == m_NodePreviews.end() || it->second.version != m_PreviewVersion) {
            target = selected;
        }
    }
    if (!target) {
        for (const auto& node : m_Graph.nodes) {
            if (!node) continue;
            if (dynamic_cast<SlipShaderGraph::MasterNode*>(node.get()) != nullptr) continue;
            bool isMasterTitle = false;
            (void)isMasterTitle;
            auto it = m_NodePreviews.find(node->id);
            if (it == m_NodePreviews.end() || it->second.version != m_PreviewVersion) {
                target = node.get();
                break;
            }
        }
    }

    if (!target) {
        // Limpiar previews de nodos eliminados
        for (auto it = m_NodePreviews.begin(); it != m_NodePreviews.end(); ) {
            if (!m_Graph.FindNode(it->first)) {
                ReleaseNodePreview(it->second);
                it = m_NodePreviews.erase(it);
            } else {
                ++it;
            }
        }
        m_NodePreviewsDirty = false;
        return;
    }

    // Regenerar (1 nodo por frame)
    NodePreviewCache& entry = m_NodePreviews[target->id];
    ReleaseNodePreview(entry);
    entry.version = m_PreviewVersion;
    entry.error.clear();

    SlipShaderGraph::ShaderCompilerOptions options;
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    options.targetVersion = SlipShaderGraph::GLSLTargetVersion::Mobile_GLES300_ES;
#else
    options.targetVersion = SlipShaderGraph::GLSLTargetVersion::Desktop_GL330_Core;
#endif

    SlipShaderGraph::GeneratedShaderCode code =
        SlipShaderGraph::ShaderGraphCompiler::CompileNodePreview(m_Graph, target->id, std::string(), options);
    if (!code.success) {
        entry.error = code.errorMessage;
        return;
    }

    SlipShader* shader = SlipShader::CreateFromSource(code.vertexSource.c_str(), code.fragmentSource.c_str());
    if (!shader) {
        entry.error = "Fallo al compilar GL del preview del nodo (ver consola).";
        return;
    }
    entry.shader = shader;
    entry.valid = true;
    EnsureNodePreviewFBO(entry);
}

void ShaderGraphPanel::RenderNodePreviews() {
    if (!m_ShowNodePreviews || m_NodePreviews.empty()) return;
    EnsurePreviewResources();
    if (m_SphereIndexCount == 0) return;

    bool anyValid = false;
    for (auto& [id, entry] : m_NodePreviews) {
        if (entry.valid && entry.shader) { anyValid = true; break; }
    }
    if (!anyValid) return;

    // ---------- Guardar estado GL ----------
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLfloat prevClearColor[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLsizei prevCullMode = 0;
    GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    if (cullWasEnabled) glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);

    glm::mat4 model = glm::rotate(glm::mat4(1.0f), m_NodePreviewTime * 0.6f, glm::vec3(0, 1, 0));

    for (auto& [nodeId, entry] : m_NodePreviews) {
        if (!entry.valid || !entry.shader) continue;

        EnsureNodePreviewFBO(entry);
        glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
        glViewport(0, 0, entry.size, entry.size);
        glClearColor(0.14f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        if (cullWasEnabled) { glEnable(GL_CULL_FACE); glCullFace(static_cast<GLenum>(prevCullMode)); }

        entry.shader->use();
        BindPreviewUniforms(entry.shader, model, 1.0f, m_NodePreviewTime);

        glBindVertexArray(m_SphereVAO);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_SphereIndexCount), GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    // ---------- Restaurar estado GL ----------
    glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullWasEnabled) { glEnable(GL_CULL_FACE); glCullFace(static_cast<GLenum>(prevCullMode)); } else glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
}

void ShaderGraphPanel::RenderExpandedPreviewWindow(const ImVec2& canvasP0, ImDrawList* drawList) {
    if (m_ExpandedPreviewNodeId.empty()) {
        m_ExpandedPreviewRect = ImRect(0, 0, 0, 0);
        return;
    }

    SlipShaderGraph::ShaderNode* node = FindNodeById(m_ExpandedPreviewNodeId);
    if (!node) {
        m_ExpandedPreviewNodeId.clear();
        m_ExpandedPreviewRect = ImRect(0, 0, 0, 0);
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 anchor = NodeTopLeft(canvasP0, *node);
    ImVec2 winPos(anchor.x + m_ExpandedPreviewOffset.x, anchor.y + m_ExpandedPreviewOffset.y);

    float winW = m_ExpandedPreviewSize;
    float headerH = 22.0f;
    float winH = headerH + m_ExpandedPreviewSize;
    ImVec2 winMax(winPos.x + winW, winPos.y + winH);
    m_ExpandedPreviewRect = ImRect(winPos, winMax);

    // Marco
    drawList->AddRectFilled(winPos, winMax, IM_COL32(18, 19, 24, 248), 7.0f);
    drawList->AddRect(winPos, winMax, IM_COL32(90, 150, 255, 190), 7.0f, 0, 1.4f);
    drawList->AddRectFilled(winPos, ImVec2(winMax.x, winPos.y + headerH), IM_COL32(38, 44, 66, 255), 7.0f, ImDrawFlags_RoundCornersTop);

    // Titulo
    drawList->AddText(ImVec2(winPos.x + 8.0f, winPos.y + 4.0f), IM_COL32(235, 240, 255, 255), node->title.c_str());

    // Arrastre por la cabecera
    ImRect headerRect(winPos, ImVec2(winMax.x - 20.0f, winPos.y + headerH));
    if (headerRect.Contains(io.MousePos) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DraggingExpandedPreview = true;
    }
    if (m_DraggingExpandedPreview && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
        m_ExpandedPreviewOffset.x += io.MouseDelta.x;
        m_ExpandedPreviewOffset.y += io.MouseDelta.y;
    }
    if (m_DraggingExpandedPreview && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_DraggingExpandedPreview = false;
    }

    // Boton cerrar (x)
    ImVec2 xBtnMin(winMax.x - 16.0f, winPos.y + 4.0f);
    ImVec2 xBtnMax(winMax.x - 4.0f, winPos.y + headerH - 2.0f);
    ImRect xBtnRect(xBtnMin, xBtnMax);
    if (xBtnRect.Contains(io.MousePos)) {
        drawList->AddRectFilled(xBtnMin, xBtnMax, IM_COL32(160, 60, 60, 255), 3.0f);
    }
    drawList->AddText(ImVec2(xBtnMin.x + 4.0f, xBtnMin.y + 1.0f), IM_COL32(255, 200, 200, 255), "x");
    if (xBtnRect.Contains(io.MousePos) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_ExpandedPreviewNodeId.clear();
        m_ExpandedPreviewRect = ImRect(0, 0, 0, 0);
        return;
    }

    // Cuerpo con la textura del preview del nodo
    auto entryIt = m_NodePreviews.find(node->id);
    if (entryIt != m_NodePreviews.end() && entryIt->second.valid && entryIt->second.tex != 0) {
        ImTextureID tex = static_cast<ImTextureID>(static_cast<uintptr_t>(entryIt->second.tex));
        drawList->AddImageRounded(tex, ImVec2(winPos.x + 4.0f, winPos.y + headerH + 2.0f),
                                  ImVec2(winMax.x - 4.0f, winMax.y - 4.0f),
                                  ImVec2(0, 1), ImVec2(1, 0), IM_COL32(255, 255, 255, 255), 5.0f);
    } else {
        drawList->AddRectFilled(ImVec2(winPos.x + 4.0f, winPos.y + headerH + 2.0f),
                                ImVec2(winMax.x - 4.0f, winMax.y - 4.0f), IM_COL32(30, 32, 40, 255), 5.0f);
        const char* msg = "Preview no disponible";
        if (entryIt != m_NodePreviews.end() && !entryIt->second.error.empty()) {
            std::string err = entryIt->second.error.substr(0, 220);
            drawList->AddText(ImVec2(winPos.x + 10.0f, winPos.y + headerH + 8.0f),
                              IM_COL32(255, 120, 120, 255), err.c_str());
        } else {
            drawList->AddText(ImVec2(winPos.x + 10.0f, winPos.y + headerH + 8.0f),
                              IM_COL32(150, 155, 170, 255), msg);
        }
    }
}
