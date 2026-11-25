#include "SlipEngine/editor/panels/ProjectSettingsPanel.h"
#include "SlipEngine/editor/panels/ViewportPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/platform/SlipWindow.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    #include "SlipEngine/render/vulkan/VulkanRHI.h"
#endif
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include "SlipGlobals.h"
#include <imgui.h>
#include <SDL2/SDL.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdio>

namespace {

void CreateTouchActionButton(const std::string& actionName) {
    SlipEditor* editor = SlipEditor::GetInstance();
    if (!editor) return;
    SlipLevel* level = editor->GetContextLevel();
    if (!level) {
        editor->SetContextLevel(new SlipLevel());
        level = editor->GetContextLevel();
    }
    if (!level) return;

    SlipEntity* canvas = nullptr;
    for (SlipEntity* entity : level->GetEntities()) {
        if (entity && entity->HasComponent<CanvasComponent>()) {
            canvas = entity;
            break;
        }
    }
    if (!canvas) {
        canvas = new SlipEntity("Canvas");
        canvas->AddComponent<CanvasComponent>();
        level->AddEntity(canvas);
    }

    std::string label = actionName.empty() ? "Accion" : actionName;
    SlipEntity* button = new SlipEntity(label);
    auto rect = button->AddComponent<RectTransformComponent>();
    rect->ApplyAnchorPreset(UIAnchorPreset::BottomRight);
    rect->size = glm::vec2(150.0f, 150.0f);
    rect->anchoredPosition = glm::vec2(-90.0f, 90.0f);
    auto actionButton = button->AddComponent<UIActionButtonComponent>();
    actionButton->actionName = actionName;
    level->AddEntity(button);
    button->SetParent(canvas);
}

} // namespace

ProjectSettingsPanel::ProjectSettingsPanel() {
    m_IsOpen = false; // Oculto por defecto hasta que el usuario lo abra desde el menú
}

void ProjectSettingsPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Configuración del Proyecto", &m_IsOpen)) {
        // Layout de 2 columnas estilo Unity / Unreal: Barra lateral izquierda + Panel de contenido derecho
        float sidebarWidth = 160.0f;

        ImGui::BeginChild("##SettingsSidebar", ImVec2(sidebarWidth, 0.0f), true);
        {
            ImGui::TextDisabled("CATEGORÍAS");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Selectable("  General / Escenas", m_SelectedCategory == 0)) {
                m_SelectedCategory = 0;
            }
            if (ImGui::Selectable("  Gráficos", m_SelectedCategory == 1)) {
                m_SelectedCategory = 1;
            }
            if (ImGui::Selectable("  Física", m_SelectedCategory == 2)) {
                m_SelectedCategory = 2;
            }
            if (ImGui::Selectable("  Entrada (Input)", m_SelectedCategory == 3)) {
                m_SelectedCategory = 3;
            }
            if (m_SelectedCategory != 3) {
                ClearInputListen();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        float footerHeight = 44.0f;
        ImGui::BeginChild("##SettingsContent", ImVec2(0.0f, -footerHeight), true);
        {
            switch (m_SelectedCategory) {
                case 0:
                    RenderGeneralCategory();
                    break;
                case 1:
                    RenderGraphicsCategory();
                    break;
                case 2:
                    RenderPhysicsCategory();
                    break;
                case 3:
                    RenderInputCategory();
                    break;
                default:
                    break;
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        if (ImGui::Button(" [V] Guardar Todos los Ajustes del Proyecto ", ImVec2(340, 30))) {
            SaveAllSettings();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Guarda .slipproj, GraphicsSettings.ini, InputSettings.ini y Fisica.");
    }
    ImGui::End();
}

void ProjectSettingsPanel::SaveAllSettings() {
    auto& project = SlipProject::Get();
    bool projOk = project.SaveCurrentProject();

    std::string configDir = project.GetCurrentProject().configPath;
    if (configDir.empty()) configDir = "config";
    std::string iniPath = SlipFileSystem::CombinePath(configDir, "GraphicsSettings.ini");
    bool gfxOk = SlipGraphicsSettings::Get().SaveToFile(iniPath);

    std::string inputIniPath = SlipFileSystem::CombinePath(configDir, "InputSettings.ini");
    bool inputOk = SlipInputMap::Get().SaveToFile(inputIniPath);

    if (projOk && gfxOk && inputOk) {
        SlipConsole::LogSuccess("Todos los ajustes del proyecto (.slipproj, GraphicsSettings.ini, InputSettings.ini) se guardaron exitosamente.");
    } else {
        SlipConsole::LogWarning("Algunos ajustes del proyecto no pudieron ser guardados en disco.");
    }
}

void ProjectSettingsPanel::RenderGeneralCategory() {
    auto& project = SlipProject::Get();
    auto& projInfo = project.GetCurrentProject();

    ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Ajustes Generales del Proyecto");
    ImGui::TextDisabled("Configura el nombre del proyecto y las escenas de inicio para el editor y juego standalone.");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Nombre del Proyecto: %s", projInfo.name.c_str());
    ImGui::TextDisabled("Ruta: %s", projInfo.rootPath.c_str());
    ImGui::Spacing();

    // Buscar escenas disponibles en la carpeta scenes/
    std::vector<std::string> sceneFiles;
    if (SlipFileSystem::Exists(projInfo.scenesPath)) {
        auto files = SlipFileSystem::ListDirectory(projInfo.scenesPath, ".scene");
        for (const auto& f : files) {
            std::string rel = "scenes/" + f.name;
            sceneFiles.push_back(rel);
        }
    }
    if (sceneFiles.empty()) {
        sceneFiles.push_back("scenes/Main.scene");
    }

    auto renderSceneCombo = [&](const char* label, std::string& currentVal) {
        int selectedIdx = -1;
        for (int i = 0; i < (int)sceneFiles.size(); ++i) {
            if (sceneFiles[i] == currentVal || SlipFileSystem::GetFileName(sceneFiles[i]) == SlipFileSystem::GetFileName(currentVal)) {
                selectedIdx = i;
                break;
            }
        }
        std::string preview = (selectedIdx >= 0) ? sceneFiles[selectedIdx] : (currentVal.empty() ? "(Sin asignar)" : currentVal);
        if (ImGui::BeginCombo(label, preview.c_str())) {
            for (int i = 0; i < (int)sceneFiles.size(); ++i) {
                bool isSelected = (i == selectedIdx);
                if (ImGui::Selectable(sceneFiles[i].c_str(), isSelected)) {
                    currentVal = sceneFiles[i];
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    };

    if (ImGui::CollapsingHeader("Escenas de Inicio", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        renderSceneCombo("Escena Inicial del Juego (Standalone)", projInfo.gameStartScene);
        ImGui::TextDisabled("Esta escena sera la primera en cargarse al ejecutar el juego compilado / standalone.");

        ImGui::Spacing();
        renderSceneCombo("Escena Inicial del Editor", projInfo.editorStartScene);
        ImGui::Checkbox("Reabrir automaticamente la ultima escena editada", &projInfo.reopenLastEditedScene);
        ImGui::TextDisabled("Si esta activo, el editor cargara la escena que tenias abierta antes de cerrarlo.");
        ImGui::Spacing();
    }

    ImGui::Spacing();
    if (ImGui::Button("Guardar Ajustes de Proyecto", ImVec2(220, 32))) {
        if (project.SaveCurrentProject()) {
            SlipConsole::LogSuccess("Configuracion de proyecto guardada correctamente.");
        } else {
            SlipConsole::LogError("Error al guardar la configuracion del proyecto.");
        }
    }
}

void ProjectSettingsPanel::RenderGraphicsCategory() {
    auto& settingsMgr = SlipGraphicsSettings::Get();
    auto& data = settingsMgr.GetData();

    ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Ajustes de Gráficos y Renderizado");
    ImGui::TextDisabled("Configura la calidad visual y parámetros de sombras persistentes por proyecto.");
    ImGui::Separator();
    ImGui::Spacing();

#if !defined(SLIP_PLATFORM_ANDROID)
    if (ImGui::CollapsingHeader("API Gráfica y Backend (RHI)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        const char* apiOptions[] = { "OpenGL 4.3 (Legado)", "Vulkan 1.3+ (Ray Tracing / Path Tracing)" };
        int apiIdx = (data.graphicsAPI == SlipGraphicsAPI::Vulkan) ? 1 : 0;
        if (ImGui::Combo("API de Renderizado (Requiere reinicio)", &apiIdx, apiOptions, 2)) {
            data.graphicsAPI = (apiIdx == 1) ? SlipGraphicsAPI::Vulkan : SlipGraphicsAPI::OpenGL;
        }
        ISlipRHI* rhi = SlipRHI::Get();
        GraphicsAPI activeApi = SlipRHI::GetActiveAPI();
        ImGui::Text("Backend Activo: %s", SlipRHI::GetAPIName(activeApi));
        if (rhi) {
            const auto& caps = rhi->GetCapabilities();
            ImGui::Text("GPU: %s (%u.%u)", caps.deviceName.c_str(), caps.apiVersionMajor, caps.apiVersionMinor);
            ImGui::Text("Hardware Ray Tracing (VK_KHR_ray_query): %s",
                        caps.supportsRayQuery ? "Disponible" : "No disponible en backend actual");
        }
        if (data.graphicsAPI != activeApi) {
            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.25f, 1.0f),
                               "Guarda la configuracion y reinicia el editor (o usa --vulkan / --opengl) para aplicar el cambio de API.");
        }
    }
    ImGui::Spacing();
#endif

    if (ImGui::CollapsingHeader("Sombras Dinámicas (Cascaded Shadow Maps)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::Checkbox("Activar Sombras Dinámicas", &data.shadowsEnabled);
        ImGui::Spacing();

        if (data.shadowsEnabled) {
            // Resolución del mapa de profundidad
            const char* resOptions[] = { "Baja (1024x1024)", "Media (2048x2048)", "Alta (4096x4096)" };
            int currentResIdx = (data.shadowResolution <= 1024) ? 0 : ((data.shadowResolution >= 4096) ? 2 : 1);
            if (ImGui::Combo("Resolución de Sombras", &currentResIdx, resOptions, 3)) {
                if (currentResIdx == 0) data.shadowResolution = 1024;
                else if (currentResIdx == 1) data.shadowResolution = 2048;
                else if (currentResIdx == 2) data.shadowResolution = 4096;
                settingsMgr.RequestRebuild();
            }

            // Cantidad de cascadas
            if (ImGui::SliderInt("Número de Cascadas (CSM)", &data.cascadeCount, 1, 4)) {
                settingsMgr.RequestRebuild();
            }

            // Distancia máxima
            ImGui::SliderFloat("Distancia Máxima de Sombra", &data.shadowDistance, 10.0f, 300.0f, "%.1f m");

            // Sesgo (bias)
            ImGui::SliderFloat("Sesgo de Sombra (Bias)", &data.shadowBias, 0.0001f, 0.0150f, "%.4f");

            // Filtrado PCF
            const char* pcfOptions[] = { "Desactivado (1x1 Hard)", "Normal (PCF 3x3)", "Ultra Suave (PCF 5x5)" };
            ImGui::Combo("Filtrado de Sombras (PCF)", &data.pcfQuality, pcfOptions, 3);

            // Factor Lambda de partición de cascadas
            ImGui::SliderFloat("Distribución de Cascadas (Lambda)", &data.cascadeLambda, 0.0f, 1.0f, "%.2f (Uniforme a Log)");
        }
    }

#if !defined(SLIP_PLATFORM_ANDROID)
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Tasa de Cuadros y Sincronización (PC)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        if (ImGui::Checkbox("Activar V-Sync (Límite a tasa de refresco del monitor)", &data.vSyncEnabled)) {
            SlipWindow::Get().SetVSync(data.vSyncEnabled);
        }
        if (!data.vSyncEnabled) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "[FPS Ilimitados]");
        }
        ImGui::TextDisabled("Desactiva V-Sync para remover el límite de 60 FPS y permitir framerate máximo en PC.");
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Anti-Aliasing (FXAA)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::Checkbox("Activar Filtro Anti-Aliasing (FXAA)", &data.fxAAEnabled);
        if (data.fxAAEnabled) {
            const char* fxaaOptions[] = { "Bajo (Rápido)", "Medio (Equilibrado)", "Alto (Ultra Nítido)" };
            ImGui::Combo("Calidad FXAA", &data.fxAAQuality, fxaaOptions, 3);
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Iluminación Global (Ray Tracing / Path Tracing)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ISlipRHI* rhi = SlipRHI::Get();
        const bool rqReady = (rhi && rhi->GetAPI() == GraphicsAPI::Vulkan && rhi->GetCapabilities().supportsRayQuery);
        ImGui::TextColored(
            rqReady ? ImVec4(0.35f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
            rqReady ? "Hardware Ray Tracing (VK_KHR_ray_query): Activo"
                    : "Requiere backend Vulkan 1.3+ con VK_KHR_ray_query (en OpenGL usa fallback)"
        );

        const char* giModes[] = {
            "Desactivado (Off)",
            "RTGI (Tiempo Real + Denoiser)",
            "Path Tracing"
        };
        int giModeIdx = static_cast<int>(data.giMode);
        if (ImGui::Combo("Modo de Iluminación Global", &giModeIdx, giModes, 3)) {
            data.giMode = static_cast<GlobalIlluminationMode>(giModeIdx);
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
            if (auto* vkRhi = dynamic_cast<VulkanRHI*>(rhi)) {
                vkRhi->ResetPathTracingAccumulation();
            }
#endif
        }

        if (data.giMode == GlobalIlluminationMode::PathTracing) {
            const char* ptModes[] = {
                "Path Tracing Progresivo",
                "Path Tracing Tiempo Real (SVGF)"
            };
            int ptModeIdx = static_cast<int>(data.ptMode);
            if (ImGui::Combo("Modo de Path Tracing", &ptModeIdx, ptModes, 2)) {
                data.ptMode = static_cast<PathTracingMode>(ptModeIdx);
                if (data.ptMode == PathTracingMode::RealtimeSVGF && data.giRaysPerPixel > 2) {
                    data.giRaysPerPixel = 2;
                }
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                if (auto* vkRhi = dynamic_cast<VulkanRHI*>(rhi)) {
                    vkRhi->ResetPathTracingAccumulation();
                }
#endif
            }
        }

        if (data.giMode != GlobalIlluminationMode::Off) {
            const int maxBounces = (data.giMode == GlobalIlluminationMode::RTGI) ? 4 : 8;
            if (data.giBounces > maxBounces) data.giBounces = maxBounces;
            ImGui::SliderInt("Rebotes (Bounces)", &data.giBounces, 1, maxBounces);
            if (data.giMode == GlobalIlluminationMode::RTGI) {
                ImGui::TextDisabled("RTGI en tiempo real usa hasta 4 rebotes. Path Tracing llega a 8.");
            }
            const bool isRealtimePt = (data.giMode == GlobalIlluminationMode::PathTracing &&
                                       data.ptMode == PathTracingMode::RealtimeSVGF);
            const int maxSpp = isRealtimePt ? 2 : 4;
            if (data.giRaysPerPixel > maxSpp) {
                data.giRaysPerPixel = maxSpp;
            }
            ImGui::SliderInt(
                data.giMode == GlobalIlluminationMode::PathTracing ? "Muestras por Frame (SPP/Frame)" : "Rayos por Píxel",
                &data.giRaysPerPixel, 1, maxSpp);
            ImGui::SliderFloat("Intensidad GI", &data.giIntensity, 0.0f, 4.0f, "%.2f");
            ImGui::Checkbox("Sombras por Ray Query", &data.giRayTracedShadows);
            ImGui::Checkbox("Denoiser Temporal/Espacial", &data.giDenoiserEnabled);

            if (data.giMode == GlobalIlluminationMode::RTGI) {
                if (data.giDenoiserEnabled) {
                    ImGui::SliderFloat("Alfa Temporal", &data.giTemporalAlpha, 0.02f, 0.50f, "%.2f");
                }
            } else if (data.giMode == GlobalIlluminationMode::PathTracing) {
                if (data.ptMode == PathTracingMode::RealtimeSVGF) {
                    if (data.giDenoiserEnabled) {
                        ImGui::SliderFloat("Alfa Temporal (SVGF)", &data.giTemporalAlpha, 0.02f, 0.50f, "%.2f");
                        ImGui::SliderInt("Iteraciones À-Trous (SVGF)", &data.svgfAtrousIterations, 1, 5);
                        ImGui::SliderFloat("Sigma Luminancia (SVGF)", &data.svgfSigmaLuminance, 0.50f, 5.00f, "%.2f");
                        ImGui::SliderFloat("Sigma Normal (SVGF)", &data.svgfSigmaNormal, 16.0f, 256.0f, "%.1f");
                        ImGui::SliderFloat("Sigma Profundidad (SVGF)", &data.svgfSigmaDepth, 0.25f, 4.00f, "%.2f");
                    }
                    ImGui::TextColored(ImVec4(0.45f, 0.90f, 1.0f, 1.0f),
                                       "SVGF Dual: Difuso Demodulado + Especular por Roughness (1-2 SPP)");
                } else {
                    ImGui::SliderInt("Muestras Máximas (0 = Ilimitado)", &data.ptMaxSamples, 0, 1024);
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                    if (auto* vkRhi = dynamic_cast<VulkanRHI*>(rhi)) {
                        ImGui::Text("Muestras Acumuladas: %u / %d", vkRhi->GetPathTracingSampleCount(), data.ptMaxSamples);
                        if (ImGui::Button("Reiniciar Acumulación")) {
                            vkRhi->ResetPathTracingAccumulation();
                        }
                    }
#endif
                }
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Reflejos en Espacio de Pantalla (SSR)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::Checkbox("Activar SSR", &data.ssrEnabled);
        if (data.ssrEnabled) {
            const char* ssrOptions[] = { "Baja (mitad de resolución)", "Media (75% resolución)", "Alta (resolución completa)" };
            ImGui::Combo("Calidad SSR", &data.ssrQuality, ssrOptions, 3);
            ImGui::SliderFloat("Límite de Rugosidad (Roughness Cutoff)", &data.ssrRoughnessCutoff, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Grosor de Detección (Thickness)", &data.ssrThickness, 0.05f, 2.0f, "%.2f m");
            ImGui::SliderFloat("Suavizado de Bordes (Edge Fade)", &data.ssrEdgeFade, 0.01f, 0.5f, "%.2f");
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Sonda de Reflejos Cubemap (Bakeado)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        if (ImGui::Checkbox("Activar Reflejos por Cubemap", &data.reflectionProbeEnabled)) {
            if (data.reflectionProbeEnabled) {
                SlipEditor* ed = SlipEditor::GetInstance();
                if (ed && ed->GetViewportPanel()) ed->GetViewportPanel()->RequestReflectionBake();
            }
        }
        if (data.reflectionProbeEnabled) {
            const char* cubeResOptions[] = { "128x128", "256x256", "512x512", "1024x1024" };
            int currentCubeIdx = (data.reflectionProbeResolution <= 128) ? 0 : ((data.reflectionProbeResolution <= 256) ? 1 : ((data.reflectionProbeResolution <= 512) ? 2 : 3));
            if (ImGui::Combo("Resolución Cubemap", &currentCubeIdx, cubeResOptions, 4)) {
                if (currentCubeIdx == 0) data.reflectionProbeResolution = 128;
                else if (currentCubeIdx == 1) data.reflectionProbeResolution = 256;
                else if (currentCubeIdx == 2) data.reflectionProbeResolution = 512;
                else if (currentCubeIdx == 3) data.reflectionProbeResolution = 1024;
                SlipEditor* ed = SlipEditor::GetInstance();
                if (ed && ed->GetViewportPanel()) ed->GetViewportPanel()->RequestReflectionBake();
            }
            ImGui::Checkbox("Incluir Objetos Dinámicos en Tiempo Real", &data.reflectionIncludeDynamic);
            ImGui::SliderFloat("Intensidad de Reflejo", &data.reflectionIntensity, 0.0f, 3.0f, "%.2f");
            ImGui::SliderFloat("Límite Rugosidad Reflejos (Cutoff)", &data.reflectionRoughnessCutoff, 0.05f, 1.0f, "%.2f");

            SlipEditor* editor = SlipEditor::GetInstance();
            if (editor && editor->GetViewportPanel()) {
                if (ImGui::Button("Re-hornear Reflejos (Bake)", ImVec2(220, 28))) {
                    editor->GetViewportPanel()->RequestReflectionBake();
                    SlipConsole::LogSuccess("Horneando reflejos de Cubemap para la escena...");
                }
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Cielo Gradiente Procedimental y Sol Ficticio", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::Checkbox("Activar Cielo Gradiente", &data.skyEnabled);
        if (data.skyEnabled) {
            ImGui::ColorEdit3("Color Zenit (Cénit)", glm::value_ptr(data.skyZenithColor));
            ImGui::ColorEdit3("Color Horizonte", glm::value_ptr(data.skyHorizonColor));
            ImGui::ColorEdit3("Color Suelo", glm::value_ptr(data.skyGroundColor));
            ImGui::SliderFloat("Nitidez de Horizonte", &data.skyHorizonSharpness, 0.1f, 8.0f, "%.1f");

            ImGui::Separator();
            ImGui::Checkbox("Activar Sol Ficticio (Alineado con Luz Direccional)", &data.sunDiscEnabled);
            if (data.sunDiscEnabled) {
                ImGui::SliderFloat("Tamaño del Disco Solar", &data.sunSize, 0.01f, 0.35f, "%.3f");
                ImGui::SliderFloat("Resplandor Atmosférico (Glow)", &data.sunGlow, 0.01f, 1.0f, "%.2f");
                ImGui::SliderFloat("Intensidad Solar", &data.sunIntensity, 0.0f, 10.0f, "%.2f");
                ImGui::ColorEdit3("Color del Sol", glm::value_ptr(data.sunColor));
            }
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Rendimiento y Optimización", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::Checkbox("Activar Frustum Culling (Árbol Espacial BVH)", &data.frustumCullingEnabled);
        ImGui::TextDisabled("Descarta instantáneamente nodos de arquitectura y mallas fuera del cono visual en CPU.");
        ImGui::Checkbox("Activar Occlusion Culling (Hardware Queries)", &data.occlusionCullingEnabled);
        ImGui::TextDisabled("Descarta objetos ocultos detrás de muros y habitaciones mediante queries asincrónicas de GPU.");
        ImGui::Spacing();

        const char* currentLabel = "Alta (4 Huesos por vertice)";
        if (data.skinningQuality == 0) currentLabel = "Desactivada (0 Huesos - Pose fija)";
        else if (data.skinningQuality == 1) currentLabel = "Baja (1 Hueso por vertice)";
        else if (data.skinningQuality == 2) currentLabel = "Media (2 Huesos por vertice)";
        else if (data.skinningQuality == 4) currentLabel = "Alta (4 Huesos por vertice)";

        if (ImGui::BeginCombo("Calidad de Deformacion (Skinning)", currentLabel)) {
            if (ImGui::Selectable("Alta (4 Huesos por vertice - Suave)", data.skinningQuality == 4)) data.skinningQuality = 4;
            if (ImGui::Selectable("Media (2 Huesos por vertice - Equilibrado)", data.skinningQuality == 2)) data.skinningQuality = 2;
            if (ImGui::Selectable("Baja (1 Hueso por vertice - Rapido)", data.skinningQuality == 1)) data.skinningQuality = 1;
            if (ImGui::Selectable("Desactivada (Bind pose fija)", data.skinningQuality == 0)) data.skinningQuality = 0;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Ajusta globalmente las influencias de huesos calculadas por vertice en el shader GPU.");
    }
#endif

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Guardado y persistencia
    std::string configDir = SlipProject::Get().GetCurrentProject().configPath;
    if (configDir.empty()) configDir = "config";
    std::string iniPath = SlipFileSystem::CombinePath(configDir, "GraphicsSettings.ini");

    if (ImGui::Button("Guardar Configuración", ImVec2(180, 32))) {
        settingsMgr.SaveToFile(iniPath);
    }

    ImGui::SameLine();
    if (ImGui::Button("Restablecer por Defecto", ImVec2(180, 32))) {
        settingsMgr.ResetToDefaults();
        settingsMgr.SaveToFile(iniPath);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Archivo: %s", iniPath.c_str());
}

void ProjectSettingsPanel::RenderPhysicsCategory() {
    auto& project = SlipProject::Get();
    auto& projInfo = project.GetCurrentProject();

    ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Ajustes del Motor de Física (Bullet3)");
    ImGui::TextDisabled("Parámetros globales de simulación física y paso de tiempo fijo.");
    ImGui::Separator();
    ImGui::Spacing();

    float grav[3] = { projInfo.gravity.x, projInfo.gravity.y, projInfo.gravity.z };
    if (ImGui::DragFloat3("Gravedad Global (m/s²)", grav, 0.1f)) {
        projInfo.gravity = glm::vec3(grav[0], grav[1], grav[2]);
        SlipPhysics::Get().setGravity(projInfo.gravity);
    }

    if (ImGui::SliderInt("Iteraciones del Solver", &projInfo.solverIterations, 4, 32)) {
    }

    float fixedHz = (projInfo.fixedTimestep > 0.0f) ? (1.0f / projInfo.fixedTimestep) : 60.0f;
    if (ImGui::DragFloat("Frecuencia de Física (Fixed Hz)", &fixedHz, 1.0f, 10.0f, 240.0f, "%.0f Hz")) {
        projInfo.fixedTimestep = 1.0f / (std::max)(1.0f, fixedHz);
        SlipGlobals::Get().SetFixedDeltaTime(projInfo.fixedTimestep);
    }
    ImGui::TextDisabled("Paso de tiempo fijo de física: %.4f s (%.1f ms)", projInfo.fixedTimestep, projInfo.fixedTimestep * 1000.0f);

    ImGui::Spacing();
    if (ImGui::Button("Guardar Ajustes de Física", ImVec2(200, 30))) {
        if (project.SaveCurrentProject()) {
            SlipConsole::LogSuccess("Ajustes de física guardados en el proyecto.");
        }
    }
}

void ProjectSettingsPanel::ClearInputListen() {
    m_ListenKind = 0;
    m_ListenIndex = -1;
    m_ListenSlot = -1;
}

void ProjectSettingsPanel::DrawBindingControl(const char* label, InputBinding& binding, int listenKind, int index, int slot) {
    ImGui::PushID(slot);
    const bool listening = m_ListenKind == listenKind && m_ListenIndex == index && m_ListenSlot == slot;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (listening) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Presiona una tecla (Esc para cancelar)");
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) ClearInputListen();
    } else {
        int option = SlipInputMap::OptionFromBinding(binding);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##binding", SlipInputMap::BindingOptionLabel(option))) {
            int count = SlipInputMap::BindingOptionCount();
            for (int i = 0; i < count; ++i) {
                const char* optionLabel = SlipInputMap::BindingOptionLabel(i);
                if (!SlipInputMap::IsBindingOptionSelectable(i)) {
                    ImGui::Selectable(optionLabel, false, ImGuiSelectableFlags_Disabled);
                    continue;
                }
                if (ImGui::Selectable(optionLabel, i == option)) {
                    binding = SlipInputMap::BindingFromOption(i);
                }
                if (i == option) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Asignar")) {
            m_ListenKind = listenKind;
            m_ListenIndex = index;
            m_ListenSlot = slot;
        }
    }
    ImGui::PopID();
}

void ProjectSettingsPanel::RenderInputCategory() {
    auto& map = SlipInputMap::Get();

    if (m_ListenKind != 0 && SlipInput::HasInstance()) {
        SlipInput& input = SlipInput::Get();
        if (input.WasKeyPressed(SDL_SCANCODE_ESCAPE)) {
            ClearInputListen();
        } else {
            int kind = 0;
            int code = 0;
            if (input.PollPressedBinding(kind, code)) {
                InputBinding pressed;
                pressed.kind = static_cast<InputBindingKind>(kind);
                pressed.code = code;
                if (m_ListenKind == 1) {
                    auto& actions = map.GetActions();
                    if (m_ListenIndex >= 0 && m_ListenIndex < static_cast<int>(actions.size())) {
                        auto& bindings = actions[m_ListenIndex].bindings;
                        if (m_ListenSlot >= 0 && m_ListenSlot < static_cast<int>(bindings.size())) {
                            bindings[m_ListenSlot] = pressed;
                        }
                    }
                } else if (m_ListenKind == 2) {
                    auto& axes = map.GetAxes();
                    if (m_ListenIndex >= 0 && m_ListenIndex < static_cast<int>(axes.size())) {
                        InputAxis& axis = axes[m_ListenIndex];
                        InputBinding* slot = nullptr;
                        if (m_ListenSlot == 0) slot = &axis.negative;
                        else if (m_ListenSlot == 1) slot = &axis.positive;
                        else if (m_ListenSlot == 2) slot = &axis.altNegative;
                        else if (m_ListenSlot == 3) slot = &axis.altPositive;
                        if (slot) *slot = pressed;
                    }
                }
                ClearInputListen();
            }
        }
    }

    ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Ajustes de Entrada (Input)");
    ImGui::TextDisabled("Acciones (Jump) y ejes de un solo eje (Horizontal). En Lua: Slip.Input.GetAction y GetAxis.");
    ImGui::Separator();
    ImGui::Spacing();

    auto uniqueName = [](auto& list, const char* base) {
        std::string name = base;
        int suffix = 2;
        bool exists = true;
        while (exists) {
            exists = false;
            for (const auto& item : list) {
                if (item.name == name) {
                    exists = true;
                    name = std::string(base) + std::to_string(suffix++);
                    break;
                }
            }
        }
        return name;
    };

    if (ImGui::CollapsingHeader("Acciones", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Una acción está pulsada si cualquiera de sus teclas lo está.");
        auto& actions = map.GetActions();
        for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
            ImGui::PushID(i);
            InputAction& action = actions[i];
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", action.name.c_str());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
                action.name = nameBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quitar")) {
                actions.erase(actions.begin() + i);
                ClearInputListen();
                ImGui::PopID();
                break;
            }
            if (action.bindings.empty()) action.bindings.push_back(InputBinding{});
            for (int b = 0; b < static_cast<int>(action.bindings.size()); ++b) {
                ImGui::PushID(b + 1000);
                DrawBindingControl("Tecla", action.bindings[b], 1, i, b);
                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    action.bindings.erase(action.bindings.begin() + b);
                    ClearInputListen();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ Tecla")) {
                action.bindings.push_back(InputBinding{});
            }
            ImGui::SameLine();
            if (ImGui::Button("Crear botón táctil")) {
                CreateTouchActionButton(action.name);
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("Añadir acción")) {
            InputAction action;
            action.name = uniqueName(actions, "Accion");
            action.bindings.push_back(InputBinding{});
            actions.push_back(action);
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Ejes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("El teclado devuelve -1, 0 o 1. El stick usa el valor de mayor magnitud.");
        auto& axes = map.GetAxes();
        for (int i = 0; i < static_cast<int>(axes.size()); ++i) {
            ImGui::PushID(i + 5000);
            InputAxis& axis = axes[i];
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", axis.name.c_str());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
                axis.name = nameBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quitar")) {
                axes.erase(axes.begin() + i);
                ClearInputListen();
                ImGui::PopID();
                break;
            }
            DrawBindingControl("Negativo", axis.negative, 2, i, 0);
            DrawBindingControl("Positivo", axis.positive, 2, i, 1);
            DrawBindingControl("Alternativa negativa", axis.altNegative, 2, i, 2);
            DrawBindingControl("Alternativa positiva", axis.altPositive, 2, i, 3);

            int axisOption = SlipInputMap::OptionFromGamepadAxis(axis.gamepadAxis);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Eje del mando", SlipInputMap::GamepadAxisOptionLabel(axisOption))) {
                int count = SlipInputMap::GamepadAxisOptionCount();
                for (int option = 0; option < count; ++option) {
                    if (ImGui::Selectable(SlipInputMap::GamepadAxisOptionLabel(option), option == axisOption)) {
                        axis.gamepadAxis = SlipInputMap::GamepadAxisFromOption(option);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Zona muerta", &axis.deadzone, 0.0f, 0.9f, "%.2f");
            ImGui::Checkbox("Invertir", &axis.invert);
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("Añadir eje")) {
            InputAxis axis;
            axis.name = uniqueName(axes, "Eje");
            axis.deadzone = 0.15f;
            axes.push_back(axis);
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Controles táctiles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("El joystick solo actúa cuando el juego corre en Android. Cada stick escribe dos ejes.");
        auto& sticks = map.GetTouchSticks();
        auto& axes = map.GetAxes();
        auto axisLabel = [&](const std::string& axisName) -> const char* {
            for (const InputAxis& axis : axes) {
                if (axis.name == axisName) return axis.name.c_str();
            }
            return axisName.empty() ? "(Ninguno)" : axisName.c_str();
        };
        for (int i = 0; i < static_cast<int>(sticks.size()); ++i) {
            ImGui::PushID(i + 9000);
            InputTouchStick& stick = sticks[i];
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", stick.name.c_str());
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Nombre", nameBuf, sizeof(nameBuf))) {
                stick.name = nameBuf;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quitar")) {
                sticks.erase(sticks.begin() + i);
                ImGui::PopID();
                break;
            }

            auto drawAxisCombo = [&](const char* label, std::string& axisName) {
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo(label, axisName.empty() ? "(Ninguno)" : axisLabel(axisName))) {
                    if (ImGui::Selectable("(Ninguno)", axisName.empty())) axisName.clear();
                    for (const InputAxis& axis : axes) {
                        if (ImGui::Selectable(axis.name.c_str(), axis.name == axisName)) {
                            axisName = axis.name;
                        }
                    }
                    ImGui::EndCombo();
                }
            };
            drawAxisCombo("Eje X", stick.axisX);
            drawAxisCombo("Eje Y", stick.axisY);
            ImGui::SliderFloat("Zona muerta", &stick.deadzone, 0.0f, 0.9f, "%.2f");
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("Añadir stick")) {
            InputTouchStick stick;
            stick.name = uniqueName(sticks, "Stick");
            if (!axes.empty()) {
                stick.axisX = axes[0].name;
                if (axes.size() > 1) stick.axisY = axes[1].name;
            }
            stick.deadzone = 0.15f;
            sticks.push_back(stick);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::string configDir = SlipProject::Get().GetCurrentProject().configPath;
    if (configDir.empty()) configDir = "config";
    std::string iniPath = SlipFileSystem::CombinePath(configDir, "InputSettings.ini");

    if (ImGui::Button("Guardar Configuración", ImVec2(180, 32))) {
        map.SaveToFile(iniPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restablecer por Defecto", ImVec2(180, 32))) {
        ClearInputListen();
        map.ResetToDefaults();
        map.SaveToFile(iniPath);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Archivo: %s", iniPath.c_str());
}

