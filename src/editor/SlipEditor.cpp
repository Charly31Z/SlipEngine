#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/editor/panels/HierarchyPanel.h"
#include "SlipEngine/editor/panels/InspectorPanel.h"
#include "SlipEngine/editor/panels/ConsolePanel.h"
#include "SlipEngine/editor/panels/ViewportPanel.h"
#include "SlipEngine/editor/panels/ContentBrowserPanel.h"
#include "SlipEngine/editor/panels/ScriptEditorPanel.h"
#include "SlipEngine/editor/panels/PrefabEditorPanel.h"
#include "SlipEngine/editor/panels/ProjectSettingsPanel.h"
#include "SlipEngine/editor/panels/AnimationEditorPanel.h"
#include "SlipEngine/editor/panels/AnimatorGraphPanel.h"
#include "SlipEngine/editor/panels/ShaderGraphPanel.h"
#include "SlipEngine/editor/panels/PixelArtEditorPanel.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/editor/texteditor/SlipScriptIndexer.h"
#include "SlipEngine/scripting/SlipLuaEngine.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/editor/SlipFileDialog.h"
#include "SlipEngine/platform/SlipWindow.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipClusteredLighting.h"
#include "SlipEngine/render/SlipLightBaker.h"
// Incluir backends de ImGui
#include <imgui.h>
#include <imgui_internal.h>
#include "imgui_impl_sdl2.h"
#include <imgui_impl_opengl3.h>
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    #include "SlipEngine/render/vulkan/VulkanRHI.h"
    #include <imgui_impl_vulkan.h>
#endif
#include <SDL2/SDL.h>

SlipEditor::SlipEditor() {
    s_Instance = this;

    // 1. Instanciar los paneles modulares
    auto onEntitySelected = [this](SlipEntity* entity) {
        NotifySelectionChanged(entity ? std::vector<SlipEntity*>{ entity } : std::vector<SlipEntity*>{}, entity);
    };

    auto hierarchy = std::make_unique<HierarchyPanel>(onEntitySelected);

    auto contentBrowser = std::make_unique<ContentBrowserPanel>([this](const std::string& assetPath) {
        for (auto& panel : m_Panels) {
            panel->SetSelectedAsset(assetPath);
            if (!assetPath.empty()) {
                panel->SetSelectedEntity(nullptr);
            }
        }
        m_SelectedEntity = nullptr;
        m_Selection.clear();
    });

    m_Panels.push_back(std::move(hierarchy));
    m_Panels.push_back(std::make_unique<InspectorPanel>());
    m_Panels.push_back(std::make_unique<ConsolePanel>());
    m_Panels.push_back(std::make_unique<ViewportPanel>(onEntitySelected));
    m_Panels.push_back(std::move(contentBrowser));
    m_Panels.push_back(std::make_unique<ScriptEditorPanel>());
    m_Panels.push_back(std::make_unique<PrefabEditorPanel>());
    m_Panels.push_back(std::make_unique<ProjectSettingsPanel>());
    m_Panels.push_back(std::make_unique<AnimationEditorPanel>());
    m_Panels.push_back(std::make_unique<AnimatorGraphPanel>());
    m_Panels.push_back(std::make_unique<ShaderGraphPanel>());
    m_Panels.push_back(std::make_unique<PixelArtEditorPanel>());

    // Callback de multi-selección: Hierarchy y Viewport notifican al editor, que propaga a todos los paneles
    auto onMultiSelected = [this](const std::vector<SlipEntity*>& sel, SlipEntity* primary) {
        NotifySelectionChanged(sel, primary);
    };
    for (auto& panel : m_Panels) {
        if (auto hp = dynamic_cast<HierarchyPanel*>(panel.get())) {
            hp->SetMultiSelectionCallback(onMultiSelected);
        } else if (auto vp = dynamic_cast<ViewportPanel*>(panel.get())) {
            vp->SetMultiSelectionCallback(onMultiSelected);
        }
    }
}

SlipEditor::~SlipEditor() {
    Shutdown();
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
}

ViewportPanel* SlipEditor::GetViewportPanel() const {
    return const_cast<SlipEditor*>(this)->GetPanel<ViewportPanel>();
}

bool SlipEditor::Init(SlipWindow& window) {
    return Init(window.getWindow(), window.getGLContext(), window.getGraphicsAPI());
}

bool SlipEditor::Init(SDL_Window* window, SDL_GLContext glContext, GraphicsAPI api) {
    if (m_Initialized) {
        return true;
    }

    m_Window = window;
    m_GLContext = glContext;
    m_GraphicsAPI = api;

#if !defined(SLIP_PLATFORM_ANDROID)
    if (m_GraphicsAPI == GraphicsAPI::OpenGL) {
        // Cargar punteros de OpenGL (GLAD) en este módulo (.exe)
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            return false;
        }
    }
#endif

    // Inicializar subsistemas del motor para el Editor
    SlipPhysics::Get();
    if (m_GraphicsAPI == GraphicsAPI::OpenGL) {
        SlipShaderLibrary::Reset();
    }

    // Registrar callback de bakeado automático en demanda para mallas en SlipMesh
    SlipMesh::SetBakeCallback([](const std::string& modelPath, std::string& outCache) -> bool {
        AssetMetadata meta;
        SlipModelExtract::EnsureMetaForFile(modelPath, &meta);
        return SlipModelExtract::BakeModelToCache(modelPath, meta, &outCache);
    });

    // Registrar callback de extracción de esqueletos y animaciones para SkinnedMeshComponent
    SkinnedMeshComponent::SetSkeletonExtractCallback([](const std::string& modelPath, SlipSkeleton& outSkeleton, std::vector<std::shared_ptr<AnimationClip>>& outClips) -> bool {
        return SlipModelExtract::ExtractSkeletonAndAnimations(modelPath, outSkeleton, outClips);
    });

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Permite acoplar ventanas estilo Unity/UE

    // Cargar fuente moderna del sistema con soporte para español (á, é, í, ó, ú, ñ) y símbolos
    static const ImWchar glyphRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2000, 0x206F, // General Punctuation
        0x2190, 0x21FF, // Arrows
        0x25A0, 0x25FF, // Geometric Shapes
        0,
    };

    ImFont* customFont = nullptr;
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf"
    };
    for (const char* fPath : fontCandidates) {
        if (SlipFileSystem::Exists(fPath)) {
            customFont = io.Fonts->AddFontFromFileTTF(fPath, 17.0f, nullptr, glyphRanges);
            if (customFont) break;
        }
    }
    if (!customFont) {
        io.Fonts->AddFontDefault();
    }

    ImGui::StyleColorsDark();

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    // Permitir emulación y eventos táctiles en SDL2
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

    // Guardar imgui.ini en el almacenamiento interno de la app (/data/data/com.charlystudios.slipengine/files/imgui.ini)
    std::string internalPath = SlipFileSystem::GetInternalStoragePath();
    SlipFileSystem::CreateDirectoryPath(internalPath);
    static std::string iniPath = internalPath + "/imgui.ini";
    io.IniFilename = iniPath.c_str();

    // Aumentar la escala general de la interfaz al doble para facilitar el toque táctil
    io.FontGlobalScale = 2.0f;

    ImGuiStyle& style = ImGui::GetStyle();
    style.TouchExtraPadding = ImVec2(4.0f, 4.0f); // Área extra sensible al toque
    style.GrabMinSize = 25.0f; // Deslizadores más anchos para dedos
    style.ScrollbarSize = 30.0f; // Barras de scroll más gruesas
#endif

#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (m_GraphicsAPI == GraphicsAPI::Vulkan) {
        VulkanRHI* vkRhi = dynamic_cast<VulkanRHI*>(SlipRHI::Get());
        if (!vkRhi) {
            return false;
        }
        ImGui_ImplSDL2_InitForVulkan(window);
        ImGui_ImplVulkan_LoadFunctions(
            VK_API_VERSION_1_3,
            [](const char* fnName, void* userData) -> PFN_vkVoidFunction {
                return static_cast<VulkanRHI*>(userData)->LoadInstanceFunction(fnName);
            },
            vkRhi
        );

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = vkRhi->GetInstance();
        initInfo.PhysicalDevice = vkRhi->GetPhysicalDevice();
        initInfo.Device = vkRhi->GetDevice();
        initInfo.QueueFamily = vkRhi->GetGraphicsQueueFamily();
        initInfo.Queue = vkRhi->GetGraphicsQueue();
        initInfo.DescriptorPool = vkRhi->GetDescriptorPool();
        initInfo.MinImageCount = vkRhi->GetSwapchainImageCount();
        initInfo.ImageCount = vkRhi->GetSwapchainImageCount();
        initInfo.PipelineInfoMain.RenderPass = vkRhi->GetSwapchainRenderPass();
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            return false;
        }
    } else
#endif
    {
        ImGui_ImplSDL2_InitForOpenGL(window, glContext);
#if defined(SLIP_PLATFORM_ANDROID)
        ImGui_ImplOpenGL3_Init("#version 300 es");
#else
        ImGui_ImplOpenGL3_Init("#version 330");
#endif
    }

    m_Initialized = true;

    std::string defaultWorkPath = SlipFileSystem::GetDefaultWorkingPath();
    std::strncpy(m_NewProjectPathBuf, defaultWorkPath.c_str(), sizeof(m_NewProjectPathBuf) - 1);

    return true;
}

void SlipEditor::ProcessEvent(const SDL_Event& event) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    if (m_Window) {
        int w = 1, h = 1;
        SDL_GetWindowSize(m_Window, &w, &h);
        ImGuiIO& io = ImGui::GetIO();

        if (event.type == SDL_FINGERDOWN) {
            float px = event.tfinger.x * w;
            float py = event.tfinger.y * h;
            m_TouchFingers.push_back({ event.tfinger.fingerId, px, py, px, py });
            m_TouchGesture.fingerCount = (int)m_TouchFingers.size();

            if (m_TouchFingers.size() <= 1) {
                io.MousePos = ImVec2(px, py);
                io.MouseDown[0] = true;
            } else {
                io.MouseDown[0] = false;
            }
        } else if (event.type == SDL_FINGERMOTION) {
            float px = event.tfinger.x * w;
            float py = event.tfinger.y * h;
            for (auto& f : m_TouchFingers) {
                if (f.id == event.tfinger.fingerId) {
                    f.prevX = f.x;
                    f.prevY = f.y;
                    f.x = px;
                    f.y = py;
                    break;
                }
            }
            m_TouchGesture.fingerCount = (int)m_TouchFingers.size();

            if (m_TouchFingers.size() >= 2) {
                float d0x = m_TouchFingers[0].x - m_TouchFingers[0].prevX;
                float d0y = m_TouchFingers[0].y - m_TouchFingers[0].prevY;
                float d1x = m_TouchFingers[1].x - m_TouchFingers[1].prevX;
                float d1y = m_TouchFingers[1].y - m_TouchFingers[1].prevY;

                m_TouchGesture.panDeltaX += (d0x + d1x) * 0.5f;
                m_TouchGesture.panDeltaY += (d0y + d1y) * 0.5f;

                float prevDist = std::hypot(m_TouchFingers[0].prevX - m_TouchFingers[1].prevX, m_TouchFingers[0].prevY - m_TouchFingers[1].prevY);
                float currDist = std::hypot(m_TouchFingers[0].x - m_TouchFingers[1].x, m_TouchFingers[0].y - m_TouchFingers[1].y);
                m_TouchGesture.pinchDelta += (currDist - prevDist);
                io.MouseDown[0] = false;
            } else if (m_TouchFingers.size() == 1) {
                io.MousePos = ImVec2(px, py);
            }
        } else if (event.type == SDL_FINGERUP) {
            float px = event.tfinger.x * w;
            float py = event.tfinger.y * h;
            for (auto it = m_TouchFingers.begin(); it != m_TouchFingers.end(); ++it) {
                if (it->id == event.tfinger.fingerId) {
                    m_TouchFingers.erase(it);
                    break;
                }
            }
            m_TouchGesture.fingerCount = (int)m_TouchFingers.size();
            if (m_TouchFingers.empty()) {
                io.MousePos = ImVec2(px, py);
                io.MouseDown[0] = false;
            }
        }
    }
#endif
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void SlipEditor::BeginFrame() {
    SlipInput::BeginFrame();
    SlipInputMap::Get().BeginTouchFrame();
    SlipModelExtract::UpdateTasks();
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (m_GraphicsAPI == GraphicsAPI::Vulkan) {
        ImGui_ImplVulkan_NewFrame();
    } else
#endif
    {
        ImGui_ImplOpenGL3_NewFrame();
    }
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    {
        ImGuiIO& io = ImGui::GetIO();
        if (IsInPlaySession() && SlipInput::HasInstance() && SlipInput::Get().IsCursorLocked()) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            io.MouseDrawCursor = false;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
        }
    }
    if (!m_ShowProjectHub) {
        ImGuiIO& io = ImGui::GetIO();
        // Atajo de teclado global: Ctrl+S para guardar proyecto y escena
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            SaveCurrentScene();
        }

        // Atajo de teclado global: ESC para detener Play Mode
        if (IsInPlaySession() && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || SlipInput::Get().GetKey(SDLK_ESCAPE))) {
            StopPlayMode();
        }

        // Alt izquierdo libera o vuelve a capturar el cursor pedido por los scripts.
        if (IsInPlaySession() && SlipInput::HasInstance() && ImGui::IsKeyPressed(ImGuiKey_LeftAlt, false)) {
            SlipInput::Get().ToggleCursorRelease();
        }

        // Atajos de entidades desde el Viewport: Ctrl+D duplicar, Ctrl+C copiar, Ctrl+X cortar,
        // Ctrl+V pegar y Supr eliminar (operan sobre la multi-selección completa)
        ViewportPanel* viewportPanel = GetViewportPanel();
        if (viewportPanel && !IsInPlaySession() && (viewportPanel->IsHovered() || viewportPanel->IsFocused()) && !io.WantTextInput) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                DuplicateSelection();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                CopySelection();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
                CutSelection();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                PasteFromClipboard();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                DeleteSelection();
            }
        }

        // Simulación física y lógica durante Play Mode
        if (m_PlayState == EditorPlayState::Play) {
            float dt = io.DeltaTime;
            if (dt <= 0.0f || dt > 0.2f) dt = 1.0f / 60.0f;
            SlipGlobals::Get().SetDeltaTime(dt);

            // 1. Ejecutar lógica y scripts primero (ej. rotación de aspas, controles de movimiento)
            if (m_ActiveLevel) {
                // Iterar usando tamaño por índice para que instanciaciones o spawns de entidades
                // no invaliden iteradores ni causen reasignaciones de memoria sobre punteros inválidos
                size_t entCount = m_ActiveLevel->GetEntities().size();
                for (size_t i = 0; i < entCount; ++i) {
                    if (i < m_ActiveLevel->GetEntities().size()) {
                        SlipEntity* ent = m_ActiveLevel->GetEntities()[i];
                        if (ent) {
                            ent->ejecStart();
                            ent->Update(dt);
                        }
                    }
                }
            }

            // 2. Simular física con transforms actualizados en tiempo real
            SlipPhysics::Get().update(dt);

            // 3. Sincronizar objetos dinámicos desde Bullet
            if (m_ActiveLevel) {
                for (size_t i = 0; i < m_ActiveLevel->GetEntities().size(); ++i) {
                    SlipEntity* ent = m_ActiveLevel->GetEntities()[i];
                    if (ent) {
                        if (auto col = ent->GetComponent<ColliderComponent>()) {
                            col->SyncTransformFromEntity();
                        }
                        if (auto rb2d = ent->GetComponent<RigidBody2DComponent>()) {
                            if (rb2d->bodyType == BodyType2D::Dynamic) {
                                rb2d->SyncTransformFromPhysics();
                            }
                        }
                    }
                }
            }
        }

        RenderDockSpace();
        RenderToolbar();
    }
}

void SlipEditor::RenderPanels() {
    if (m_ShowProjectHub) {
        // En modo Hub a pantalla completa no renderizamos ningún panel ni dockspace
        RenderProjectHub();
        SlipFileDialog::Instance().Render();
        if (m_IsLoading) {
            RenderLoadingOverlay();
            ProcessPendingLoading();
        }
        return;
    }

    if (IsAndroidGameView()) {
        // En Android durante Play Mode: SOLO renderizamos el Viewport (Game View a pantalla completa),
        // ocultando completamente todos los demás paneles del editor (Jerarquía, Inspector, Consola, Scripts, etc.)
        for (auto& panel : m_Panels) {
            if (dynamic_cast<ViewportPanel*>(panel.get())) {
                panel->OnImGuiRender();
                break;
            }
        }
        SlipFileDialog::Instance().Render();
        if (m_IsLoading) {
            RenderLoadingOverlay();
            ProcessPendingLoading();
        }
        return;
    }

    for (auto& panel : m_Panels) {
        panel->OnImGuiRender();
    }

    // Focus diferido de paneles (ej. al abrir un script desde el explorador de archivos):
    // se consume tras renderizar para que la ventana exista y el docking traiga su pestaña al frente
    if (!m_PendingFocusWindow.empty()) {
        ImGui::SetWindowFocus(m_PendingFocusWindow.c_str());
        m_PendingFocusWindow.clear();
    }

    // Dibujar el diálogo modal de selección de archivos si está activo
    SlipFileDialog::Instance().Render();

    if (m_IsLoading) {
        RenderLoadingOverlay();
        ProcessPendingLoading();
    }
}

void SlipEditor::EndFrame() {
    ImGui::Render();
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (m_GraphicsAPI == GraphicsAPI::Vulkan) {
        if (VulkanRHI* vkRhi = dynamic_cast<VulkanRHI*>(SlipRHI::Get())) {
            vkRhi->BeginSwapchainPass();
            VkCommandBuffer cmd = vkRhi->GetCurrentCommandBuffer();
            if (cmd != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
            }
        }
    } else
#endif
    {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    ResetTouchGesture();

    if (m_ActiveLevel && m_ActiveLevel->HasPendingSceneLoad()) {
        m_ActiveLevel->ProcessPendingSceneLoad();
        if (GetViewportPanel()) {
            GetViewportPanel()->RequestReflectionBake();
        }
    }
}

void SlipEditor::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    if (IsInPlaySession()) {
        StopPlayMode();
    }

    SlipModelExtract::Shutdown();

#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (m_GraphicsAPI == GraphicsAPI::Vulkan) {
        if (ISlipRHI* rhi = SlipRHI::Get()) {
            rhi->WaitIdle();
        }
        m_Panels.clear();
        ImGui_ImplVulkan_Shutdown();
    } else
#endif
    {
        m_Panels.clear();
        SlipShaderLibrary::Reset();
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_Initialized = false;
    m_Window = nullptr;
    m_GLContext = nullptr;
}

void SlipEditor::SetContextLevel(SlipLevel* level) {
    m_ActiveLevel = level;
    SlipLevel::SetActiveLevel(level);
    m_SelectedEntity = nullptr;
    m_Selection.clear();
    m_CutEntities.clear();
    for (auto& panel : m_Panels) {
        panel->SetSelectedEntity(nullptr);
        panel->SetContext(level);
    }
}

void SlipEditor::NotifySelectionChanged(const std::vector<SlipEntity*>& selection, SlipEntity* primary) {
    m_Selection = selection;
    m_SelectedEntity = primary;
    for (auto& panel : m_Panels) {
        panel->ApplySelection(selection, primary);
        if (primary) panel->SetSelectedAsset("");
    }
}

// ==========================================================================
// Portapapeles de entidades central (funciona desde Hierarchy y Viewport)
// ==========================================================================

// Devuelve solo las entidades cuyos ancestros no están también en la lista
// (evita destruir un padre y luego acceder a su hijo ya eliminado)
static std::vector<SlipEntity*> FilterRootEntities(const std::vector<SlipEntity*>& list) {
    auto isDescendantOf = [](SlipEntity* e, SlipEntity* ancestor) {
        for (SlipEntity* p = (e ? e->GetParent() : nullptr); p != nullptr; p = p->GetParent()) {
            if (p == ancestor) return true;
        }
        return false;
    };
    std::vector<SlipEntity*> roots;
    for (SlipEntity* ent : list) {
        if (!ent) continue;
        bool covered = false;
        for (SlipEntity* other : list) {
            if (other && other != ent && isDescendantOf(ent, other)) {
                covered = true;
                break;
            }
        }
        if (!covered) roots.push_back(ent);
    }
    return roots;
}

// Renombra con sufijo _NNN a toda la descendencia de un árbol recién clonado/pegado
static void UniquifySubtreeNames(SlipEntity* root, SlipLevel* level) {
    if (!root || !level) return;
    for (SlipEntity* child : root->GetChildren()) {
        if (!child) continue;
        child->SetName(SlipPrefab::MakeUniqueName(child->GetName(), level, child));
        UniquifySubtreeNames(child, level);
    }
}

void SlipEditor::DuplicateSelection() {
    if (!m_ActiveLevel || m_Selection.empty()) return;
    std::vector<SlipEntity*> clones;
    for (SlipEntity* ent : m_Selection) {
        if (!ent) continue;
        SlipEntity* clone = SlipPrefab::CloneEntity(ent, m_ActiveLevel);
        if (clone) {
            if (ent->GetParent()) clone->SetParent(ent->GetParent());
            UniquifySubtreeNames(clone, m_ActiveLevel);
            clones.push_back(clone);
            SlipConsole::LogInfo("Entidad duplicada: " + clone->GetName());
        }
    }
    if (!clones.empty()) {
        NotifySelectionChanged(clones, clones.back());
    }
}

void SlipEditor::CopySelection() {
    if (m_Selection.empty()) return;
    m_ClipboardDataList.clear();
    m_CutEntities.clear();
    for (SlipEntity* ent : m_Selection) {
        if (ent) m_ClipboardDataList.push_back(SlipPrefab::SerializeEntityToString(ent));
    }
    if (!m_ClipboardDataList.empty()) {
        SlipConsole::LogInfo(std::to_string(m_ClipboardDataList.size()) + " entidad(es) copiada(s) al portapapeles");
    }
}

void SlipEditor::CutSelection() {
    if (m_Selection.empty()) return;
    CopySelection();
    m_CutEntities = FilterRootEntities(m_Selection);
    SlipConsole::LogInfo(std::to_string(m_CutEntities.size()) + " entidad(es) cortada(s) al portapapeles");
}

void SlipEditor::PasteFromClipboard(SlipEntity* pasteParent) {
    if (m_ClipboardDataList.empty() || !m_ActiveLevel) return;

    std::vector<SlipEntity*> pasted;
    for (const std::string& data : m_ClipboardDataList) {
        SlipEntity* pastedEnt = SlipPrefab::DeserializeEntityFromString(data, m_ActiveLevel);
        if (!pastedEnt) continue;

        if (m_CutEntities.empty()) {
            // Copia: nombre único con sufijo _NNN y ligero desplazamiento
            pastedEnt->SetName(SlipPrefab::MakeUniqueName(pastedEnt->GetName(), m_ActiveLevel, pastedEnt));
            UniquifySubtreeNames(pastedEnt, m_ActiveLevel);
            if (auto tc = pastedEnt->GetComponent<TransformComponent>()) {
                tc->Translate(0.5f, 0.0f, 0.5f);
            } else {
                pastedEnt->position += glm::vec3(0.5f, 0.0f, 0.5f);
            }
        }

        if (pasteParent && pasteParent != pastedEnt) {
            pastedEnt->SetParent(pasteParent);
        }
        pasted.push_back(pastedEnt);
    }

    // Si era cortada: eliminar los originales después de pegar
    if (!m_CutEntities.empty()) {
        for (SlipEntity* cutEnt : m_CutEntities) {
            if (cutEnt) m_ActiveLevel->DestroyEntity(cutEnt);
        }
        m_CutEntities.clear();
    }

    if (!pasted.empty()) {
        NotifySelectionChanged(pasted, pasted.back());
        SlipConsole::LogSuccess(std::to_string(pasted.size()) + " entidad(es) pegada(s): " + pasted.back()->GetName());
    }
}

void SlipEditor::DeleteSelection() {
    if (m_Selection.empty() || !m_ActiveLevel) return;
    std::vector<SlipEntity*> toDelete = FilterRootEntities(m_Selection);
    NotifySelectionChanged({}, nullptr);
    for (SlipEntity* ent : toDelete) {
        if (ent) m_ActiveLevel->DestroyEntity(ent);
    }
}

void SlipEditor::NotifyProjectOpened() {
    if (!SlipProject::Get().HasActiveProject()) return;
    std::string assets = SlipProject::Get().GetCurrentProject().assetsPath;
    std::string root = SlipProject::Get().GetCurrentProject().rootPath;
    SlipFileSystem::SetProjectPaths(root, assets);
    SlipLuaEngine::Get().SetAssetsDirectory(assets);
    SlipScriptIndexer::Get().RefreshIndex(assets);

    // Cargar o inicializar configuración gráfica del proyecto activo
    std::string configPath = SlipProject::Get().GetCurrentProject().configPath;
    std::string iniPath = SlipFileSystem::CombinePath(configPath, "GraphicsSettings.ini");
    if (SlipFileSystem::Exists(iniPath)) {
        SlipGraphicsSettings::Get().LoadFromFile(iniPath);
    } else {
        SlipGraphicsSettings::Get().SaveToFile(iniPath);
    }

    std::string inputPath = SlipFileSystem::CombinePath(configPath, "InputSettings.ini");
    if (SlipFileSystem::Exists(inputPath)) {
        if (!SlipInputMap::Get().LoadFromFile(inputPath)) {
            SlipInputMap::Get().ResetToDefaults();
        }
    } else {
        SlipInputMap::Get().ResetToDefaults();
        SlipInputMap::Get().SaveToFile(inputPath);
    }

    for (auto& panel : m_Panels) {
        if (auto cb = dynamic_cast<ContentBrowserPanel*>(panel.get())) {
            cb->SetCurrentDirPath(assets);
        }
    }
}

void SlipEditor::CloseCurrentProject() {
    // 1. Detener Play Mode si está activo
    if (IsInPlaySession()) {
        StopPlayMode();
    }

    // 2. Limpiar selección y referencias a entidades / assets en SlipEditor y en TODOS los paneles
    m_SelectedEntity = nullptr;
    for (auto& panel : m_Panels) {
        panel->SetSelectedEntity(nullptr);
        panel->SetSelectedAsset("");
    }

    // 3. Notificar a paneles específicos para que cierren pestañas, previews y prefab context
    for (auto& panel : m_Panels) {
        if (auto animPanel = dynamic_cast<AnimationEditorPanel*>(panel.get())) {
            animPanel->SetTargetEntity(nullptr);
        }
        if (auto prefabPanel = dynamic_cast<PrefabEditorPanel*>(panel.get())) {
            prefabPanel->ClosePrefab();
        }
    }

    // 4. Limpiar entidades del nivel actual y su ruta
    if (m_ActiveLevel) {
        m_ActiveLevel->ClearLevel(false);
        m_ActiveLevel->path.clear();
    }

    // 5. Limpiar completamente el mundo de Bullet Physics
    if (SlipPhysics::IsInitialized()) {
        SlipPhysics::Get().clean();
    }

    // 6. Resetear el estado de la máquina virtual de Lua
    SlipLuaEngine::Get().ResetState();
}

void SlipEditor::RequestOpenProject(const std::string& projectFilePath) {
    if (projectFilePath.empty() || !SlipFileSystem::Exists(projectFilePath)) {
        SlipConsole::LogError("Ruta de proyecto no existe o es inválida: " + projectFilePath);
        return;
    }
    m_PendingOpenProjectPath = projectFilePath;
    m_PendingCreateParentDir.clear();
    m_PendingCreateProjName.clear();
    m_PendingLoadScenePath.clear();
    std::string name = SlipFileSystem::GetFileName(projectFilePath);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    m_LoadingMessage = "Cargando proyecto '" + name + "'...";
    m_IsLoading = true;
    m_LoadingFrameCounter = 0;
}

void SlipEditor::RequestCreateAndOpenProject(const std::string& parentDir, const std::string& projName) {
    if (parentDir.empty() || projName.empty()) {
        SlipConsole::LogError("Nombre o ruta del nuevo proyecto vacía.");
        return;
    }
    m_PendingOpenProjectPath.clear();
    m_PendingCreateParentDir = parentDir;
    m_PendingCreateProjName = projName;
    m_PendingLoadScenePath.clear();
    m_LoadingMessage = "Creando proyecto '" + projName + "'...";
    m_IsLoading = true;
    m_LoadingFrameCounter = 0;
}

void SlipEditor::RequestLoadScene(const std::string& scenePath) {
    if (scenePath.empty() || !SlipFileSystem::Exists(scenePath)) return;
    m_PendingOpenProjectPath.clear();
    m_PendingCreateParentDir.clear();
    m_PendingCreateProjName.clear();
    m_PendingLoadScenePath = scenePath;
    std::string name = SlipFileSystem::GetFileNameWithoutExtension(scenePath);
    m_LoadingMessage = "Cargando escena '" + name + "'...";
    m_IsLoading = true;
    m_LoadingFrameCounter = 0;
}

void SlipEditor::ProcessPendingLoading() {
    m_LoadingFrameCounter++;
    // Permitir al menos 2 frames para garantizar que el buffer fue intercambiado (SwapBuffers)
    // y el usuario visualiza el indicador en pantalla antes de que comience la carga sincrónica.
    if (m_LoadingFrameCounter >= 2) {
        m_IsLoadingExecution = true;

        std::string proj = m_PendingOpenProjectPath;
        std::string createDir = m_PendingCreateParentDir;
        std::string createName = m_PendingCreateProjName;
        std::string scene = m_PendingLoadScenePath;

        m_PendingOpenProjectPath.clear();
        m_PendingCreateParentDir.clear();
        m_PendingCreateProjName.clear();
        m_PendingLoadScenePath.clear();

        if (!proj.empty()) {
            ExecuteOpenProject(proj);
        } else if (!createDir.empty() && !createName.empty()) {
            ExecuteCreateAndOpenProject(createDir, createName);
        } else if (!scene.empty()) {
            ExecuteLoadScene(scene);
        }

        m_IsLoadingExecution = false;
        m_IsLoading = false;
        m_LoadingFrameCounter = 0;
    }
}

void SlipEditor::RenderLoadingOverlay() {
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 1. Fondo oscuro semitransparente que bloquea interacción visual
    drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(10, 12, 16, 215));

    // 2. Tarjeta flotante central
    float cardW = (std::min)(440.0f, displaySize.x - 40.0f);
    float cardH = 190.0f;
    ImVec2 cardMin((displaySize.x - cardW) * 0.5f, (displaySize.y - cardH) * 0.5f);
    ImVec2 cardMax(cardMin.x + cardW, cardMin.y + cardH);

    // Sombra suave exterior y fondo
    drawList->AddRectFilled(ImVec2(cardMin.x - 4, cardMin.y - 4), ImVec2(cardMax.x + 4, cardMax.y + 4), IM_COL32(0, 0, 0, 70), 16.0f);
    drawList->AddRectFilled(cardMin, cardMax, IM_COL32(24, 26, 32, 255), 14.0f);
    drawList->AddRect(cardMin, cardMax, IM_COL32(58, 64, 78, 255), 14.0f, 0, 1.5f);

    // 3. Spinner animado
    ImVec2 spinnerCenter(displaySize.x * 0.5f, cardMin.y + 42.0f);
    float radius = 18.0f;
    float time = static_cast<float>(ImGui::GetTime());
    float startAngle = time * 6.0f;
    float arcLength = 2.2f + 0.8f * std::sin(time * 3.0f);

    // Círculo base tenue
    drawList->AddCircle(spinnerCenter, radius, IM_COL32(45, 50, 62, 255), 32, 3.0f);
    // Arco dinámico giratorio en azul SlipEngine
    drawList->PathClear();
    drawList->PathArcTo(spinnerCenter, radius, startAngle, startAngle + arcLength, 24);
    drawList->PathStroke(IM_COL32(64, 158, 255, 255), 0, 3.5f);

    // 4. Texto de título / estado
    const char* titleText = m_LoadingMessage.empty() ? "Cargando proyecto..." : m_LoadingMessage.c_str();
    ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    ImVec2 titlePos(displaySize.x * 0.5f - titleSize.x * 0.5f, cardMin.y + 82.0f);
    drawList->AddText(titlePos, IM_COL32(245, 248, 255, 255), titleText);

    // 5. Subtexto descriptivo
    const char* subText = "Inicializando escena, texturas y sombreadores...";
    ImVec2 subSize = ImGui::CalcTextSize(subText);
    ImVec2 subPos(displaySize.x * 0.5f - subSize.x * 0.5f, cardMin.y + 108.0f);
    drawList->AddText(subPos, IM_COL32(145, 155, 172, 255), subText);

    // 6. Barra de progreso animada tipo glow
    float barW = cardW - 60.0f;
    float barH = 4.0f;
    ImVec2 barMin(displaySize.x * 0.5f - barW * 0.5f, cardMin.y + 144.0f);
    ImVec2 barMax(barMin.x + barW, barMin.y + barH);

    drawList->AddRectFilled(barMin, barMax, IM_COL32(40, 44, 55, 255), 2.0f);

    // Resplandor viajero sobre la barra
    float pulse = std::fmod(time * 0.85f, 1.0f);
    float glowW = barW * 0.35f;
    float glowX1 = barMin.x + (barW + glowW) * pulse - glowW;
    float glowX2 = glowX1 + glowW;
    float clampedX1 = (std::max)(barMin.x, glowX1);
    float clampedX2 = (std::min)(barMax.x, glowX2);

    if (clampedX2 > clampedX1) {
        drawList->AddRectFilled(ImVec2(clampedX1, barMin.y), ImVec2(clampedX2, barMax.y), IM_COL32(64, 168, 255, 255), 2.0f);
    }
}

bool SlipEditor::OpenProject(const std::string& projectFilePath) {
    if (!m_IsLoadingExecution) {
        RequestOpenProject(projectFilePath);
        return true;
    }
    return ExecuteOpenProject(projectFilePath);
}

bool SlipEditor::ExecuteOpenProject(const std::string& projectFilePath) {
    if (projectFilePath.empty() || !SlipFileSystem::Exists(projectFilePath)) {
        SlipConsole::LogError("Ruta de proyecto no existe o es inválida: " + projectFilePath);
        return false;
    }

    // Cerrar de forma segura el proyecto anterior
    CloseCurrentProject();

    if (!SlipProject::Get().OpenProject(projectFilePath)) {
        SlipConsole::LogError("Fallo al abrir archivo de proyecto: " + projectFilePath);
        return false;
    }

    NotifyProjectOpened();

    if (!m_ActiveLevel) {
        SetContextLevel(new SlipLevel());
    } else {
        SetContextLevel(m_ActiveLevel);
    }

    std::string scenePath = SlipProject::Get().GetCurrentScenePath();
    if (!scenePath.empty() && SlipFileSystem::Exists(scenePath)) {
        ExecuteLoadScene(scenePath);
    } else {
        std::string defaultScene = SlipFileSystem::CombinePath(SlipProject::Get().GetCurrentProject().scenesPath, "Main.scene");
        SlipProject::Get().SetCurrentScenePath(defaultScene);
        m_ActiveLevel->path = defaultScene;
        m_ActiveLevel->SaveScene(defaultScene);
    }

    if (GetViewportPanel()) {
        GetViewportPanel()->RequestReflectionBake();
    }

    m_ShowProjectHub = false;
    SlipConsole::LogSuccess("Proyecto cargado exitosamente: " + SlipProject::Get().GetCurrentProject().name);
    return true;
}

bool SlipEditor::CreateAndOpenProject(const std::string& parentDir, const std::string& projName) {
    if (!m_IsLoadingExecution) {
        RequestCreateAndOpenProject(parentDir, projName);
        return true;
    }
    return ExecuteCreateAndOpenProject(parentDir, projName);
}

bool SlipEditor::ExecuteCreateAndOpenProject(const std::string& parentDir, const std::string& projName) {
    if (parentDir.empty() || projName.empty()) {
        SlipConsole::LogError("Nombre o ruta del nuevo proyecto vacía.");
        return false;
    }

    // Cerrar de forma segura el proyecto anterior
    CloseCurrentProject();

    if (!SlipProject::Get().CreateNewProject(parentDir, projName)) {
        SlipConsole::LogError("Error al inicializar la estructura del nuevo proyecto.");
        return false;
    }

    NotifyProjectOpened();

    if (!m_ActiveLevel) {
        SetContextLevel(new SlipLevel());
    } else {
        SetContextLevel(m_ActiveLevel);
    }

    std::string scenePath = SlipProject::Get().GetCurrentScenePath();
    if (SlipFileSystem::Exists(scenePath)) {
        ExecuteLoadScene(scenePath);
    } else {
        m_ActiveLevel->path = scenePath;
        m_ActiveLevel->SaveScene(scenePath);
    }

    if (GetViewportPanel()) {
        GetViewportPanel()->RequestReflectionBake();
    }

    m_ShowProjectHub = false;
    SlipConsole::LogSuccess("Proyecto creado y abierto exitosamente: " + projName);
    return true;
}

void SlipEditor::OpenProjectSettings() {
    for (auto& panel : m_Panels) {
        if (auto psp = dynamic_cast<ProjectSettingsPanel*>(panel.get())) {
            psp->SetOpen(true);
            break;
        }
    }
}

void SlipEditor::OpenPixelArtEditor() {
    for (auto& panel : m_Panels) {
        if (auto pa = dynamic_cast<PixelArtEditorPanel*>(panel.get())) {
            pa->SetOpen(true);
            break;
        }
    }
}

void SlipEditor::OpenScript(const std::string& scriptPath, int line) {
    for (auto& panel : m_Panels) {
        if (auto sp = dynamic_cast<ScriptEditorPanel*>(panel.get())) {
            sp->OpenScript(scriptPath, line);
            // Enfocar el panel del editor de scripts (trae la pestaña al frente en el dock)
            RequestPanelFocus(ScriptEditorPanel::WINDOW_NAME);
            break;
        }
    }
}

void SlipEditor::OpenPrefab(const std::string& prefabPath) {
    for (auto& panel : m_Panels) {
        if (auto pp = dynamic_cast<PrefabEditorPanel*>(panel.get())) {
            pp->OpenPrefab(prefabPath);
            break;
        }
    }
}

void SlipEditor::OpenAnimatorController(const std::string& controllerPath) {
    for (auto& panel : m_Panels) {
        if (auto agp = dynamic_cast<AnimatorGraphPanel*>(panel.get())) {
            agp->OpenController(controllerPath);
            agp->SetOpen(true);
            break;
        }
    }
}

void SlipEditor::OpenShaderGraph(const std::string& graphPath) {
    for (auto& panel : m_Panels) {
        if (auto sgp = dynamic_cast<ShaderGraphPanel*>(panel.get())) {
            sgp->OpenGraph(graphPath);
            break;
        }
    }
}

void SlipEditor::SaveCurrentScene() {
    SlipProject::Get().SaveCurrentProject();
    if (m_ActiveLevel) {
        std::string scenePath = SlipProject::Get().GetCurrentScenePath();
        if (scenePath.empty()) {
            scenePath = SlipFileSystem::CombinePath(SlipProject::Get().GetCurrentProject().scenesPath, "Main.scene");
            SlipProject::Get().SetCurrentScenePath(scenePath);
        }
        m_ActiveLevel->SaveScene(scenePath);
    }
}

void SlipEditor::LoadScene(const std::string& scenePath) {
    if (!m_IsLoadingExecution) {
        RequestLoadScene(scenePath);
        return;
    }
    ExecuteLoadScene(scenePath);
}

void SlipEditor::ExecuteLoadScene(const std::string& scenePath) {
    if (scenePath.empty() || !SlipFileSystem::Exists(scenePath)) return;
    if (!m_ActiveLevel) {
        SetContextLevel(new SlipLevel());
    } else {
        m_ActiveLevel->ClearLevel();
    }
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (SlipRHI::GetActiveAPI() == GraphicsAPI::Vulkan) {
        if (auto* vkRhi = dynamic_cast<VulkanRHI*>(SlipRHI::Get())) {
            vkRhi->InvalidateSceneCache();
        }
    }
#endif
    m_SelectedEntity = nullptr;
    m_Selection.clear();
    m_CutEntities.clear();
    for (auto& panel : m_Panels) {
        panel->SetSelectedEntity(nullptr);
    }
    m_ActiveLevel->LoadScene(scenePath);
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::Vulkan) {
        SlipLightBaker::Get().LoadBakedLightmap(m_ActiveLevel);
        if (GetViewportPanel()) {
            GetViewportPanel()->RequestReflectionBake();
        }
    }
    SlipProject::Get().SetCurrentScenePath(scenePath);
}

void SlipEditor::StartPlayMode() {
    if (!m_ActiveLevel) {
        SlipConsole::LogWarning("No hay una escena activa para reproducir.");
        return;
    }

    // 1. Determinar ruta del snapshot temporal en el cache/proyecto
    std::string baseDir = SlipProject::Get().GetCurrentProject().scenesPath;
    if (baseDir.empty() || !SlipFileSystem::Exists(baseDir)) {
        baseDir = SlipProject::Get().GetCurrentProject().rootPath;
    }
    if (baseDir.empty() || !SlipFileSystem::Exists(baseDir)) {
        baseDir = SlipFileSystem::GetDefaultWorkingPath();
    }
    m_PlaySnapshotPath = SlipFileSystem::CombinePath(baseDir, "temp_playmode_snapshot.scene");

    // 2. Serializar la escena actual al snapshot temporal para no perder cambios de edición
    if (!m_ActiveLevel->SaveScene(m_PlaySnapshotPath)) {
        SlipConsole::LogError("Error al crear snapshot temporal de la escena para Play Mode: " + m_PlaySnapshotPath);
        return;
    }

    SlipConsole::LogSuccess("Play Mode iniciado. Escena respaldada en: " + m_PlaySnapshotPath);

    // 3. Inicializar componentes físicos de cada entidad
    SlipPhysics::Get(); // Garantiza que el dynamicsWorld existe
    for (SlipEntity* ent : m_ActiveLevel->GetEntities()) {
        if (!ent) continue;
        ent->ResetStart();
        for (auto& comp : ent->GetComponents()) {
            if (comp) {
                comp->OnInit();
            }
        }
        auto sc = ent->GetComponent<ScriptComponent>();
        if (sc) {
            sc->ReloadScript();
        }
        ent->ejecStart();
        auto rb = ent->GetComponent<RigidBodyComponent>();
        if (rb) {
            rb->ApplyToCollider();
            btRigidBody* bulletRb = rb->GetBulletRigidBody();
            if (bulletRb) {
                bulletRb->activate(true);
                bulletRb->clearForces();
            }
        }
        auto veh = ent->GetComponent<VehicleComponent>();
        if (veh) {
            veh->InitVehicle();
        }
        auto rb2d = ent->GetComponent<RigidBody2DComponent>();
        if (rb2d) {
            rb2d->RebuildPhysicsBody();
            if (rb2d->bulletBody) {
                rb2d->bulletBody->activate(true);
                rb2d->bulletBody->clearForces();
            }
        }
    }

    m_PlayState = EditorPlayState::Play;
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    m_AndroidGameView = true;
#endif
    SlipPhysics::Get().SetSimulating(true);

    // Cambiar automáticamente la cámara activa del Viewport a la Cámara del Juego estilo Unity
    for (auto& panel : m_Panels) {
        if (auto vp = dynamic_cast<ViewportPanel*>(panel.get())) {
            vp->SetActiveCameraType(ViewportCameraType::GameCamera);
            break;
        }
    }
}

void SlipEditor::PausePlayMode() {
    if (m_PlayState == EditorPlayState::Play) {
        m_PlayState = EditorPlayState::Pause;
        SlipConsole::LogInfo("Play Mode en pausa.");
    }
}

void SlipEditor::ResumePlayMode() {
    if (m_PlayState == EditorPlayState::Pause) {
        m_PlayState = EditorPlayState::Play;
        SlipConsole::LogInfo("Play Mode reanudado.");
    }
}

void SlipEditor::StopPlayMode() {
    if (m_PlayState == EditorPlayState::Edit) return;

    if (SlipInput::HasInstance()) {
        SlipInput::Get().ResetCursor();
    }

    // 1. Limpiar selección en paneles
    m_SelectedEntity = nullptr;
    for (auto& panel : m_Panels) {
        panel->SetSelectedEntity(nullptr);
    }

    // 2. Limpiar las entidades actuales antes de limpiar Bullet
    if (m_ActiveLevel) {
        m_ActiveLevel->ClearLevel();
    }
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (SlipRHI::GetActiveAPI() == GraphicsAPI::Vulkan) {
        if (auto* vkRhi = dynamic_cast<VulkanRHI*>(SlipRHI::Get())) {
            vkRhi->InvalidateSceneCache();
        }
    }
#endif

    // 3. Limpiar el mundo de físicas de Bullet
    SlipPhysics::Get().clean();

    // 4. Reiniciar estado de scripting Lua ANTES de recargar la escena
    SlipLuaEngine::Get().ResetState();

    // 5. Restaurar la escena original desde el snapshot temporal
    if (!m_PlaySnapshotPath.empty() && SlipFileSystem::Exists(m_PlaySnapshotPath)) {
        std::string originalProjScene = SlipProject::Get().GetCurrentScenePath();
        if (m_ActiveLevel) {
            m_ActiveLevel->LoadScene(m_PlaySnapshotPath);
            m_ActiveLevel->path = originalProjScene;
        }
        SlipProject::Get().SetCurrentScenePath(originalProjScene);
        SlipFileSystem::DeleteFileOrDir(m_PlaySnapshotPath);
        SlipConsole::LogSuccess("Play Mode detenido. Escena restaurada al estado original.");
    } else {
        SlipConsole::LogWarning("No se encontró el snapshot temporal al detener Play Mode.");
    }

    SlipPhysics::Get().SetSimulating(false);
    m_PlayState = EditorPlayState::Edit;
    m_AndroidGameView = false;
    m_NeedsResetLayout = true;

    // Restaurar automáticamente la cámara del Viewport a la Cámara de Editor
    for (auto& panel : m_Panels) {
        if (auto vp = dynamic_cast<ViewportPanel*>(panel.get())) {
            vp->SetActiveCameraType(ViewportCameraType::EditorCamera);
            break;
        }
    }

    ResetDefaultLayout();
}

bool SlipEditor::IsAndroidGameView() const {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    return IsInPlaySession() && m_AndroidGameView;
#else
    return false;
#endif
}

void SlipEditor::SetAndroidGameView(bool gameView) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    if (!IsInPlaySession()) {
        m_AndroidGameView = false;
        m_NeedsResetLayout = true;
        ResetDefaultLayout();
        return;
    }
    bool wasGameView = m_AndroidGameView;
    m_AndroidGameView = gameView;
    if (wasGameView && !gameView) {
        m_NeedsResetLayout = true;
        ResetDefaultLayout();
    }
#else
    (void)gameView;
    m_AndroidGameView = false;
#endif
}

void SlipEditor::ResetDefaultLayout() {
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (!mainViewport) return;

    ImGuiID dockspaceId = mainViewport->ID;
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, mainViewport->WorkSize);
    ImGui::DockBuilderSetNodePos(dockspaceId, mainViewport->WorkPos);

    ImGuiID dockMain = dockspaceId;
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, nullptr, &dockMain);
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Jerarquía de Escena", dockLeft);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Consola", dockBottom);
    ImGui::DockBuilderDockWindow("Explorador de Archivos", dockBottom);
    ImGui::DockBuilderDockWindow(ScriptEditorPanel::WINDOW_NAME, dockBottom);
    ImGui::DockBuilderDockWindow("Viewport", dockMain);
    ImGui::DockBuilderFinish(dockspaceId);
}

void SlipEditor::RenderDockSpace() {
    if (IsAndroidGameView()) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiID dockspaceId = viewport->ID;
    ImGui::DockSpaceOverViewport(dockspaceId, viewport);

    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
    if (m_NeedsResetLayout || !node || node->IsEmpty() || !node->IsSplitNode()) {
        m_NeedsResetLayout = false;
        ResetDefaultLayout();
    }
}

void SlipEditor::RenderToolbar() {
    if (IsAndroidGameView()) return;

    // 1. Barra de menú principal superior única con margen de seguridad para notch y esquinas redondeadas
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
#else
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
#endif

    if (ImGui::BeginMainMenuBar()) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        // Margen seguro amplio para que bordes circulares y notch no tapen "Archivo"
        ImGui::Dummy(ImVec2(80.0f, 0.0f));
        ImGui::SameLine();
#else
        ImGui::Dummy(ImVec2(10.0f, 0.0f));
        ImGui::SameLine();
#endif
        if (ImGui::BeginMenu("Archivo")) {
            if (ImGui::MenuItem("Project Hub...", "Ctrl+H")) {
                m_ShowProjectHub = true;
            }
            if (ImGui::MenuItem("Nuevo Proyecto...", "Ctrl+Shift+N")) {
                m_ShowProjectHub = true;
            }
            if (ImGui::MenuItem("Abrir Proyecto...", "Ctrl+O")) {
                SlipFileDialog::Instance().Open("ToolbarOpenProj", "Seleccionar Proyecto", ".slipproj,*.*", "", SlipFileDialogType::OpenFile);
            }
            if (ImGui::MenuItem("Guardar Proyecto / Escena", "Ctrl+S")) {
                SaveCurrentScene();
            }
            if (ImGui::MenuItem("Guardar Escena Como...", "Ctrl+Shift+S")) {
                SlipFileDialog::Instance().Open("ToolbarSaveSceneAs", "Guardar Escena Como", ".scene", SlipProject::Get().GetCurrentProject().scenesPath, SlipFileDialogType::SaveFile);
            }
            if (ImGui::MenuItem("Abrir Escena...", "")) {
                SlipFileDialog::Instance().Open("ToolbarOpenScene", "Abrir Escena", ".scene", SlipProject::Get().GetCurrentProject().scenesPath, SlipFileDialogType::OpenFile);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Importar Asset Crudo...", "Ctrl+I")) {
                SlipFileDialog::Instance().Open("ToolbarImportAsset", "Importar Asset al Proyecto", ".fbx,.obj,.dae,.gltf,.png,.jpg,.jpeg,.tga,.wav,.ogg", "", SlipFileDialogType::OpenFile);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Salir", "Alt+F4")) {
                SDL_Event quitEvt;
                quitEvt.type = SDL_QUIT;
                SDL_PushEvent(&quitEvt);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Editar")) {
            if (ImGui::MenuItem("Configuración del Proyecto...", "Ctrl+Shift+P")) {
                OpenProjectSettings();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject")) {
            if (ImGui::MenuItem("Crear Cubo")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* cube = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Cube, "Cubo");
                m_ActiveLevel->AddEntity(cube);
                m_SelectedEntity = cube;
                for (auto& p : m_Panels) p->SetSelectedEntity(cube);
            }
            if (ImGui::MenuItem("Crear Esfera")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* sphere = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Sphere, "Esfera");
                m_ActiveLevel->AddEntity(sphere);
                m_SelectedEntity = sphere;
                for (auto& p : m_Panels) p->SetSelectedEntity(sphere);
            }
            if (ImGui::MenuItem("Crear Plano")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* plane = SlipPrimitive::CreatePrimitiveEntity(PrimitiveType::Plane, "Plano");
                m_ActiveLevel->AddEntity(plane);
                m_SelectedEntity = plane;
                for (auto& p : m_Panels) p->SetSelectedEntity(plane);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Crear Luz Direccional")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* light = new SlipEntity("Luz Direccional");
                light->position = glm::vec3(0.0f, 8.0f, 0.0f);
                light->rotation = glm::vec3(50.0f, -30.0f, 0.0f);
                if (auto tc = light->GetComponent<TransformComponent>()) {
                    tc->position = light->position;
                    tc->rotation = light->rotation;
                }
                auto lc = light->AddComponent<LightComponent>(LightType::Directional);
                lc->intensity = 1.2f;
                lc->color = glm::vec3(1.0f, 0.98f, 0.92f);
                m_ActiveLevel->AddEntity(light);
                m_SelectedEntity = light;
                for (auto& p : m_Panels) p->SetSelectedEntity(light);
            }
            if (ImGui::MenuItem("Crear Luz Puntual")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* light = new SlipEntity("Luz Puntual");
                light->position = glm::vec3(0.0f, 3.0f, 0.0f);
                if (auto tc = light->GetComponent<TransformComponent>()) {
                    tc->position = light->position;
                }
                auto lc = light->AddComponent<LightComponent>(LightType::Point);
                lc->intensity = 2.0f;
                lc->range = 10.0f;
                m_ActiveLevel->AddEntity(light);
                m_SelectedEntity = light;
                for (auto& p : m_Panels) p->SetSelectedEntity(light);
            }
            if (ImGui::MenuItem("Crear Luz Foco (Spot)")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* light = new SlipEntity("Luz Foco");
                light->position = glm::vec3(0.0f, 5.0f, 0.0f);
                light->rotation = glm::vec3(90.0f, 0.0f, 0.0f);
                if (auto tc = light->GetComponent<TransformComponent>()) {
                    tc->position = light->position;
                    tc->rotation = light->rotation;
                }
                auto lc = light->AddComponent<LightComponent>(LightType::Spot);
                lc->intensity = 3.0f;
                lc->range = 15.0f;
                lc->spotAngle = 45.0f;
                m_ActiveLevel->AddEntity(light);
                m_SelectedEntity = light;
                for (auto& p : m_Panels) p->SetSelectedEntity(light);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Crear Volumen de post-proceso")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* volume = new SlipEntity("Post Process Volume");
                volume->AddComponent<PostProcessVolumeComponent>();
                m_ActiveLevel->AddEntity(volume);
                m_SelectedEntity = volume;
                for (auto& p : m_Panels) p->SetSelectedEntity(volume);
            }
            if (ImGui::MenuItem("Crear Cámara")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* cam = new SlipEntity("Cámara Principal");
                cam->position = glm::vec3(0.0f, 2.0f, 5.0f);
                if (auto tc = cam->GetComponent<TransformComponent>()) {
                    tc->position = cam->position;
                }
                cam->AddComponent<CameraComponent>();
                m_ActiveLevel->AddEntity(cam);
                m_SelectedEntity = cam;
                for (auto& p : m_Panels) p->SetSelectedEntity(cam);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Crear Entidad Vacia")) {
                if (!m_ActiveLevel) {
                    SetContextLevel(new SlipLevel());
                }
                SlipEntity* ent = new SlipEntity("Nueva Entidad");
                m_ActiveLevel->AddEntity(ent);
                m_SelectedEntity = ent;
                for (auto& p : m_Panels) p->SetSelectedEntity(ent);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Objeto 2D")) {
                if (ImGui::MenuItem("Sprite 2D")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* spriteEnt = new SlipEntity("Sprite 2D");
                    spriteEnt->AddComponent<SpriteRendererComponent>();
                    m_ActiveLevel->AddEntity(spriteEnt);
                    m_SelectedEntity = spriteEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(spriteEnt);
                }
                if (ImGui::MenuItem("Caja 2D Dinamica (Sprite + RigidBody2D)")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* boxEnt = new SlipEntity("Caja 2D");
                    boxEnt->AddComponent<SpriteRendererComponent>();
                    boxEnt->AddComponent<BoxCollider2DComponent>();
                    boxEnt->AddComponent<RigidBody2DComponent>();
                    m_ActiveLevel->AddEntity(boxEnt);
                    m_SelectedEntity = boxEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(boxEnt);
                }
                if (ImGui::MenuItem("Sprite Animado (2D Animator)")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* animEnt = new SlipEntity("Sprite Animado");
                    animEnt->AddComponent<SpriteRendererComponent>();
                    animEnt->AddComponent<SpriteAnimatorComponent>();
                    m_ActiveLevel->AddEntity(animEnt);
                    m_SelectedEntity = animEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(animEnt);
                }
                if (ImGui::MenuItem("TileGrid 2D (Tilemap)")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* tileEnt = new SlipEntity("TileGrid 2D");
                    tileEnt->AddComponent<TileGridComponent>();
                    m_ActiveLevel->AddEntity(tileEnt);
                    m_SelectedEntity = tileEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(tileEnt);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Luz 2D (Point Light)")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* lightEnt = new SlipEntity("Luz 2D");
                    auto l2d = lightEnt->AddComponent<Light2DComponent>();
                    l2d->lightType = Light2DType::Point;
                    m_ActiveLevel->AddEntity(lightEnt);
                    m_SelectedEntity = lightEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(lightEnt);
                }
                if (ImGui::MenuItem("Luz 2D Focal (Spot Light)")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* spotEnt = new SlipEntity("Luz 2D Focal");
                    auto l2d = spotEnt->AddComponent<Light2DComponent>();
                    l2d->lightType = Light2DType::Spot;
                    m_ActiveLevel->AddEntity(spotEnt);
                    m_SelectedEntity = spotEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(spotEnt);
                }
                if (ImGui::MenuItem("Sombra 2D (Shadow Caster)")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* scEnt = new SlipEntity("Sombra 2D");
                    scEnt->AddComponent<ShadowCaster2DComponent>();
                    m_ActiveLevel->AddEntity(scEnt);
                    m_SelectedEntity = scEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(scEnt);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Editor de Sprites Pixel Art...")) {
                    OpenPixelArtEditor();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("UI (Interfaz)")) {
                auto ensureCanvas = [&]() -> SlipEntity* {
                    if (!m_ActiveLevel) {
                        SetContextLevel(new SlipLevel());
                    }
                    if (m_SelectedEntity && (m_SelectedEntity->HasComponent<CanvasComponent>() || m_SelectedEntity->HasComponent<RectTransformComponent>())) {
                        return m_SelectedEntity;
                    }
                    for (SlipEntity* e : m_ActiveLevel->GetEntities()) {
                        if (e && e->HasComponent<CanvasComponent>()) return e;
                    }
                    SlipEntity* canvasEnt = new SlipEntity("Canvas");
                    canvasEnt->AddComponent<CanvasComponent>();
                    m_ActiveLevel->AddEntity(canvasEnt);
                    return canvasEnt;
                };

                if (ImGui::MenuItem("Canvas")) {
                    if (!m_ActiveLevel) SetContextLevel(new SlipLevel());
                    SlipEntity* canvasEnt = new SlipEntity("Canvas");
                    canvasEnt->AddComponent<CanvasComponent>();
                    m_ActiveLevel->AddEntity(canvasEnt);
                    m_SelectedEntity = canvasEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(canvasEnt);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Imagen (UI Image)")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* imgEnt = new SlipEntity("Imagen UI");
                    auto rt = imgEnt->AddComponent<RectTransformComponent>();
                    rt->size = glm::vec2(200.0f, 200.0f);
                    imgEnt->AddComponent<UIImageComponent>();
                    m_ActiveLevel->AddEntity(imgEnt);
                    imgEnt->SetParent(parent);
                    m_SelectedEntity = imgEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(imgEnt);
                }
                if (ImGui::MenuItem("Texto (UI Text)")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* txtEnt = new SlipEntity("Texto UI");
                    auto rt = txtEnt->AddComponent<RectTransformComponent>();
                    rt->size = glm::vec2(260.0f, 60.0f);
                    txtEnt->AddComponent<UITextComponent>("Nuevo Texto");
                    m_ActiveLevel->AddEntity(txtEnt);
                    txtEnt->SetParent(parent);
                    m_SelectedEntity = txtEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(txtEnt);
                }
                if (ImGui::MenuItem("Botón (UI Button)")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* btnEnt = new SlipEntity("Botón UI");
                    auto rt = btnEnt->AddComponent<RectTransformComponent>();
                    rt->size = glm::vec2(220.0f, 56.0f);
                    btnEnt->AddComponent<UIButtonComponent>();
                    auto txt = btnEnt->AddComponent<UITextComponent>("Botón");
                    txt->fontSize = 22.0f;
                    m_ActiveLevel->AddEntity(btnEnt);
                    btnEnt->SetParent(parent);
                    m_SelectedEntity = btnEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(btnEnt);
                }
                if (ImGui::MenuItem("Slider (UI Slider)")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* sldEnt = new SlipEntity("Slider UI");
                    auto rt = sldEnt->AddComponent<RectTransformComponent>();
                    rt->size = glm::vec2(260.0f, 28.0f);
                    sldEnt->AddComponent<UISliderComponent>();
                    m_ActiveLevel->AddEntity(sldEnt);
                    sldEnt->SetParent(parent);
                    m_SelectedEntity = sldEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(sldEnt);
                }
                if (ImGui::MenuItem("Barra de Progreso (Progress Bar)")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* pbEnt = new SlipEntity("Barra de Progreso UI");
                    auto rt = pbEnt->AddComponent<RectTransformComponent>();
                    rt->size = glm::vec2(280.0f, 34.0f);
                    pbEnt->AddComponent<UIProgressBarComponent>();
                    m_ActiveLevel->AddEntity(pbEnt);
                    pbEnt->SetParent(parent);
                    m_SelectedEntity = pbEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(pbEnt);
                }
                if (ImGui::MenuItem("Joystick")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* joyEnt = new SlipEntity("Joystick");
                    auto rt = joyEnt->AddComponent<RectTransformComponent>();
                    rt->ApplyAnchorPreset(UIAnchorPreset::BottomLeft);
                    rt->size = glm::vec2(220.0f, 220.0f);
                    rt->anchoredPosition = glm::vec2(80.0f, 80.0f);
                    joyEnt->AddComponent<UIJoystickComponent>();
                    m_ActiveLevel->AddEntity(joyEnt);
                    joyEnt->SetParent(parent);
                    m_SelectedEntity = joyEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(joyEnt);
                }
                if (ImGui::MenuItem("Botón de acción")) {
                    SlipEntity* parent = ensureCanvas();
                    SlipEntity* actEnt = new SlipEntity("Botón de acción");
                    auto rt = actEnt->AddComponent<RectTransformComponent>();
                    rt->ApplyAnchorPreset(UIAnchorPreset::BottomRight);
                    rt->size = glm::vec2(150.0f, 150.0f);
                    rt->anchoredPosition = glm::vec2(-90.0f, 90.0f);
                    actEnt->AddComponent<UIActionButtonComponent>();
                    m_ActiveLevel->AddEntity(actEnt);
                    actEnt->SetParent(parent);
                    m_SelectedEntity = actEnt;
                    for (auto& p : m_Panels) p->SetSelectedEntity(actEnt);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Proyecto")) {
            if (ImGui::MenuItem("Configuración del Proyecto...", "Ctrl+Shift+P")) {
                OpenProjectSettings();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Regenerar Archivos .meta")) {
                SlipModelExtract::ScanAndGenerateMetas(SlipProject::Get().GetCurrentProject().assetsPath);
            }
            if (ImGui::MenuItem("Guardar Configuracion")) {
                SlipProject::Get().SaveCurrentProject();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Iluminación")) {
            if (ImGui::MenuItem("Hornear Iluminación de Escena (Bake Lightmaps)...")) {
                if (m_ActiveLevel) {
                    LightBakeSettings settings;
                    settings.resolution = 1024;
                    settings.indirectBounces = 1;
                    SlipLightBaker::Get().BakeLevel(m_ActiveLevel, settings, nullptr);
                }
            }
            if (ImGui::MenuItem("Limpiar Lightmaps de Escena")) {
                if (m_ActiveLevel) {
                    SlipLightBaker::Get().ClearBakedLightmap(m_ActiveLevel);
                }
            }
            ImGui::Separator();
            bool clustered = SlipClusteredLighting::Get().IsEnabled();
            if (ImGui::MenuItem("Clustered Forward Shading (1,000+ Luces)", nullptr, &clustered)) {
                SlipClusteredLighting::Get().SetEnabled(clustered);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Ventanas")) {
            for (auto& panel : m_Panels) {
                const char* panelName = "Panel";
                if (dynamic_cast<HierarchyPanel*>(panel.get())) panelName = "Jerarquia";
                else if (dynamic_cast<InspectorPanel*>(panel.get())) panelName = "Inspector";
                else if (dynamic_cast<ConsolePanel*>(panel.get())) panelName = "Consola";
                else if (dynamic_cast<ViewportPanel*>(panel.get())) panelName = "Viewport";
                else if (dynamic_cast<ContentBrowserPanel*>(panel.get())) panelName = "Explorador de Archivos";
                else if (dynamic_cast<ScriptEditorPanel*>(panel.get())) panelName = "Editor de Scripts";
                else if (dynamic_cast<PrefabEditorPanel*>(panel.get())) panelName = "Editor de Prefabs";
                else if (dynamic_cast<ProjectSettingsPanel*>(panel.get())) panelName = "Configuracion del Proyecto";
                else if (dynamic_cast<AnimationEditorPanel*>(panel.get())) panelName = "Editor de Animacion";
                else if (dynamic_cast<AnimatorGraphPanel*>(panel.get())) panelName = "Grafo de Animator (Controller)";
                else if (dynamic_cast<ShaderGraphPanel*>(panel.get())) panelName = "Editor de Shader Graph (Nodos)";
                else if (dynamic_cast<PixelArtEditorPanel*>(panel.get())) panelName = "Editor de Sprites Pixel Art (2D)";

                bool isOpen = panel->IsOpen();
                if (ImGui::MenuItem(panelName, nullptr, &isOpen)) {
                    panel->SetOpen(isOpen);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Restablecer Disposicion (Layout)")) {
                ResetDefaultLayout();
            }
            ImGui::Separator();
            ImGui::MenuItem("Project Hub", nullptr, &m_ShowProjectHub);
            ImGui::EndMenu();
        }

        // Controles de Play Mode centrales estilo Unity/Unreal
        bool isPlaying = IsPlaying();
        bool isPaused = IsPaused();
        bool inSession = IsInPlaySession();

        float centerToolbarWidth = 240.0f;
        float centerPos = (ImGui::GetWindowWidth() - centerToolbarWidth) * 0.5f;
        if (centerPos > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(centerPos);
        }

        // --- PLAY BUTTON ---
        if (isPlaying) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.75f, 0.35f, 1.0f));
        }
        if (ImGui::Button(isPlaying ? " [>] Jugando " : " [>] Play ")) {
            if (isPaused) {
                ResumePlayMode();
            } else if (!isPlaying) {
                StartPlayMode();
            }
        }
        if (isPlaying) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isPlaying ? "Modo Play activo (simulando fisicas)" : "Iniciar Play Mode");
        }

        ImGui::SameLine();

        // --- PAUSE BUTTON ---
        if (isPaused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.65f, 0.18f, 1.0f));
        }
        if (ImGui::Button(isPaused ? " [||] Pausado " : " [||] Pause ")) {
            if (isPlaying) {
                PausePlayMode();
            } else if (isPaused) {
                ResumePlayMode();
            }
        }
        if (isPaused) {
            ImGui::PopStyleColor(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(isPaused ? "Reanudar simulacion fisica" : "Pausar simulacion fisica");
        }

        ImGui::SameLine();

        // --- STOP BUTTON ---
        if (ImGui::Button(" [X] Stop ")) {
            if (inSession) {
                StopPlayMode();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Detener Play Mode y restaurar la escena original (ESC)");
        }

        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        if (ImGui::Button(" [Anim] Editor ")) {
            for (auto& p : m_Panels) {
                if (auto aep = dynamic_cast<AnimationEditorPanel*>(p.get())) {
                    aep->SetOpen(!aep->IsOpen());
                    break;
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Abrir / Cerrar el Editor de Animaciones 3D");
        }

        ImGui::SameLine();
        if (ImGui::Button(" [Grafo] Animator ")) {
            for (auto& p : m_Panels) {
                if (auto agp = dynamic_cast<AnimatorGraphPanel*>(p.get())) {
                    agp->SetOpen(!agp->IsOpen());
                    break;
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Abrir / Cerrar el Editor de Nodos del Animator Controller");
        }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        if (inSession) {
            ImGui::SameLine();
            if (ImGui::Button(" Juego ")) {
                SetAndroidGameView(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Volver al juego a pantalla completa");
            }
        }
#endif

        // Insignia del Proyecto en el extremo derecho de la barra
        const auto& curProj = SlipProject::Get().GetCurrentProject();
        std::string badgeText = "Proyecto: " + (curProj.isLoaded ? curProj.name : std::string("Sin Proyecto"));
        float textWidth = ImGui::CalcTextSize(badgeText.c_str()).x;

        // Si hay una tarea en segundo plano (importando o bakeando modelo), mostrar indicador con barra de progreso
        if (SlipModelExtract::IsProcessing()) {
            float prog = SlipModelExtract::GetProcessProgress();
            std::string st = SlipModelExtract::GetProcessStatus();
            float progWidth = 260.0f;
            float targetPos = ImGui::GetWindowWidth() - textWidth - progWidth - 50.0f;
            if (targetPos > ImGui::GetCursorPosX()) {
                ImGui::SetCursorPosX(targetPos);
            }
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));
            char progOverlay[64];
            snprintf(progOverlay, sizeof(progOverlay), "Bakeando %.0f%%", prog * 100.0f);
            ImGui::ProgressBar(prog, ImVec2(130.0f, 18.0f), progOverlay);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", st.c_str());
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "[Procesando]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", st.c_str());
            }
            ImGui::SameLine();
        }

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - textWidth - 25.0f);
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s", badgeText.c_str());

        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleVar();

    // 2. Procesar resultados de diálogos iniciados por la toolbar
    if (SlipFileDialog::Instance().GetCurrentDialogKey() == "ToolbarOpenProj" && SlipFileDialog::Instance().HasSelected()) {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        OpenProject(selected);
    }
    if (SlipFileDialog::Instance().GetCurrentDialogKey() == "ToolbarSaveSceneAs" && SlipFileDialog::Instance().HasSelected()) {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        if (!selected.empty()) {
            if (selected.find(".scene") == std::string::npos) selected += ".scene";
            SlipProject::Get().SetCurrentScenePath(selected);
            if (m_ActiveLevel) {
                m_ActiveLevel->SaveScene(selected);
            }
        }
    }
    if (SlipFileDialog::Instance().GetCurrentDialogKey() == "ToolbarOpenScene" && SlipFileDialog::Instance().HasSelected()) {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        LoadScene(selected);
    }
    if (SlipFileDialog::Instance().GetCurrentDialogKey() == "ToolbarImportAsset" && SlipFileDialog::Instance().HasSelected()) {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        bool handledByBrowser = false;
        for (auto& panel : m_Panels) {
            if (auto cb = dynamic_cast<ContentBrowserPanel*>(panel.get())) {
                cb->SetOpen(true);
                cb->RequestAssetImport(selected);
                handledByBrowser = true;
                break;
            }
        }
        if (!handledByBrowser) {
            SlipModelExtract::ImportRawAssetAsync(selected);
        }
    }
}

void SlipEditor::RenderProjectHub() {
    if (!m_ShowProjectHub) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hubFlags = ImGuiWindowFlags_NoDecoration |
                               ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoSavedSettings |
                               ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));

    if (ImGui::Begin("SlipEngineHubFullscreen", nullptr, hubFlags)) {
        // Cabecera superior con logotipo y botón de salida
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "SlipEngine Project Hub");
        ImGui::SameLine();
        ImGui::TextDisabled("v0.1.0");

        float exitBtnWidth = 110.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - exitBtnWidth - 30.0f);
        if (ImGui::Button("Salir del Motor", ImVec2(exitBtnWidth, 28.0f))) {
            SDL_Event quitEvt;
            quitEvt.type = SDL_QUIT;
            SDL_PushEvent(&quitEvt);
        }

        ImGui::TextDisabled("Selecciona un proyecto existente o crea uno nuevo para empezar a trabajar.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("HubTabBar", ImGuiTabBarFlags_None)) {
            // Pestaña 1: Proyectos Recientes
            if (ImGui::BeginTabItem("  Proyectos Recientes  ")) {
                ImGui::Spacing();
                auto recentProjects = SlipProject::Get().GetRecentProjects();

                if (recentProjects.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("No hay proyectos recientes registrados.");
                    ImGui::TextDisabled("Crea un proyecto en la pestana 'Nuevo Proyecto' o abre uno desde tu disco.");
                    ImGui::Spacing();
                } else {
                    float tableHeight = ImGui::GetContentRegionAvail().y - 65.0f;
                    if (ImGui::BeginTable("RecentProjectsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, tableHeight))) {
                        ImGui::TableSetupColumn("Nombre del Proyecto", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                        ImGui::TableSetupColumn("Ubicacion en Disco", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Accion", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                        ImGui::TableHeadersRow();

                        for (int i = 0; i < (int)recentProjects.size(); ++i) {
                            const auto& projPath = recentProjects[i];
                            std::string pName = SlipFileSystem::GetFileName(projPath);
                            size_t dot = pName.find_last_of('.');
                            if (dot != std::string::npos) pName = pName.substr(0, dot);

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", pName.c_str());

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextDisabled("%s", projPath.c_str());

                            ImGui::TableSetColumnIndex(2);
                            ImGui::PushID(i);
                            if (ImGui::Button("Abrir Proyecto", ImVec2(100.0f, 0))) {
                                OpenProject(projPath);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("X")) {
                                SlipProject::Get().RemoveRecentProject(projPath);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                }

                ImGui::Spacing();
                if (ImGui::Button("Examinar Proyecto en Disco...", ImVec2(240.0f, 32.0f))) {
                    SlipFileDialog::Instance().Open("HubBrowseProj", "Abrir Proyecto Existente", ".slipproj,*.*", "", SlipFileDialogType::OpenFile);
                }

                ImGui::EndTabItem();
            }

            // Pestaña 2: Crear Nuevo Proyecto
            if (ImGui::BeginTabItem("  Nuevo Proyecto  ")) {
                ImGui::Spacing();
                ImGui::Text("Nombre del Proyecto:");
                ImGui::SetNextItemWidth(380.0f);
                ImGui::InputText("##NewProjName", m_NewProjectNameBuf, sizeof(m_NewProjectNameBuf));

                ImGui::Spacing();
                ImGui::Text("Ubicacion en Disco:");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.0f);
                ImGui::InputText("##NewProjPath", m_NewProjectPathBuf, sizeof(m_NewProjectPathBuf));
                ImGui::SameLine();
                if (ImGui::Button("Examinar...", ImVec2(120.0f, 0))) {
                    SlipFileDialog::Instance().Open("HubChooseParentFolder", "Seleccionar Carpeta para el Proyecto", "*", m_NewProjectPathBuf, SlipFileDialogType::SelectFolder);
                }

                ImGui::Spacing();
                std::string fullPreview = SlipFileSystem::CombinePath(m_NewProjectPathBuf, m_NewProjectNameBuf);
                ImGui::TextDisabled("El proyecto se creara en: %s", fullPreview.c_str());

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Crear y Abrir Proyecto", ImVec2(220.0f, 36.0f))) {
                    CreateAndOpenProject(m_NewProjectPathBuf, m_NewProjectNameBuf);
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // Procesar selecciones de diálogos del Hub
    if (SlipFileDialog::Instance().GetCurrentDialogKey() == "HubBrowseProj" && SlipFileDialog::Instance().HasSelected()) {
        std::string selected = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        OpenProject(selected);
    }
    if (SlipFileDialog::Instance().GetCurrentDialogKey() == "HubChooseParentFolder" && SlipFileDialog::Instance().HasSelected()) {
        std::string selectedDir = SlipFileDialog::Instance().GetSelectedPath();
        SlipFileDialog::Instance().ClearSelection();
        std::strncpy(m_NewProjectPathBuf, selectedDir.c_str(), sizeof(m_NewProjectPathBuf) - 1);
    }
}
