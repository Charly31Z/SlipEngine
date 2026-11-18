#include "SlipEngine/editor/panels/PrefabEditorPanel.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include <imgui.h>

PrefabEditorPanel::PrefabEditorPanel() {
    m_IsOpen = false;
    m_PrefabLevel = std::make_unique<SlipLevel>();
}

PrefabEditorPanel::~PrefabEditorPanel() {
    ClosePrefab();
}

void PrefabEditorPanel::OpenPrefab(const std::string& filePath) {
    ClosePrefab();

    m_PrefabPath = filePath;
    m_PrefabName = SlipFileSystem::GetFileNameWithoutExtension(filePath);
    m_PrefabLevel = std::make_unique<SlipLevel>();

    m_RootEntity = SlipPrefab::LoadPrefab(m_PrefabPath, m_PrefabLevel.get());
    if (m_RootEntity) {
        m_SelectedEntity = m_RootEntity;
        m_IsOpen = true;
        m_IsModified = false;
        SlipConsole::LogInfo("Prefab abierto para edición aislada: " + m_PrefabName);
    } else {
        SlipConsole::LogError("Error al abrir Prefab: " + filePath);
    }
}

void PrefabEditorPanel::SavePrefab() {
    if (!m_RootEntity || m_PrefabPath.empty()) return;
    if (SlipPrefab::SavePrefab(m_PrefabPath, m_RootEntity)) {
        m_IsModified = false;
        SlipConsole::LogSuccess("Prefab guardado exitosamente: " + m_PrefabPath);
    }
}

void PrefabEditorPanel::ClosePrefab() {
    m_SelectedEntity = nullptr;
    m_RootEntity = nullptr;
    if (m_PrefabLevel) {
        m_PrefabLevel->ClearLevel();
    }
    m_PrefabPath.clear();
    m_PrefabName.clear();
    m_IsModified = false;
}

void PrefabEditorPanel::OnImGuiRender() {
    if (!m_IsOpen) {
        if (m_RootEntity) ClosePrefab();
        return;
    }

    std::string title = "Editor de Prefabs";
    if (!m_PrefabName.empty()) {
        title += " - " + m_PrefabName;
        if (m_IsModified) title += " *";
    }
    title += "###PrefabEditor";

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(title.c_str(), &m_IsOpen, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Archivo")) {
                if (ImGui::MenuItem("Guardar Prefab", "Ctrl+S", false, m_RootEntity != nullptr)) {
                    SavePrefab();
                }
                if (ImGui::MenuItem("Cerrar Prefab")) {
                    m_IsOpen = false;
                    ClosePrefab();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            SavePrefab();
        }

        if (!m_RootEntity) {
            ImGui::TextDisabled("No hay ningún prefab abierto.");
            ImGui::TextDisabled("Haz doble clic en cualquier archivo .prefab en el Explorador de Archivos para editarlo.");
            ImGui::End();
            return;
        }

        // Barra superior de herramientas del Prefab
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Prefab: %s", m_PrefabPath.c_str());
        ImGui::SameLine();
        if (ImGui::Button(" Guardar Cambios ")) {
            SavePrefab();
        }
        ImGui::SameLine();
        if (ImGui::Button(" Instanciar en Escena ")) {
            if (m_ContextLevel) {
                SlipEntity* inst = SlipPrefab::Instantiate(m_PrefabPath, m_ContextLevel);
                if (inst) {
                    SlipConsole::LogSuccess("Instanciado '" + m_PrefabName + "' en la escena actual.");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(" Cerrar ")) {
            m_IsOpen = false;
            ClosePrefab();
            ImGui::End();
            return;
        }

        ImGui::Separator();

        // División en 2 columnas: Jerarquía del Prefab a la izquierda, Inspector a la derecha
        ImGui::Columns(2, "PrefabEditorColumns", true);

        // Columna 1: Jerarquía
        ImGui::Text("Jerarquía del Prefab");
        ImGui::Separator();
        RenderPrefabHierarchy();

        ImGui::NextColumn();

        // Columna 2: Inspector
        ImGui::Text("Inspector de Entidad");
        ImGui::Separator();
        RenderPrefabInspector();

        ImGui::Columns(1);
    }
    ImGui::End();
}

void PrefabEditorPanel::RenderPrefabHierarchy() {
    if (!m_RootEntity) return;

    std::function<void(SlipEntity*)> drawPrefabNode = [&](SlipEntity* entity) {
        if (!entity) return;

        bool isSelected = (entity == m_SelectedEntity);
        bool hasChildren = !entity->GetChildren().empty();

        ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) |
                                   (hasChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf) |
                                   ImGuiTreeNodeFlags_DefaultOpen |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;

        bool opened = ImGui::TreeNodeEx((void*)entity, flags, "%s", entity->GetName().c_str());

        if (ImGui::IsItemClicked()) {
            m_SelectedEntity = entity;
        }

        if (opened) {
            for (SlipEntity* child : entity->GetChildren()) {
                drawPrefabNode(child);
            }
            ImGui::TreePop();
        }
    };

    drawPrefabNode(m_RootEntity);
}

void PrefabEditorPanel::RenderPrefabInspector() {
    if (!m_SelectedEntity) {
        ImGui::TextDisabled("Selecciona una entidad en la jerarquía del prefab.");
        return;
    }

    // Nombre
    char nameBuf[128];
    strncpy(nameBuf, m_SelectedEntity->GetName().c_str(), sizeof(nameBuf));
    if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
        m_SelectedEntity->SetName(nameBuf);
        m_IsModified = true;
    }
    // Validar unicidad del nombre al terminar la edición (sufijo automático _NNN)
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string uniqueName = SlipPrefab::MakeUniqueName(m_SelectedEntity->GetName(), m_PrefabLevel.get(), m_SelectedEntity);
        if (uniqueName != m_SelectedEntity->GetName()) {
            m_SelectedEntity->SetName(uniqueName);
            m_IsModified = true;
        }
    }

    ImGui::Separator();

    // Transform
    if (auto tc = m_SelectedEntity->GetComponent<TransformComponent>()) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat3("Posición", &tc->position.x, 0.1f)) {
                m_SelectedEntity->position = tc->position;
                m_IsModified = true;
            }
            if (ImGui::DragFloat3("Rotación", &tc->rotation.x, 0.5f)) {
                m_SelectedEntity->rotation = tc->rotation;
                m_IsModified = true;
            }
            if (ImGui::DragFloat3("Escala", &tc->scale.x, 0.05f, 0.001f, 100.0f)) {
                m_SelectedEntity->scale = tc->scale;
                m_IsModified = true;
            }
        }
    }

    // Scripts y sus variables expuestas
    for (auto sc : m_SelectedEntity->GetScripts()) {
        if (!sc) continue;
        std::string scriptTitle = sc->GetScriptFileName() + " (Script)###PrefabScript_" + std::to_string((uintptr_t)sc.get());
        if (ImGui::CollapsingHeader(scriptTitle.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Ruta: %s", sc->scriptPath.c_str());
            bool enabled = sc->IsEnabled();
            if (ImGui::Checkbox("Habilitado", &enabled)) {
                sc->SetEnabled(enabled);
                m_IsModified = true;
            }

            auto& props = sc->GetProperties();
            if (!props.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Variables Públicas:");
                for (auto& prop : props) {
                    ImGui::PushID(prop.name.c_str());
                    switch (prop.type) {
                        case ScriptPropertyType::Float: {
                            float val = prop.floatValue;
                            if (ImGui::DragFloat(prop.name.c_str(), &val, 0.1f)) {
                                sc->SetPropertyValue(prop.name, val);
                                m_IsModified = true;
                            }
                            break;
                        }
                        case ScriptPropertyType::Int: {
                            int val = prop.intValue;
                            if (ImGui::DragInt(prop.name.c_str(), &val, 1)) {
                                sc->SetPropertyValue(prop.name, val);
                                m_IsModified = true;
                            }
                            break;
                        }
                        case ScriptPropertyType::Bool: {
                            bool val = prop.boolValue;
                            if (ImGui::Checkbox(prop.name.c_str(), &val)) {
                                sc->SetPropertyValue(prop.name, val);
                                m_IsModified = true;
                            }
                            break;
                        }
                        case ScriptPropertyType::String: {
                            char strBuf[256];
                            strncpy(strBuf, prop.stringValue.c_str(), sizeof(strBuf));
                            if (ImGui::InputText(prop.name.c_str(), strBuf, sizeof(strBuf))) {
                                sc->SetPropertyValue(prop.name, std::string(strBuf));
                                m_IsModified = true;
                            }
                            break;
                        }
                        case ScriptPropertyType::Vec3: {
                            glm::vec3 val = prop.vec3Value;
                            if (ImGui::DragFloat3(prop.name.c_str(), &val.x, 0.1f)) {
                                sc->SetPropertyValue(prop.name, val);
                                m_IsModified = true;
                            }
                            break;
                        }
                    }
                    ImGui::PopID();
                }
            }
        }
    }
}

