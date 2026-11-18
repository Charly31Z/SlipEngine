#include "SlipEngine/editor/panels/HierarchyPanel.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipTerrainComponent.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/editor/panels/AnimationEditorPanel.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/editor/panels/ViewportPanel.h"
#include <imgui.h>
#include <algorithm>

HierarchyPanel::HierarchyPanel(SelectionCallback onSelect)
    : m_OnSelectCallback(onSelect) {
    SlipLevel::SetOnEntityDestroyedCallback([this](SlipEntity* deadEnt) {
        // Purgar la entidad destruida de la multi-selección
        if (IsEntitySelected(deadEnt) || m_SelectedEntity == deadEnt) {
            std::vector<SlipEntity*> remaining;
            for (SlipEntity* e : m_Selection) {
                if (e && e != deadEnt) remaining.push_back(e);
            }
            SlipEntity* newPrimary = (m_SelectedEntity == deadEnt)
                ? (remaining.empty() ? nullptr : remaining.back())
                : m_SelectedEntity;
            ApplySelection(remaining, newPrimary);
            if (m_OnMultiSelectCallback) {
                m_OnMultiSelectCallback(remaining, newPrimary);
            } else if (!remaining.empty() && m_OnSelectCallback) {
                m_OnSelectCallback(newPrimary);
            }
        }
        if (auto aep = AnimationEditorPanel::GetInstance()) {
            if (aep->GetTargetEntity() == deadEnt) {
                aep->SetTargetEntity(nullptr);
            }
        }
    });
}

void HierarchyPanel::SelectEntities(const std::vector<SlipEntity*>& selection, SlipEntity* primary) {
    ApplySelection(selection, primary);
    if (m_OnMultiSelectCallback) m_OnMultiSelectCallback(selection, primary);
}

void HierarchyPanel::DuplicateSelected() {
    if (!m_SelectedEntity || !m_ContextLevel) return;
    if (SlipEditor::GetInstance()) {
        SlipEditor::GetInstance()->DuplicateSelection();
    }
}

void HierarchyPanel::CopySelected() {
    if (!m_SelectedEntity) return;
    if (SlipEditor::GetInstance()) {
        SlipEditor::GetInstance()->CopySelection();
    }
}

void HierarchyPanel::CutSelected() {
    if (!m_SelectedEntity) return;
    if (SlipEditor::GetInstance()) {
        SlipEditor::GetInstance()->CutSelection();
    }
}

void HierarchyPanel::Paste() {
    if (!m_ContextLevel) return;
    if (SlipEditor::GetInstance()) {
        // Pegar como hijo de la entidad seleccionada actualmente (si la hay)
        SlipEditor::GetInstance()->PasteFromClipboard(m_SelectedEntity);
    }
}

void HierarchyPanel::SaveSelectedAsPrefab() {
    if (!m_SelectedEntity) return;

    std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
    if (assetsDir.empty()) assetsDir = "assets";
    std::string prefabsDir = SlipFileSystem::CombinePath(assetsDir, "Prefabs");
    SlipFileSystem::CreateDirectoryPath(prefabsDir);

    std::string prefabPath = SlipFileSystem::CombinePath(prefabsDir, m_SelectedEntity->GetName() + ".prefab");
    if (SlipPrefab::SavePrefab(prefabPath, m_SelectedEntity)) {
        SlipConsole::LogSuccess("Prefab creado exitosamente en: " + prefabPath);
    }
}

void HierarchyPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    if (ImGui::Begin("Jerarquía de Escena", &m_IsOpen)) {
        ImGuiIO& io = ImGui::GetIO();
        bool isFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool hasClipboard = SlipEditor::GetInstance() && SlipEditor::GetInstance()->HasEntityClipboard();

        // Atajos de teclado estilo Unity3D
        if (isFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            DuplicateSelected();
        }
        if (isFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            CopySelected();
        }
        if (isFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            CutSelected();
        }
        if (isFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && hasClipboard) {
            Paste();
        }
        if (isFocused && m_SelectedEntity && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (SlipEditor::GetInstance()) {
                SlipEditor::GetInstance()->DeleteSelection();
            } else if (m_ContextLevel) {
                SlipEntity* toDel = m_SelectedEntity;
                if (m_OnSelectCallback) m_OnSelectCallback(nullptr);
                m_ContextLevel->DestroyEntity(toDel);
            }
        }
        // Tecla F: enfocar la cámara del viewport en la selección
        if (isFocused && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (SlipEditor::GetInstance() && SlipEditor::GetInstance()->GetViewportPanel()) {
                SlipEditor::GetInstance()->GetViewportPanel()->FocusOnSelection();
            }
        }

        // Botón rápido superior para crear objetos
        if (ImGui::Button(" [+] Crear Objeto ")) {
            ImGui::OpenPopup("HierarchyCreateMenu");
        }
        ImGui::SameLine();
        if (ImGui::Button(" Pegar ") && hasClipboard) {
            Paste();
        }

        auto makeUniqueName = [&](const std::string& baseName) -> std::string {
            return SlipPrefab::MakeUniqueName(baseName, m_ContextLevel);
        };

        if (ImGui::BeginPopup("HierarchyCreateMenu")) {
            if (ImGui::MenuItem("Cubo (Mesh + BoxCollider)")) {
                if (m_ContextLevel) {
                    SlipEntity* cube = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Cube);
                    cube->SetName(makeUniqueName("Cubo"));
                    m_ContextLevel->AddEntity(cube);
                    m_SelectedEntity = cube;
                    if (m_OnSelectCallback) m_OnSelectCallback(cube);
                }
            }
            if (ImGui::MenuItem("Esfera (Mesh + SphereCollider)")) {
                if (m_ContextLevel) {
                    SlipEntity* sphere = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Sphere);
                    sphere->SetName(makeUniqueName("Esfera"));
                    m_ContextLevel->AddEntity(sphere);
                    m_SelectedEntity = sphere;
                    if (m_OnSelectCallback) m_OnSelectCallback(sphere);
                }
            }
            if (ImGui::MenuItem("Plano (Mesh + BoxCollider)")) {
                if (m_ContextLevel) {
                    SlipEntity* plane = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Plane);
                    plane->SetName(makeUniqueName("Plano"));
                    m_ContextLevel->AddEntity(plane);
                    m_SelectedEntity = plane;
                    if (m_OnSelectCallback) m_OnSelectCallback(plane);
                }
            }
            if (ImGui::MenuItem("Terreno (V-Clust)")) {
                if (m_ContextLevel) {
                    SlipEntity* terrainEnt = new SlipEntity(makeUniqueName("Terreno"));
                    terrainEnt->SetStatic(true);
                    auto tc = terrainEnt->AddComponent<SlipTerrainComponent>();
                    tc->RebuildTerrain();
                    m_ContextLevel->AddEntity(terrainEnt);
                    m_SelectedEntity = terrainEnt;
                    if (m_OnSelectCallback) m_OnSelectCallback(terrainEnt);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Luz Puntual")) {
                if (m_ContextLevel) {
                    SlipEntity* light = new SlipEntity(makeUniqueName("Luz Puntual"));
                    light->AddComponent<LightComponent>(LightType::Point);
                    m_ContextLevel->AddEntity(light);
                    m_SelectedEntity = light;
                    if (m_OnSelectCallback) m_OnSelectCallback(light);
                }
            }
            if (ImGui::MenuItem("Luz Direccional")) {
                if (m_ContextLevel) {
                    SlipEntity* light = new SlipEntity(makeUniqueName("Luz Direccional"));
                    light->AddComponent<LightComponent>(LightType::Directional);
                    m_ContextLevel->AddEntity(light);
                    m_SelectedEntity = light;
                    if (m_OnSelectCallback) m_OnSelectCallback(light);
                }
            }
            if (ImGui::MenuItem("Luz Spot")) {
                if (m_ContextLevel) {
                    SlipEntity* light = new SlipEntity(makeUniqueName("Luz Spot"));
                    light->AddComponent<LightComponent>(LightType::Spot);
                    m_ContextLevel->AddEntity(light);
                    m_SelectedEntity = light;
                    if (m_OnSelectCallback) m_OnSelectCallback(light);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Volumen de post-proceso")) {
                if (m_ContextLevel) {
                    SlipEntity* volume = new SlipEntity(makeUniqueName("Post Process Volume"));
                    volume->AddComponent<PostProcessVolumeComponent>();
                    m_ContextLevel->AddEntity(volume);
                    m_SelectedEntity = volume;
                    if (m_OnSelectCallback) m_OnSelectCallback(volume);
                }
            }
            if (ImGui::MenuItem("Cámara (Camera)")) {
                if (m_ContextLevel) {
                    SlipEntity* camEnt = new SlipEntity(makeUniqueName("Cámara"));
                    camEnt->position = glm::vec3(0.0f, 2.0f, 5.0f);
                    if (auto tc = camEnt->GetComponent<TransformComponent>()) {
                        tc->position = camEnt->position;
                    }
                    camEnt->AddComponent<CameraComponent>();
                    m_ContextLevel->AddEntity(camEnt);
                    m_SelectedEntity = camEnt;
                    if (m_OnSelectCallback) m_OnSelectCallback(camEnt);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Entidad Vacía")) {
                if (m_ContextLevel) {
                    SlipEntity* ent = new SlipEntity(makeUniqueName("Nueva Entidad"));
                    m_ContextLevel->AddEntity(ent);
                    m_SelectedEntity = ent;
                    if (m_OnSelectCallback) m_OnSelectCallback(ent);
                }
            }
            if (ImGui::MenuItem("Skinned Mesh (Malla Deformable con Huesos)")) {
                if (m_ContextLevel) {
                    SlipEntity* smEnt = new SlipEntity(makeUniqueName("SkinnedMesh"));
                    smEnt->AddComponent<SkinnedMeshComponent>();
                    smEnt->AddComponent<AnimatorComponent>();
                    m_ContextLevel->AddEntity(smEnt);
                    m_SelectedEntity = smEnt;
                    if (m_OnSelectCallback) m_OnSelectCallback(smEnt);
                }
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Efectos / Partículas")) {
                if (ImGui::MenuItem("Sistema de Partículas (Billboard - Estilo Unity)")) {
                    if (m_ContextLevel) {
                        SlipEntity* psEnt = new SlipEntity(makeUniqueName("Particle System"));
                        psEnt->AddComponent<ParticleSystemComponent>(ParticleRenderMode::Billboard);
                        m_ContextLevel->AddEntity(psEnt);
                        m_SelectedEntity = psEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(psEnt);
                    }
                }
                if (ImGui::MenuItem("Sistema de Partículas Pesadas (Niagara GL_POINTS - Estilo Unreal)")) {
                    if (m_ContextLevel) {
                        SlipEntity* psEnt = new SlipEntity(makeUniqueName("Niagara Particle System"));
                        psEnt->AddComponent<ParticleSystemComponent>(ParticleRenderMode::PointNiagara);
                        m_ContextLevel->AddEntity(psEnt);
                        m_SelectedEntity = psEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(psEnt);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Objeto 2D")) {
                if (ImGui::MenuItem("Sprite 2D")) {
                    if (m_ContextLevel) {
                        SlipEntity* spriteEnt = new SlipEntity(makeUniqueName("Sprite 2D"));
                        spriteEnt->AddComponent<SpriteRendererComponent>();
                        m_ContextLevel->AddEntity(spriteEnt);
                        m_SelectedEntity = spriteEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(spriteEnt);
                    }
                }
                if (ImGui::MenuItem("Sprite Animado (2D Animator)")) {
                    if (m_ContextLevel) {
                        SlipEntity* animEnt = new SlipEntity(makeUniqueName("Sprite Animado"));
                        animEnt->AddComponent<SpriteRendererComponent>();
                        animEnt->AddComponent<SpriteAnimatorComponent>();
                        m_ContextLevel->AddEntity(animEnt);
                        m_SelectedEntity = animEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(animEnt);
                    }
                }
                if (ImGui::MenuItem("TileGrid 2D (Tilemap)")) {
                    if (m_ContextLevel) {
                        SlipEntity* tileEnt = new SlipEntity(makeUniqueName("TileGrid 2D"));
                        tileEnt->AddComponent<TileGridComponent>();
                        m_ContextLevel->AddEntity(tileEnt);
                        m_SelectedEntity = tileEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(tileEnt);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Luz 2D (Point Light)")) {
                    if (m_ContextLevel) {
                        SlipEntity* lightEnt = new SlipEntity(makeUniqueName("Luz 2D"));
                        auto l2d = lightEnt->AddComponent<Light2DComponent>();
                        l2d->lightType = Light2DType::Point;
                        m_ContextLevel->AddEntity(lightEnt);
                        m_SelectedEntity = lightEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(lightEnt);
                    }
                }
                if (ImGui::MenuItem("Luz 2D Focal (Spot Light)")) {
                    if (m_ContextLevel) {
                        SlipEntity* spotEnt = new SlipEntity(makeUniqueName("Luz 2D Focal"));
                        auto l2d = spotEnt->AddComponent<Light2DComponent>();
                        l2d->lightType = Light2DType::Spot;
                        m_ContextLevel->AddEntity(spotEnt);
                        m_SelectedEntity = spotEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(spotEnt);
                    }
                }
                if (ImGui::MenuItem("Sombra 2D (Shadow Caster)")) {
                    if (m_ContextLevel) {
                        SlipEntity* scEnt = new SlipEntity(makeUniqueName("Sombra 2D"));
                        scEnt->AddComponent<ShadowCaster2DComponent>();
                        m_ContextLevel->AddEntity(scEnt);
                        m_SelectedEntity = scEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(scEnt);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Editor de Sprites Pixel Art...")) {
                    if (SlipEditor::GetInstance()) {
                        SlipEditor::GetInstance()->OpenPixelArtEditor();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("UI (Interfaz)")) {
                auto ensureCanvas = [&]() -> SlipEntity* {
                    if (!m_ContextLevel) return nullptr;
                    if (m_SelectedEntity && (m_SelectedEntity->HasComponent<CanvasComponent>() || m_SelectedEntity->HasComponent<RectTransformComponent>())) {
                        return m_SelectedEntity;
                    }
                    for (SlipEntity* e : m_ContextLevel->GetEntities()) {
                        if (e && e->HasComponent<CanvasComponent>()) return e;
                    }
                    SlipEntity* canvasEnt = new SlipEntity(makeUniqueName("Canvas"));
                    canvasEnt->AddComponent<CanvasComponent>();
                    m_ContextLevel->AddEntity(canvasEnt);
                    return canvasEnt;
                };

                if (ImGui::MenuItem("Canvas")) {
                    if (m_ContextLevel) {
                        SlipEntity* canvasEnt = new SlipEntity(makeUniqueName("Canvas"));
                        canvasEnt->AddComponent<CanvasComponent>();
                        m_ContextLevel->AddEntity(canvasEnt);
                        m_SelectedEntity = canvasEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(canvasEnt);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Imagen (UI Image)")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* imgEnt = new SlipEntity(makeUniqueName("Imagen UI"));
                        auto rt = imgEnt->AddComponent<RectTransformComponent>();
                        rt->size = glm::vec2(200.0f, 200.0f);
                        imgEnt->AddComponent<UIImageComponent>();
                        m_ContextLevel->AddEntity(imgEnt);
                        imgEnt->SetParent(parent);
                        m_SelectedEntity = imgEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(imgEnt);
                    }
                }
                if (ImGui::MenuItem("Texto (UI Text)")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* txtEnt = new SlipEntity(makeUniqueName("Texto UI"));
                        auto rt = txtEnt->AddComponent<RectTransformComponent>();
                        rt->size = glm::vec2(260.0f, 60.0f);
                        txtEnt->AddComponent<UITextComponent>("Nuevo Texto");
                        m_ContextLevel->AddEntity(txtEnt);
                        txtEnt->SetParent(parent);
                        m_SelectedEntity = txtEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(txtEnt);
                    }
                }
                if (ImGui::MenuItem("Botón (UI Button)")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* btnEnt = new SlipEntity(makeUniqueName("Botón UI"));
                        auto rt = btnEnt->AddComponent<RectTransformComponent>();
                        rt->size = glm::vec2(220.0f, 56.0f);
                        btnEnt->AddComponent<UIButtonComponent>();
                        auto txt = btnEnt->AddComponent<UITextComponent>("Botón");
                        txt->fontSize = 22.0f;
                        m_ContextLevel->AddEntity(btnEnt);
                        btnEnt->SetParent(parent);
                        m_SelectedEntity = btnEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(btnEnt);
                    }
                }
                if (ImGui::MenuItem("Slider (UI Slider)")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* sldEnt = new SlipEntity(makeUniqueName("Slider UI"));
                        auto rt = sldEnt->AddComponent<RectTransformComponent>();
                        rt->size = glm::vec2(260.0f, 28.0f);
                        sldEnt->AddComponent<UISliderComponent>();
                        m_ContextLevel->AddEntity(sldEnt);
                        sldEnt->SetParent(parent);
                        m_SelectedEntity = sldEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(sldEnt);
                    }
                }
                if (ImGui::MenuItem("Barra de Progreso (Progress Bar)")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* pbEnt = new SlipEntity(makeUniqueName("Barra de Progreso UI"));
                        auto rt = pbEnt->AddComponent<RectTransformComponent>();
                        rt->size = glm::vec2(280.0f, 34.0f);
                        pbEnt->AddComponent<UIProgressBarComponent>();
                        m_ContextLevel->AddEntity(pbEnt);
                        pbEnt->SetParent(parent);
                        m_SelectedEntity = pbEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(pbEnt);
                    }
                }
                if (ImGui::MenuItem("Joystick")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* joyEnt = new SlipEntity(makeUniqueName("Joystick"));
                        auto rt = joyEnt->AddComponent<RectTransformComponent>();
                        rt->ApplyAnchorPreset(UIAnchorPreset::BottomLeft);
                        rt->size = glm::vec2(220.0f, 220.0f);
                        rt->anchoredPosition = glm::vec2(80.0f, 80.0f);
                        joyEnt->AddComponent<UIJoystickComponent>();
                        m_ContextLevel->AddEntity(joyEnt);
                        joyEnt->SetParent(parent);
                        m_SelectedEntity = joyEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(joyEnt);
                    }
                }
                if (ImGui::MenuItem("Botón de acción")) {
                    if (m_ContextLevel) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* actEnt = new SlipEntity(makeUniqueName("Botón de acción"));
                        auto rt = actEnt->AddComponent<RectTransformComponent>();
                        rt->ApplyAnchorPreset(UIAnchorPreset::BottomRight);
                        rt->size = glm::vec2(150.0f, 150.0f);
                        rt->anchoredPosition = glm::vec2(-90.0f, 90.0f);
                        actEnt->AddComponent<UIActionButtonComponent>();
                        m_ContextLevel->AddEntity(actEnt);
                        actEnt->SetParent(parent);
                        m_SelectedEntity = actEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(actEnt);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        if (m_ContextLevel) {
            SlipEntity* entityToDelete = nullptr;
            SlipEntity* entityToDuplicate = nullptr;
            SlipEntity* entityToCopy = nullptr;
            SlipEntity* entityToCut = nullptr;
            SlipEntity* entityToPasteParent = nullptr;
            SlipEntity* entityToUnparent = nullptr;

            static bool s_HierarchyLongPressFired = false;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                s_HierarchyLongPressFired = false;
            }

            // Función recursiva para dibujar entidades y sus hijos
            std::function<void(SlipEntity*)> drawNode = [&](SlipEntity* entity) {
                if (!entity) return;

                bool isSelected = IsEntitySelected(entity);
                bool hasChildren = !entity->GetChildren().empty();

                ImGuiTreeNodeFlags flags = (isSelected ? ImGuiTreeNodeFlags_Selected : 0) |
                                           (hasChildren ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf) |
                                           ImGuiTreeNodeFlags_SpanAvailWidth;

                bool opened = ImGui::TreeNodeEx((void*)entity, flags, "%s", entity->GetName().c_str());

                if (ImGui::IsItemClicked()) {
                    ImGuiIO& clickIO = ImGui::GetIO();
                    if (clickIO.KeyCtrl) {
                        // Ctrl+Clic: alternar la entidad dentro de la multi-selección
                        bool wasSelected = IsEntitySelected(entity);
                        std::vector<SlipEntity*> sel;
                        if (wasSelected) {
                            for (SlipEntity* e : m_Selection) {
                                if (e != entity) sel.push_back(e);
                            }
                        } else {
                            sel = m_Selection;
                            sel.push_back(entity);
                        }
                        SelectEntities(sel, entity);
                    } else if (clickIO.KeyShift && m_SelectedEntity && !m_OrderedEntities.empty()) {
                        // Shift+Clic: seleccionar el rango desde la entidad primaria hasta la clickeada
                        auto itA = std::find(m_OrderedEntities.begin(), m_OrderedEntities.end(), m_SelectedEntity);
                        auto itB = std::find(m_OrderedEntities.begin(), m_OrderedEntities.end(), entity);
                        if (itA != m_OrderedEntities.end() && itB != m_OrderedEntities.end()) {
                            if (itA > itB) std::swap(itA, itB);
                            std::vector<SlipEntity*> sel(itA, itB + 1);
                            SelectEntities(sel, entity);
                        } else {
                            if (m_OnSelectCallback) m_OnSelectCallback(entity);
                        }
                    } else {
                        m_SelectedEntity = entity;
                        if (m_OnSelectCallback) {
                            m_OnSelectCallback(entity);
                        }
                    }
                }

                // Drag & Drop: Origen de arrastre para emparentar
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("HIERARCHY_ENTITY_PAYLOAD", &entity, sizeof(SlipEntity*));
                    ImGui::Text("Mover '%s'", entity->GetName().c_str());
                    ImGui::EndDragDropSource();
                }

                // Drag & Drop: Destino de soltado para convertir en hijo
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY_PAYLOAD")) {
                        SlipEntity* dragged = *(SlipEntity**)payload->Data;
                        if (dragged && dragged != entity) {
                            dragged->SetParent(entity);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Detección de pulsación prolongada (Long-Press táctil / Android) sobre entidad
                bool isEntityLongPress = ImGui::IsItemActive() &&
                                         ImGui::GetIO().MouseDownDuration[0] >= 0.45f &&
                                         !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10.0f);
                if (isEntityLongPress && !s_HierarchyLongPressFired) {
                    s_HierarchyLongPressFired = true;
                    m_SelectedEntity = entity;
                    if (m_OnSelectCallback) m_OnSelectCallback(entity);
                    ImGui::OpenPopup("EntityContextMenu");
                }

                // Menú contextual sobre entidad
                if (ImGui::BeginPopup("EntityContextMenu") || ImGui::BeginPopupContextItem()) {
                    m_SelectedEntity = entity;
                    if (m_OnSelectCallback) m_OnSelectCallback(entity);

                    if (ImGui::MenuItem("Crear Prefab...")) {
                        SaveSelectedAsPrefab();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Duplicar", "Ctrl+D")) {
                        entityToDuplicate = entity;
                    }
                    if (ImGui::MenuItem("Copiar", "Ctrl+C")) {
                        entityToCopy = entity;
                    }
                    if (ImGui::MenuItem("Cortar", "Ctrl+X")) {
                        entityToCut = entity;
                    }
                    if (ImGui::MenuItem("Pegar como Hijo", "Ctrl+V", false, hasClipboard)) {
                        entityToPasteParent = entity;
                    }
                    ImGui::Separator();

                    if (entity->GetParent() != nullptr) {
                        if (ImGui::MenuItem("Desvincular Padre (Hacer Raíz)")) {
                            entityToUnparent = entity;
                        }
                        ImGui::Separator();
                    }

                    if (ImGui::MenuItem("Eliminar", "Supr")) {
                        entityToDelete = entity;
                    }
                    ImGui::EndPopup();
                }

                if (opened) {
                    auto childrenCopy = entity->GetChildren();
                    for (SlipEntity* child : childrenCopy) {
                        drawNode(child);
                    }
                    ImGui::TreePop();
                }
            };

            // Orden de recorrido plano de la jerarquía (para selección por rango con Shift+Clic)
            m_OrderedEntities.clear();
            std::function<void(SlipEntity*)> collectOrdered = [&](SlipEntity* ent) {
                if (!ent) return;
                m_OrderedEntities.push_back(ent);
                for (SlipEntity* child : ent->GetChildren()) collectOrdered(child);
            };
            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                if (ent && ent->GetParent() == nullptr) collectOrdered(ent);
            }

            // Dibujar únicamente entidades de nivel raíz (sin padre) usando copia local para evitar invalidación
            auto rootEntities = m_ContextLevel->GetEntities();
            for (SlipEntity* entity : rootEntities) {
                if (!entity) continue;
                if (entity->GetParent() == nullptr) {
                    drawNode(entity);
                }
            }

            // Espacio vacío para soltar y desvincular padre o instanciar Prefab
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.y > 20.0f) {
                ImGui::Dummy(avail);
                if (ImGui::BeginDragDropTarget()) {
                    // Emparentar a la raíz
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY_PAYLOAD")) {
                        SlipEntity* dragged = *(SlipEntity**)payload->Data;
                        if (dragged) {
                            dragged->SetParent(nullptr);
                        }
                    }
                    // Instanciar Prefab o Modelo 3D arrastrado desde Content Browser
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string droppedPath = (const char*)payload->Data;
                        auto hasExt = [&](const char* ext) {
                            size_t elen = std::strlen(ext);
                            if (droppedPath.size() < elen) return false;
                            std::string tail = droppedPath.substr(droppedPath.size() - elen);
                            for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            return tail == ext;
                        };
                        if (hasExt(".prefab")) {
                            SlipEntity* inst = SlipPrefab::Instantiate(droppedPath, m_ContextLevel);
                            if (inst) {
                                m_SelectedEntity = inst;
                                if (m_OnSelectCallback) m_OnSelectCallback(inst);
                            }
                        } else if (hasExt(".fbx") || hasExt(".obj") || hasExt(".dae") || hasExt(".gltf") || hasExt(".glb")) {
                            size_t slashPos = droppedPath.find_last_of("/\\");
                            std::string baseName = (slashPos != std::string::npos) ? droppedPath.substr(slashPos + 1) : droppedPath;
                            size_t dotPos = baseName.find_last_of('.');
                            if (dotPos != std::string::npos) baseName = baseName.substr(0, dotPos);
                            if (baseName.empty()) baseName = "Modelo3D";

                            SlipEntity* ent = new SlipEntity(makeUniqueName(baseName));
                            AssetMetadata meta;
                            SlipModelExtract::EnsureMetaForFile(droppedPath, &meta);
                            ModelInspectionInfo info;
                            SlipModelExtract::InspectModelDetailed(droppedPath, info);
                            bool isSkinned = (info.boneCount > 0 || !info.animations.empty());

                            if (isSkinned || !meta.bakeTransforms) {
                                if (meta.importScale > 0.0f && std::abs(meta.importScale - 1.0f) > 1e-4f) {
                                    ent->scale = glm::vec3(meta.importScale);
                                    if (auto tc = ent->GetComponent<TransformComponent>()) {
                                        tc->scale = ent->scale;
                                    }
                                }
                                if (glm::length(meta.importRotation) > 0.01f) {
                                    ent->rotation = meta.importRotation;
                                    if (auto tc = ent->GetComponent<TransformComponent>()) {
                                        tc->rotation = ent->rotation;
                                    }
                                }
                            }

                            if (isSkinned) {
                                auto smc = ent->AddComponent<SkinnedMeshComponent>();
                                m_ContextLevel->AddEntity(ent);
                                smc->SetModel(droppedPath);
                                SlipConsole::LogSuccess("Modelo con huesos instanciado con SkinnedMeshComponent: " + baseName);
                            } else {
                                ent->AddComponent<MeshComponent>(droppedPath);
                                ent->SetStatic(true);
                                m_ContextLevel->AddEntity(ent);
                                SlipConsole::LogSuccess("Modelo estatico V-Clust instanciado con MeshComponent: " + baseName);
                            }
                            m_SelectedEntity = ent;
                            if (m_OnSelectCallback) m_OnSelectCallback(ent);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            if (entityToDuplicate) {
                m_SelectedEntity = entityToDuplicate;
                if (m_OnSelectCallback) m_OnSelectCallback(entityToDuplicate);
                DuplicateSelected();
            }
            if (entityToCopy) {
                m_SelectedEntity = entityToCopy;
                if (m_OnSelectCallback) m_OnSelectCallback(entityToCopy);
                CopySelected();
            }
            if (entityToCut) {
                m_SelectedEntity = entityToCut;
                if (m_OnSelectCallback) m_OnSelectCallback(entityToCut);
                CutSelected();
            }
            if (entityToPasteParent) {
                m_SelectedEntity = entityToPasteParent;
                if (m_OnSelectCallback) m_OnSelectCallback(entityToPasteParent);
                Paste();
            }
            if (entityToUnparent) {
                entityToUnparent->SetParent(nullptr);
            }

            if (entityToDelete) {
                if (m_SelectedEntity) {
                    for (SlipEntity* p = m_SelectedEntity; p != nullptr; p = p->GetParent()) {
                        if (p == entityToDelete) {
                            m_SelectedEntity = nullptr;
                            if (m_OnSelectCallback) m_OnSelectCallback(nullptr);
                            break;
                        }
                    }
                }
                m_ContextLevel->DestroyEntity(entityToDelete);
            }

            // Detección de clic derecho o pulsación prolongada en espacio vacío
            bool isBlankHierarchyLongPress = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
                                             !ImGui::IsAnyItemHovered() &&
                                             ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                                             ImGui::GetIO().MouseDownDuration[0] >= 0.45f &&
                                             !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10.0f);
            if (isBlankHierarchyLongPress && !s_HierarchyLongPressFired) {
                s_HierarchyLongPressFired = true;
                ImGui::OpenPopup("HierarchyBlankContextMenu");
            }

            if (ImGui::BeginPopup("HierarchyBlankContextMenu") ||
                ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Pegar", "Ctrl+V", false, hasClipboard)) {
                    Paste();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Crear Cubo")) {
                    SlipEntity* cube = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Cube);
                    cube->SetName(makeUniqueName("Cubo"));
                    m_ContextLevel->AddEntity(cube);
                    m_SelectedEntity = cube;
                    if (m_OnSelectCallback) m_OnSelectCallback(cube);
                }
                if (ImGui::MenuItem("Crear Esfera")) {
                    SlipEntity* sphere = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Sphere);
                    sphere->SetName(makeUniqueName("Esfera"));
                    m_ContextLevel->AddEntity(sphere);
                    m_SelectedEntity = sphere;
                    if (m_OnSelectCallback) m_OnSelectCallback(sphere);
                }
                if (ImGui::MenuItem("Crear Plano")) {
                    SlipEntity* plane = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Plane);
                    plane->SetName(makeUniqueName("Plano"));
                    m_ContextLevel->AddEntity(plane);
                    m_SelectedEntity = plane;
                    if (m_OnSelectCallback) m_OnSelectCallback(plane);
                }
                if (ImGui::MenuItem("Crear Terreno (V-Clust)")) {
                    SlipEntity* terrainEnt = new SlipEntity(makeUniqueName("Terreno"));
                    terrainEnt->SetStatic(true);
                    auto tc = terrainEnt->AddComponent<SlipTerrainComponent>();
                    tc->RebuildTerrain();
                    m_ContextLevel->AddEntity(terrainEnt);
                    m_SelectedEntity = terrainEnt;
                    if (m_OnSelectCallback) m_OnSelectCallback(terrainEnt);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Luz Puntual")) {
                    SlipEntity* light = new SlipEntity(makeUniqueName("Luz Puntual"));
                    light->AddComponent<LightComponent>(LightType::Point);
                    m_ContextLevel->AddEntity(light);
                    m_SelectedEntity = light;
                    if (m_OnSelectCallback) m_OnSelectCallback(light);
                }
                if (ImGui::MenuItem("Luz Direccional")) {
                    SlipEntity* light = new SlipEntity(makeUniqueName("Luz Direccional"));
                    light->AddComponent<LightComponent>(LightType::Directional);
                    m_ContextLevel->AddEntity(light);
                    m_SelectedEntity = light;
                    if (m_OnSelectCallback) m_OnSelectCallback(light);
                }
                if (ImGui::MenuItem("Luz Spot")) {
                    SlipEntity* light = new SlipEntity(makeUniqueName("Luz Spot"));
                    light->AddComponent<LightComponent>(LightType::Spot);
                    m_ContextLevel->AddEntity(light);
                    m_SelectedEntity = light;
                    if (m_OnSelectCallback) m_OnSelectCallback(light);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Volumen DDGI")) {
                    SlipEntity* volume = new SlipEntity(makeUniqueName("DDGI Volume"));
                    volume->SetStatic(true);
                    volume->AddComponent<DDGIVolumeComponent>();
                    m_ContextLevel->AddEntity(volume);
                    m_SelectedEntity = volume;
                    if (m_OnSelectCallback) m_OnSelectCallback(volume);
                }
                if (ImGui::MenuItem("Volumen de Reflexión (Reflection Probe)")) {
                    SlipEntity* probeEnt = new SlipEntity(makeUniqueName("Reflection Probe"));
                    probeEnt->SetStatic(true);
                    probeEnt->AddComponent<ReflectionProbeComponent>();
                    m_ContextLevel->AddEntity(probeEnt);
                    m_SelectedEntity = probeEnt;
                    if (m_OnSelectCallback) m_OnSelectCallback(probeEnt);
                }
                if (ImGui::MenuItem("Volumen de post-proceso")) {
                    SlipEntity* volume = new SlipEntity(makeUniqueName("Post Process Volume"));
                    volume->AddComponent<PostProcessVolumeComponent>();
                    m_ContextLevel->AddEntity(volume);
                    m_SelectedEntity = volume;
                    if (m_OnSelectCallback) m_OnSelectCallback(volume);
                }
                if (ImGui::MenuItem("Cámara (Camera)")) {
                    SlipEntity* camEnt = new SlipEntity(makeUniqueName("Cámara"));
                    camEnt->position = glm::vec3(0.0f, 2.0f, 5.0f);
                    if (auto tc = camEnt->GetComponent<TransformComponent>()) {
                        tc->position = camEnt->position;
                    }
                    camEnt->AddComponent<CameraComponent>();
                    m_ContextLevel->AddEntity(camEnt);
                    m_SelectedEntity = camEnt;
                    if (m_OnSelectCallback) m_OnSelectCallback(camEnt);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Entidad Vacía")) {
                    SlipEntity* ent = new SlipEntity(makeUniqueName("Nueva Entidad"));
                    m_ContextLevel->AddEntity(ent);
                    m_SelectedEntity = ent;
                    if (m_OnSelectCallback) m_OnSelectCallback(ent);
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Objeto 2D")) {
                    if (ImGui::MenuItem("Sprite 2D")) {
                        SlipEntity* spriteEnt = new SlipEntity(makeUniqueName("Sprite 2D"));
                        spriteEnt->AddComponent<SpriteRendererComponent>();
                        m_ContextLevel->AddEntity(spriteEnt);
                        m_SelectedEntity = spriteEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(spriteEnt);
                    }
                    if (ImGui::MenuItem("Caja 2D Dinamica (Sprite + RigidBody2D)")) {
                        SlipEntity* boxEnt = new SlipEntity(makeUniqueName("Caja 2D"));
                        boxEnt->AddComponent<SpriteRendererComponent>();
                        boxEnt->AddComponent<BoxCollider2DComponent>();
                        boxEnt->AddComponent<RigidBody2DComponent>();
                        m_ContextLevel->AddEntity(boxEnt);
                        m_SelectedEntity = boxEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(boxEnt);
                    }
                    if (ImGui::MenuItem("Sprite Animado (2D Animator)")) {
                        SlipEntity* animEnt = new SlipEntity(makeUniqueName("Sprite Animado"));
                        animEnt->AddComponent<SpriteRendererComponent>();
                        animEnt->AddComponent<SpriteAnimatorComponent>();
                        m_ContextLevel->AddEntity(animEnt);
                        m_SelectedEntity = animEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(animEnt);
                    }
                    if (ImGui::MenuItem("TileGrid 2D (Tilemap)")) {
                        SlipEntity* tileEnt = new SlipEntity(makeUniqueName("TileGrid 2D"));
                        tileEnt->AddComponent<TileGridComponent>();
                        m_ContextLevel->AddEntity(tileEnt);
                        m_SelectedEntity = tileEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(tileEnt);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Luz 2D (Point Light)")) {
                        SlipEntity* lightEnt = new SlipEntity(makeUniqueName("Luz 2D"));
                        auto l2d = lightEnt->AddComponent<Light2DComponent>();
                        l2d->lightType = Light2DType::Point;
                        m_ContextLevel->AddEntity(lightEnt);
                        m_SelectedEntity = lightEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(lightEnt);
                    }
                    if (ImGui::MenuItem("Luz 2D Focal (Spot Light)")) {
                        SlipEntity* spotEnt = new SlipEntity(makeUniqueName("Luz 2D Focal"));
                        auto l2d = spotEnt->AddComponent<Light2DComponent>();
                        l2d->lightType = Light2DType::Spot;
                        m_ContextLevel->AddEntity(spotEnt);
                        m_SelectedEntity = spotEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(spotEnt);
                    }
                    if (ImGui::MenuItem("Sombra 2D (Shadow Caster)")) {
                        SlipEntity* scEnt = new SlipEntity(makeUniqueName("Sombra 2D"));
                        scEnt->AddComponent<ShadowCaster2DComponent>();
                        m_ContextLevel->AddEntity(scEnt);
                        m_SelectedEntity = scEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(scEnt);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Editor de Sprites Pixel Art...")) {
                        if (SlipEditor::GetInstance()) {
                            SlipEditor::GetInstance()->OpenPixelArtEditor();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("UI (Interfaz)")) {
                    auto ensureCanvas = [&]() -> SlipEntity* {
                        for (SlipEntity* e : m_ContextLevel->GetEntities()) {
                            if (e && e->HasComponent<CanvasComponent>()) return e;
                        }
                        SlipEntity* canvasEnt = new SlipEntity(makeUniqueName("Canvas"));
                        canvasEnt->AddComponent<CanvasComponent>();
                        m_ContextLevel->AddEntity(canvasEnt);
                        return canvasEnt;
                    };
                    if (ImGui::MenuItem("Canvas")) {
                        SlipEntity* canvasEnt = new SlipEntity(makeUniqueName("Canvas"));
                        canvasEnt->AddComponent<CanvasComponent>();
                        m_ContextLevel->AddEntity(canvasEnt);
                        m_SelectedEntity = canvasEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(canvasEnt);
                    }
                    if (ImGui::MenuItem("Imagen (UI Image)")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* imgEnt = new SlipEntity(makeUniqueName("Imagen UI"));
                        imgEnt->AddComponent<RectTransformComponent>()->size = glm::vec2(200.0f, 200.0f);
                        imgEnt->AddComponent<UIImageComponent>();
                        m_ContextLevel->AddEntity(imgEnt);
                        imgEnt->SetParent(parent);
                        m_SelectedEntity = imgEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(imgEnt);
                    }
                    if (ImGui::MenuItem("Texto (UI Text)")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* txtEnt = new SlipEntity(makeUniqueName("Texto UI"));
                        txtEnt->AddComponent<RectTransformComponent>()->size = glm::vec2(260.0f, 60.0f);
                        txtEnt->AddComponent<UITextComponent>("Nuevo Texto");
                        m_ContextLevel->AddEntity(txtEnt);
                        txtEnt->SetParent(parent);
                        m_SelectedEntity = txtEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(txtEnt);
                    }
                    if (ImGui::MenuItem("Botón (UI Button)")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* btnEnt = new SlipEntity(makeUniqueName("Botón UI"));
                        btnEnt->AddComponent<RectTransformComponent>()->size = glm::vec2(220.0f, 56.0f);
                        btnEnt->AddComponent<UIButtonComponent>();
                        btnEnt->AddComponent<UITextComponent>("Botón");
                        m_ContextLevel->AddEntity(btnEnt);
                        btnEnt->SetParent(parent);
                        m_SelectedEntity = btnEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(btnEnt);
                    }
                    if (ImGui::MenuItem("Slider (UI Slider)")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* sldEnt = new SlipEntity(makeUniqueName("Slider UI"));
                        sldEnt->AddComponent<RectTransformComponent>()->size = glm::vec2(260.0f, 28.0f);
                        sldEnt->AddComponent<UISliderComponent>();
                        m_ContextLevel->AddEntity(sldEnt);
                        sldEnt->SetParent(parent);
                        m_SelectedEntity = sldEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(sldEnt);
                    }
                    if (ImGui::MenuItem("Barra de Progreso (Progress Bar)")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* pbEnt = new SlipEntity(makeUniqueName("Barra de Progreso UI"));
                        pbEnt->AddComponent<RectTransformComponent>()->size = glm::vec2(280.0f, 34.0f);
                        pbEnt->AddComponent<UIProgressBarComponent>();
                        m_ContextLevel->AddEntity(pbEnt);
                        pbEnt->SetParent(parent);
                        m_SelectedEntity = pbEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(pbEnt);
                    }
                    if (ImGui::MenuItem("Joystick")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* joyEnt = new SlipEntity(makeUniqueName("Joystick"));
                        auto rt = joyEnt->AddComponent<RectTransformComponent>();
                        rt->ApplyAnchorPreset(UIAnchorPreset::BottomLeft);
                        rt->size = glm::vec2(220.0f, 220.0f);
                        rt->anchoredPosition = glm::vec2(80.0f, 80.0f);
                        joyEnt->AddComponent<UIJoystickComponent>();
                        m_ContextLevel->AddEntity(joyEnt);
                        joyEnt->SetParent(parent);
                        m_SelectedEntity = joyEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(joyEnt);
                    }
                    if (ImGui::MenuItem("Botón de acción")) {
                        SlipEntity* parent = ensureCanvas();
                        SlipEntity* actEnt = new SlipEntity(makeUniqueName("Botón de acción"));
                        auto rt = actEnt->AddComponent<RectTransformComponent>();
                        rt->ApplyAnchorPreset(UIAnchorPreset::BottomRight);
                        rt->size = glm::vec2(150.0f, 150.0f);
                        rt->anchoredPosition = glm::vec2(-90.0f, 90.0f);
                        actEnt->AddComponent<UIActionButtonComponent>();
                        m_ContextLevel->AddEntity(actEnt);
                        actEnt->SetParent(parent);
                        m_SelectedEntity = actEnt;
                        if (m_OnSelectCallback) m_OnSelectCallback(actEnt);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextDisabled("No hay ningún nivel cargado.");
        }
    }
    ImGui::End();
}
