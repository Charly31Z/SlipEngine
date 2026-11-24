#include "SlipEngine/editor/panels/AnimationEditorPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/core/SlipGlobals.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
#else
    #include <glad/glad.h>
#endif

#ifdef GetCurrentTime
    #undef GetCurrentTime
#endif

AnimationEditorPanel::AnimationEditorPanel() {
    s_Instance = this;
    m_IsOpen = false;
    m_PreviewSkeleton = std::make_shared<SlipSkeleton>();
    m_SequencerClip = std::make_shared<KeyframeClip>("Cinematica_Camara", 5.0f, 30.0f);
}

AnimationEditorPanel::~AnimationEditorPanel() {
    if (s_Instance == this) {
        s_Instance = nullptr;
    }
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) {
        return;
    }
    if (m_DepthRBO != 0) {
        glDeleteRenderbuffers(1, &m_DepthRBO);
        m_DepthRBO = 0;
    }
    if (m_ColorTexture != 0) {
        glDeleteTextures(1, &m_ColorTexture);
        m_ColorTexture = 0;
    }
    if (m_FBO != 0) {
        glDeleteFramebuffers(1, &m_FBO);
        m_FBO = 0;
    }
}

void AnimationEditorPanel::ResizeFBO(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_FBO != 0 && m_ViewportWidth == width && m_ViewportHeight == height) return;

    m_ViewportWidth = width;
    m_ViewportHeight = height;

    if (m_FBO == 0) glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    if (m_ColorTexture == 0) glGenTextures(1, &m_ColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

    if (m_DepthRBO == 0) glGenRenderbuffers(1, &m_DepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_DepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void AnimationEditorPanel::SetTargetEntity(SlipEntity* entity) {
    if (m_TargetEntity != entity) {
        m_TargetEntity = entity;
        m_ForcePreviewUpdate = true;
        if (m_TargetEntity) {
            m_SkinnedMeshComp = m_TargetEntity->GetComponent<SkinnedMeshComponent>().get();
            m_AnimatorComp = m_TargetEntity->GetComponent<AnimatorComponent>().get();
            if (auto kfa = m_TargetEntity->GetComponent<KeyframeAnimationComponent>()) {
                if (kfa->clip) {
                    m_SequencerClip = kfa->clip;
                }
            }
        } else {
            m_SkinnedMeshComp = nullptr;
            m_AnimatorComp = nullptr;
        }
    }
}

void AnimationEditorPanel::LoadModelForPreview(const std::string& modelPath) {
    if (modelPath.empty() || !SlipFileSystem::Exists(modelPath)) return;

    m_LoadedModelPath = modelPath;
    m_PreviewMesh = std::make_unique<SlipMesh>(modelPath.c_str());
    m_PreviewMesh->init();

    m_PreviewClips.clear();
    m_PreviewSkeleton = std::make_shared<SlipSkeleton>();
    SlipModelExtract::ExtractSkeletonAndAnimations(modelPath, *m_PreviewSkeleton, m_PreviewClips);
    m_SelectedClipIdx = 0;
    m_CurrentTime = 0.0f;
    SlipConsole::LogInfo("Modelo cargado en Preview de Animacion: " + modelPath + " (" + std::to_string(m_PreviewClips.size()) + " clips)");
}

void AnimationEditorPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(850, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Editor de Animacion & Secuenciador", &m_IsOpen)) {
        // Sincronizar automáticamente con la entidad seleccionada en la escena
        if (m_SelectedEntity && m_SelectedEntity != m_TargetEntity) {
            SetTargetEntity(m_SelectedEntity);
        }

        // Pestañas superiores de navegación
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 6.0f));

        bool isSeq = (m_PanelTab == 0);
        bool isSkel = (m_PanelTab == 1);

        if (isSeq) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        if (ImGui::Button(" [Secuenciador Keyframes (Cinematicas / Unity / Roblox)] ", ImVec2(0, 26))) {
            m_PanelTab = 0;
        }
        if (isSeq) ImGui::PopStyleColor();

        ImGui::SameLine();

        if (isSkel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        if (ImGui::Button(" [Preview Esqueletico 3D (FBX / GLTF)] ", ImVec2(0, 26))) {
            m_PanelTab = 1;
        }
        if (isSkel) ImGui::PopStyleColor();

        ImGui::PopStyleVar(2);
        ImGui::Separator();

        if (m_PanelTab == 0) {
            RenderKeyframeSequencer();
        } else {
            RenderSkeletalPreview();
        }
    }
    ImGui::End();
}

// =========================================================================
// 1. SECUENCIADOR DE ANIMACIÓN POR KEYFRAME (Estilo Unity / UE5 / Roblox)
// =========================================================================
void AnimationEditorPanel::RenderKeyframeSequencer() {
    if (!m_SequencerClip) {
        m_SequencerClip = std::make_shared<KeyframeClip>("Cinematica_Camara", 5.0f, 30.0f);
    }

    float dt = ImGui::GetIO().DeltaTime;
    UpdateSequencerPreview(dt);

    RenderSequencerToolbar();
    ImGui::Separator();
    RenderSequencerTimeline();
    ImGui::Separator();

    float availHeight = ImGui::GetContentRegionAvail().y;
    float leftPaneWidth = 330.0f;

    RenderSequencerDopeSheet(leftPaneWidth, availHeight);
}

void AnimationEditorPanel::RenderSequencerToolbar() {
    // Fila 1: Selector de Entidad Objetivo + Gestión de Clips
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Entidad Objetivo:");
    ImGui::SameLine();

    std::string entLabel = m_TargetEntity ? m_TargetEntity->GetName() : "[Sin Seleccionar]";
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##SeqTargetEnt", entLabel.c_str())) {
        SlipLevel* lvl = m_ContextLevel ? m_ContextLevel : &SlipLevel::Get();
        if (lvl) {
            for (SlipEntity* ent : lvl->GetEntities()) {
                if (!ent) continue;
                bool isSel = (ent == m_TargetEntity);
                std::string name = ent->GetName();
                if (ent->GetComponent<CameraComponent>()) {
                    name += " (Camara)";
                }
                if (ImGui::Selectable(name.c_str(), isSel)) {
                    SetTargetEntity(ent);
                }
                if (isSel) ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::Text("Clip:");
    ImGui::SameLine();
    char nameBuf[128] = { 0 };
    std::strncpy(nameBuf, m_SequencerClip->name.c_str(), sizeof(nameBuf) - 1);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputText("##SeqClipName", nameBuf, sizeof(nameBuf))) {
        m_SequencerClip->name = nameBuf;
    }

    ImGui::SameLine();
    if (ImGui::Button("[+] Nuevo")) {
        m_SequencerClip = std::make_shared<KeyframeClip>("NuevoClip", 5.0f, 30.0f);
        m_SeqCurrentTime = 0.0f;
        m_ForcePreviewUpdate = true;
        m_SelectedTrackIdx = -1;
        m_SelectedKeyframeIdx = -1;
        m_StatusMessage = "Nuevo clip creado";
        m_StatusMessageTimer = 3.0f;
    }

    ImGui::SameLine();
    if (ImGui::Button("Abrir...")) {
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (assetsDir.empty()) assetsDir = "assets";
        std::string animDir = SlipFileSystem::CombinePath(assetsDir, "Animations");
        auto clipFiles = SlipFileSystem::ListDirectoryRecursive(animDir, ".clip");
        if (clipFiles.empty()) {
            clipFiles = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".clip");
        }
        if (!clipFiles.empty()) {
            if (m_SequencerClip->LoadFromFile(clipFiles[0].fullPath)) {
                m_SeqCurrentTime = 0.0f;
                m_ForcePreviewUpdate = true;
                m_StatusMessage = "Cargado: " + clipFiles[0].name;
                m_StatusMessageTimer = 3.0f;
            }
        } else {
            m_StatusMessage = "No se encontraron archivos .clip en assets/";
            m_StatusMessageTimer = 3.0f;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Guardar (.clip)")) {
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (assetsDir.empty()) assetsDir = "assets";
        std::string animDir = SlipFileSystem::CombinePath(assetsDir, "Animations");
        SlipFileSystem::CreateDirectoryPath(animDir);
        std::string safeName = m_SequencerClip->name;
        if (safeName.empty()) safeName = "ClipAnimacion";
        std::string savePath = SlipFileSystem::CombinePath(animDir, safeName + ".clip");
        if (m_SequencerClip->SaveToFile(savePath)) {
            m_StatusMessage = "Clip guardado: " + safeName + ".clip";
            m_StatusMessageTimer = 3.0f;
        }
    }

    ImGui::SameLine();
    if (m_TargetEntity && ImGui::Button("Asignar a Entidad")) {
        auto kfa = m_TargetEntity->GetComponent<KeyframeAnimationComponent>();
        if (!kfa) {
            kfa = m_TargetEntity->AddComponent<KeyframeAnimationComponent>();
        }
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (assetsDir.empty()) assetsDir = "assets";
        std::string animDir = SlipFileSystem::CombinePath(assetsDir, "Animations");
        std::string savePath = SlipFileSystem::CombinePath(animDir, m_SequencerClip->name + ".clip");
        m_SequencerClip->SaveToFile(savePath);
        kfa->clip = m_SequencerClip;
        kfa->clipPath = "Animations/" + m_SequencerClip->name + ".clip";
        kfa->loop = m_SeqLoop;
        m_StatusMessage = "Clip asignado a " + m_TargetEntity->GetName();
        m_StatusMessageTimer = 3.0f;
    }

    if (m_TargetEntity && m_TargetEntity->GetComponent<SkinnedMeshComponent>()) {
        ImGui::SameLine();
        if (ImGui::Button("[+ Huesos]")) {
            auto smc = m_TargetEntity->GetComponent<SkinnedMeshComponent>();
            if (smc && smc->skeleton) {
                int addedCount = 0;
                for (const auto& bone : smc->skeleton->GetBones()) {
                    auto& boneGrp = m_SequencerClip->GetOrCreateGroup(bone.name);
                    boneGrp.GetOrCreateTrack(AnimatableProperty::Bone_RotX, "Rotacion X");
                    boneGrp.GetOrCreateTrack(AnimatableProperty::Bone_RotY, "Rotacion Y");
                    boneGrp.GetOrCreateTrack(AnimatableProperty::Bone_RotZ, "Rotacion Z");
                    addedCount++;
                }
                m_StatusMessage = "Anadidos " + std::to_string(addedCount) + " huesos al secuenciador";
                m_StatusMessageTimer = 3.0f;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Anade automaticamente pistas de rotacion para todos los huesos del SkinnedMesh");
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Exportar a .anim")) {
        auto skelClip = m_SequencerClip->ToSkeletalAnimationClip();
        if (skelClip && !skelClip->tracks.empty()) {
            std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
            if (assetsDir.empty()) assetsDir = "assets";
            std::string animDir = SlipFileSystem::CombinePath(assetsDir, "Animations");
            SlipFileSystem::CreateDirectoryPath(animDir);
            std::string safeName = m_SequencerClip->name;
            if (safeName.empty()) safeName = "SkeletalAnim";
            std::string savePath = SlipFileSystem::CombinePath(animDir, safeName + ".clip");
            m_SequencerClip->SaveToFile(savePath);

            if (m_TargetEntity) {
                auto animComp = m_TargetEntity->GetComponent<AnimatorComponent>();
                if (!animComp) animComp = m_TargetEntity->AddComponent<AnimatorComponent>();
                if (animComp) {
                    animComp->AddClip(skelClip);
                    animComp->Play(skelClip->name);
                }
            }
            m_StatusMessage = "Clip esqueletico exportado con " + std::to_string(skelClip->tracks.size()) + " huesos!";
            m_StatusMessageTimer = 3.5f;
        } else {
            m_StatusMessage = "No hay pistas de huesos/transform para exportar a .anim";
            m_StatusMessageTimer = 3.0f;
        }
    }

    // Fila 2: Propiedades del Clip (Duración, FPS, Loop, Velocidad)
    ImGui::Spacing();
    ImGui::SetNextItemWidth(75.0f);
    if (ImGui::DragFloat("Duracion (s)", &m_SequencerClip->duration, 0.1f, 0.2f, 120.0f, "%.1f s")) {
        if (m_SequencerClip->duration < 0.2f) m_SequencerClip->duration = 0.2f;
    }
    ImGui::SameLine();

    const char* fpsOptions[] = { "24 FPS", "30 FPS", "60 FPS" };
    int fpsIdx = (m_SequencerClip->frameRate >= 50.0f) ? 2 : ((m_SequencerClip->frameRate <= 25.0f) ? 0 : 1);
    ImGui::SetNextItemWidth(85.0f);
    if (ImGui::Combo("FPS", &fpsIdx, fpsOptions, IM_ARRAYSIZE(fpsOptions))) {
        if (fpsIdx == 0) m_SequencerClip->frameRate = 24.0f;
        else if (fpsIdx == 1) m_SequencerClip->frameRate = 30.0f;
        else m_SequencerClip->frameRate = 60.0f;
    }
    ImGui::SameLine();

    ImGui::Checkbox("Bucle (Loop)", &m_SeqLoop);
    m_SequencerClip->loop = m_SeqLoop;
    ImGui::SameLine();

    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderFloat("Velocidad", &m_SeqPlaybackSpeed, 0.1f, 3.0f, "%.1fx");

    if (m_StatusMessageTimer > 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "  %s", m_StatusMessage.c_str());
    }
}

void AnimationEditorPanel::RenderSequencerTimeline() {
    float totalDur = (m_SequencerClip && m_SequencerClip->duration > 0.0f) ? m_SequencerClip->duration : 5.0f;
    float fps = (m_SequencerClip && m_SequencerClip->frameRate > 0.0f) ? m_SequencerClip->frameRate : 30.0f;
    int currentFrame = static_cast<int>(m_SeqCurrentTime * fps);
    int totalFrames = static_cast<int>(totalDur * fps);

    // Botones de transporte
    if (ImGui::Button(" |< ")) {
        m_SeqCurrentTime = 0.0f;
        m_ForcePreviewUpdate = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(" < ")) {
        m_SeqCurrentTime = (std::max)(0.0f, m_SeqCurrentTime - (1.0f / fps));
        m_ForcePreviewUpdate = true;
    }
    ImGui::SameLine();
    const char* playIcon = m_SeqIsPlaying ? " || " : " > ";
    if (ImGui::Button(playIcon, ImVec2(40, 0))) {
        m_SeqIsPlaying = !m_SeqIsPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button(" > ")) {
        m_SeqCurrentTime = (std::min)(totalDur, m_SeqCurrentTime + (1.0f / fps));
        m_ForcePreviewUpdate = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(" >| ")) {
        m_SeqCurrentTime = totalDur;
        m_ForcePreviewUpdate = true;
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // BOTÓN MÁGICO ESTILO ROBLOX / UNITY: Grabar / Capturar todas las propiedades en este fotograma
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.30f, 0.30f, 1.0f));
    if (ImGui::Button(" [*] Grabar Keyframes (Key All) ", ImVec2(0, 24))) {
        if (m_TargetEntity) {
            std::string grpName = m_TargetEntity->GetName();
            auto& grp = m_SequencerClip->GetOrCreateGroup(grpName);
            grp.CaptureAll(m_TargetEntity, m_SeqCurrentTime);
            m_SeqLastSampledTime = m_SeqCurrentTime;
            m_StatusMessage = "Keyframes capturados en t = " + std::to_string(m_SeqCurrentTime).substr(0, 4) + "s";
            m_StatusMessageTimer = 2.5f;
        } else {
            m_StatusMessage = "Selecciona una entidad primero!";
            m_StatusMessageTimer = 3.0f;
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::Text("  Tiempo: %.2fs / %.2fs  (Frame %d / %d)", m_SeqCurrentTime, totalDur, currentFrame, totalFrames);

    // Scrubber horizontal
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##SeqScrubber", &m_SeqCurrentTime, 0.0f, totalDur, "%.3f s")) {
        m_ForcePreviewUpdate = true;
    }
}

void AnimationEditorPanel::RenderSequencerDopeSheet(float leftPaneWidth, float contentHeight) {
    if (!m_SequencerClip) return;

    std::string grpName = m_TargetEntity ? m_TargetEntity->GetName() : "DefaultEntity";
    EntityAnimationTrackGroup& group = m_SequencerClip->GetOrCreateGroup(grpName);

    float totalDur = (m_SequencerClip->duration > 0.0f) ? m_SequencerClip->duration : 5.0f;
    float rowHeight = 28.0f;
    float rulerHeight = 26.0f;

    // --- PANEL IZQUIERDO: LISTA DE PISTAS Y PROPIEDADES ---
    ImGui::BeginChild("##SeqTrackListChild", ImVec2(leftPaneWidth, contentHeight), true);
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Pistas / Propiedades (%d)", (int)group.tracks.size());
        ImGui::SameLine();

        // Menú desplegable para añadir pistas
        if (ImGui::SmallButton("[+] Pista...")) {
            ImGui::OpenPopup("AddPropertyTrackPopup");
        }

        if (ImGui::BeginPopup("AddPropertyTrackPopup")) {
            ImGui::TextDisabled("Transform:");
            if (ImGui::MenuItem("Posicion X")) group.GetOrCreateTrack(AnimatableProperty::Transform_PosX, "Posicion X");
            if (ImGui::MenuItem("Posicion Y")) group.GetOrCreateTrack(AnimatableProperty::Transform_PosY, "Posicion Y");
            if (ImGui::MenuItem("Posicion Z")) group.GetOrCreateTrack(AnimatableProperty::Transform_PosZ, "Posicion Z");
            if (ImGui::MenuItem("Posicion (X, Y, Z)")) {
                group.GetOrCreateTrack(AnimatableProperty::Transform_PosX, "Posicion X");
                group.GetOrCreateTrack(AnimatableProperty::Transform_PosY, "Posicion Y");
                group.GetOrCreateTrack(AnimatableProperty::Transform_PosZ, "Posicion Z");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rotacion X")) group.GetOrCreateTrack(AnimatableProperty::Transform_RotX, "Rotacion X");
            if (ImGui::MenuItem("Rotacion Y")) group.GetOrCreateTrack(AnimatableProperty::Transform_RotY, "Rotacion Y");
            if (ImGui::MenuItem("Rotacion Z")) group.GetOrCreateTrack(AnimatableProperty::Transform_RotZ, "Rotacion Z");
            if (ImGui::MenuItem("Rotacion (X, Y, Z)")) {
                group.GetOrCreateTrack(AnimatableProperty::Transform_RotX, "Rotacion X");
                group.GetOrCreateTrack(AnimatableProperty::Transform_RotY, "Rotacion Y");
                group.GetOrCreateTrack(AnimatableProperty::Transform_RotZ, "Rotacion Z");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Escala (X, Y, Z)")) {
                group.GetOrCreateTrack(AnimatableProperty::Transform_ScaleX, "Escala X");
                group.GetOrCreateTrack(AnimatableProperty::Transform_ScaleY, "Escala Y");
                group.GetOrCreateTrack(AnimatableProperty::Transform_ScaleZ, "Escala Z");
            }

            ImGui::Separator();
            ImGui::TextDisabled("Efectos de Camara:");
            if (ImGui::MenuItem("Camara: Campo de Vision (FOV)")) {
                group.GetOrCreateTrack(AnimatableProperty::Camera_FOV, "Camara FOV");
            }
            if (ImGui::MenuItem("Camara: Bordes Negros (Letterbox H)")) {
                group.GetOrCreateTrack(AnimatableProperty::Camera_Letterbox, "Camara Letterbox");
            }
            if (ImGui::MenuItem("Camara: Bordes Negros (Pillarbox V)")) {
                group.GetOrCreateTrack(AnimatableProperty::Camera_Pillarbox, "Camara Pillarbox");
            }
            if (ImGui::MenuItem("Camara: Intensidad Shake (Sacudida)")) {
                group.GetOrCreateTrack(AnimatableProperty::Camera_ShakeIntensity, "Camara Shake");
            }

            ImGui::Separator();
            ImGui::TextDisabled("Iluminacion:");
            if (ImGui::MenuItem("Luz: Intensidad")) {
                group.GetOrCreateTrack(AnimatableProperty::Light_Intensity, "Luz Intensidad");
            }

            ImGui::Separator();
            ImGui::TextDisabled("UI / RectTransform (Transiciones & HUD):");
            if (ImGui::MenuItem("UI: Posicion Anclada (X, Y)")) {
                group.GetOrCreateTrack(AnimatableProperty::UI_AnchoredPosX, "UI: Posicion X");
                group.GetOrCreateTrack(AnimatableProperty::UI_AnchoredPosY, "UI: Posicion Y");
            }
            if (ImGui::MenuItem("UI: Tamano (Ancho, Alto)")) {
                group.GetOrCreateTrack(AnimatableProperty::UI_Width, "UI: Ancho");
                group.GetOrCreateTrack(AnimatableProperty::UI_Height, "UI: Alto");
            }
            if (ImGui::MenuItem("UI: Alfa (Opacidad / Fade)")) {
                group.GetOrCreateTrack(AnimatableProperty::UI_Alpha, "UI: Alfa Fade");
            }
            if (ImGui::MenuItem("UI: Color (R, G, B)")) {
                group.GetOrCreateTrack(AnimatableProperty::UI_ColorR, "UI: Color R");
                group.GetOrCreateTrack(AnimatableProperty::UI_ColorG, "UI: Color G");
                group.GetOrCreateTrack(AnimatableProperty::UI_ColorB, "UI: Color B");
            }
            if (ImGui::MenuItem("UI: Progreso / Barra (Value)")) {
                group.GetOrCreateTrack(AnimatableProperty::UI_ProgressValue, "UI: Progreso");
            }
            if (ImGui::MenuItem("UI: Tamano de Fuente")) {
                group.GetOrCreateTrack(AnimatableProperty::UI_FontSize, "UI: Tamano Fuente");
            }

            ImGui::Separator();
            ImGui::TextDisabled("Huesos / Esqueleto 3D:");
            if (ImGui::MenuItem("Hueso: Rotacion (X, Y, Z)")) {
                group.GetOrCreateTrack(AnimatableProperty::Bone_RotX, "Hueso: Rotacion X");
                group.GetOrCreateTrack(AnimatableProperty::Bone_RotY, "Hueso: Rotacion Y");
                group.GetOrCreateTrack(AnimatableProperty::Bone_RotZ, "Hueso: Rotacion Z");
            }
            if (ImGui::MenuItem("Hueso: Posicion (X, Y, Z)")) {
                group.GetOrCreateTrack(AnimatableProperty::Bone_PosX, "Hueso: Posicion X");
                group.GetOrCreateTrack(AnimatableProperty::Bone_PosY, "Hueso: Posicion Y");
                group.GetOrCreateTrack(AnimatableProperty::Bone_PosZ, "Hueso: Posicion Z");
            }

            ImGui::EndPopup();
        }

        ImGui::Dummy(ImVec2(0.0f, rulerHeight - 20.0f));
        ImGui::Separator();

        // Renderizar lista de pistas en filas coincidentes
        AnimatableProperty trackToRemove = AnimatableProperty::Custom;
        for (int i = 0; i < (int)group.tracks.size(); ++i) {
            auto& trk = group.tracks[i];
            ImGui::PushID(i);

            // Color tag según tipo
            ImVec4 tagColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
            if (trk.propertyType == AnimatableProperty::Transform_PosX) tagColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            else if (trk.propertyType == AnimatableProperty::Transform_PosY) tagColor = ImVec4(0.3f, 0.9f, 0.3f, 1.0f);
            else if (trk.propertyType == AnimatableProperty::Transform_PosZ) tagColor = ImVec4(0.3f, 0.5f, 0.9f, 1.0f);
            else if (trk.propertyType == AnimatableProperty::Camera_FOV) tagColor = ImVec4(0.2f, 0.9f, 0.9f, 1.0f);
            else if (trk.propertyType == AnimatableProperty::Camera_Letterbox) tagColor = ImVec4(0.95f, 0.8f, 0.2f, 1.0f);
            else if (trk.propertyType == AnimatableProperty::Camera_ShakeIntensity) tagColor = ImVec4(0.95f, 0.5f, 0.2f, 1.0f);
            else if (trk.propertyType >= AnimatableProperty::UI_AnchoredPosX && trk.propertyType <= AnimatableProperty::UI_FontSize) tagColor = ImVec4(0.2f, 0.85f, 0.6f, 1.0f);
            else if (trk.propertyType >= AnimatableProperty::Bone_RotX && trk.propertyType <= AnimatableProperty::Bone_PosZ) tagColor = ImVec4(0.85f, 0.4f, 0.9f, 1.0f);

            ImGui::ColorButton("##ColorTag", tagColor, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(8, 18));
            ImGui::SameLine();

            // Nombre de la pista recortado para que quepa en el panel con tooltip informativo
            std::string displayName = trk.name;
            if (displayName.length() > 14) {
                displayName = displayName.substr(0, 12) + "..";
            }
            ImGui::Text("%-14s", displayName.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", trk.name.c_str());
            }
            ImGui::SameLine();

            // Configurar step, límites y formato según tipo de propiedad
            float dragStep = 0.05f;
            float minVal = 0.0f;
            float maxVal = 0.0f;
            const char* fmt = "%.2f";

            if (trk.propertyType == AnimatableProperty::Camera_Letterbox || trk.propertyType == AnimatableProperty::Camera_Pillarbox) {
                dragStep = 0.005f; minVal = 0.0f; maxVal = 0.5f; fmt = "%.3f";
            } else if (trk.propertyType == AnimatableProperty::Camera_FOV) {
                dragStep = 0.5f; minVal = 1.0f; maxVal = 170.0f; fmt = "%.1f";
            } else if (trk.propertyType == AnimatableProperty::UI_Alpha || trk.propertyType == AnimatableProperty::UI_ProgressValue) {
                dragStep = 0.01f; minVal = 0.0f; maxVal = 1.0f; fmt = "%.2f";
            } else if (trk.propertyType >= AnimatableProperty::UI_ColorR && trk.propertyType <= AnimatableProperty::UI_ColorB) {
                dragStep = 0.01f; minVal = 0.0f; maxVal = 1.0f; fmt = "%.2f";
            } else if (trk.propertyType == AnimatableProperty::UI_AnchoredPosX || trk.propertyType == AnimatableProperty::UI_AnchoredPosY) {
                dragStep = 1.0f; minVal = -10000.0f; maxVal = 10000.0f; fmt = "%.0f";
            } else if (trk.propertyType == AnimatableProperty::UI_Width || trk.propertyType == AnimatableProperty::UI_Height) {
                dragStep = 1.0f; minVal = 0.0f; maxVal = 10000.0f; fmt = "%.0f";
            } else if (trk.propertyType == AnimatableProperty::UI_FontSize) {
                dragStep = 1.0f; minVal = 1.0f; maxVal = 200.0f; fmt = "%.0f";
            } else if (trk.propertyType >= AnimatableProperty::Transform_RotX && trk.propertyType <= AnimatableProperty::Transform_RotZ) {
                dragStep = 0.5f; fmt = "%.1f";
            } else if (trk.propertyType >= AnimatableProperty::Bone_RotX && trk.propertyType <= AnimatableProperty::Bone_RotZ) {
                dragStep = 0.5f; fmt = "%.1f";
            }

            // Campo de edición directa del valor en el tiempo actual
            float curVal = trk.Evaluate(m_SeqCurrentTime);
            float editVal = curVal;
            ImGui::SetNextItemWidth(72.0f);
            if (ImGui::DragFloat("##TrackVal", &editVal, dragStep, minVal, maxVal, fmt)) {
                trk.AddKeyframe(m_SeqCurrentTime, editVal, KeyframeInterpolation::Smooth);
                if (m_TargetEntity) {
                    group.Apply(m_TargetEntity, m_SeqCurrentTime);
                    if (auto smc = m_TargetEntity->GetComponent<SkinnedMeshComponent>()) {
                        smc->MarkBoneMatricesDirty();
                        smc->UpdateBoneMatrices();
                    }
                }
                m_SeqLastSampledTime = m_SeqCurrentTime;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Arrastra o haz clic para editar el valor en t = %.2fs (crea o actualiza el keyframe)", m_SeqCurrentTime);
            }
            ImGui::SameLine();

            // Botón de Añadir/Quitar Keyframe individual a este fotograma
            int keyIdx = trk.FindKeyframeAt(m_SeqCurrentTime, 0.04f);
            bool hasKey = (keyIdx >= 0);

            if (hasKey) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.1f, 1.0f));
            if (ImGui::SmallButton(hasKey ? "[*]" : "[+]")) {
                if (m_TargetEntity) {
                    group.CaptureKeyframe(m_TargetEntity, trk.propertyType, m_SeqCurrentTime);
                } else {
                    trk.AddKeyframe(m_SeqCurrentTime, curVal, KeyframeInterpolation::Smooth);
                }
                m_SeqLastSampledTime = m_SeqCurrentTime;
            }
            if (hasKey) ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                if (hasKey) {
                    ImGui::SetTooltip("Keyframe existente en este tiempo.\n[Clic Izq] Actualizar con el valor de la entidad/Inspector\n[Clic Der] Eliminar keyframe");
                } else {
                    ImGui::SetTooltip("No hay keyframe en este tiempo.\n[Clic] Grabar keyframe con el valor actual");
                }
            }

            // Clic derecho en el botón de keyframe para eliminarlo
            if (hasKey && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                trk.RemoveKeyframe(keyIdx);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                trackToRemove = trk.propertyType;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Eliminar esta pista");
            }

            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::PopID();
        }

        if (trackToRemove != AnimatableProperty::Custom) {
            group.RemoveTrack(trackToRemove);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- PANEL DERECHO: DOPE SHEET CANVAS Y REGLA DE TIEMPO ---
    ImGui::BeginChild("##SeqDopeSheetCanvasChild", ImVec2(0.0f, contentHeight), true);
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        float timelineWidth = canvasSize.x - 20.0f;
        if (timelineWidth < 100.0f) timelineWidth = 100.0f;

        // 1. REGLA DE TIEMPO (Ruler) en la parte superior
        ImVec2 rulerMin = canvasPos;
        ImVec2 rulerMax = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerHeight);
        drawList->AddRectFilled(rulerMin, rulerMax, IM_COL32(35, 38, 44, 255));
        drawList->AddLine(ImVec2(rulerMin.x, rulerMax.y), rulerMax, IM_COL32(70, 75, 85, 255));

        // Marcas y números de segundos
        int numSeconds = static_cast<int>(std::ceil(totalDur));
        for (int s = 0; s <= numSeconds; ++s) {
            float t = static_cast<float>(s);
            if (t > totalDur) break;

            float x = rulerMin.x + (t / totalDur) * timelineWidth;
            drawList->AddLine(ImVec2(x, rulerMax.y - 12.0f), ImVec2(x, rulerMax.y), IM_COL32(180, 185, 195, 255), 1.5f);

            char labelBuf[16];
            snprintf(labelBuf, sizeof(labelBuf), "%ds", s);
            drawList->AddText(ImVec2(x + 3.0f, rulerMin.y + 4.0f), IM_COL32(190, 195, 205, 255), labelBuf);

            // Marcas menores de medio segundo
            float midT = t + 0.5f;
            if (midT <= totalDur) {
                float midX = rulerMin.x + (midT / totalDur) * timelineWidth;
                drawList->AddLine(ImVec2(midX, rulerMax.y - 6.0f), ImVec2(midX, rulerMax.y), IM_COL32(120, 125, 135, 255), 1.0f);
            }
        }

        // Interacción con la regla: Clic o Arrastre para mover scrubber
        ImGuiIO& io = ImGui::GetIO();
        bool isHoveredRuler = (io.MousePos.x >= rulerMin.x && io.MousePos.x <= rulerMax.x &&
                               io.MousePos.y >= rulerMin.y && io.MousePos.y <= rulerMax.y);
        if (isHoveredRuler && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float norm = (io.MousePos.x - rulerMin.x) / timelineWidth;
            m_SeqCurrentTime = glm::clamp(norm * totalDur, 0.0f, totalDur);
            m_ForcePreviewUpdate = true;
        }

        // 2. FILAS DE DOPE SHEET PARA CADA PISTA
        float currentY = rulerMax.y + 1.0f;
        int deleteTrackIdx = -1;
        int deleteKeyIdx = -1;

        for (int trkIdx = 0; trkIdx < (int)group.tracks.size(); ++trkIdx) {
            auto& trk = group.tracks[trkIdx];
            ImVec2 rowMin = ImVec2(canvasPos.x, currentY);
            ImVec2 rowMax = ImVec2(canvasPos.x + canvasSize.x, currentY + rowHeight);

            // Fondo alternado de la fila
            ImU32 rowBg = (trkIdx % 2 == 0) ? IM_COL32(28, 30, 36, 255) : IM_COL32(23, 25, 30, 255);
            drawList->AddRectFilled(rowMin, rowMax, rowBg);
            drawList->AddLine(ImVec2(rowMin.x, rowMax.y), rowMax, IM_COL32(45, 48, 55, 255));

            // Líneas de rejilla vertical
            for (int s = 0; s <= numSeconds; ++s) {
                float t = static_cast<float>(s);
                if (t > totalDur) break;
                float x = canvasPos.x + (t / totalDur) * timelineWidth;
                drawList->AddLine(ImVec2(x, rowMin.y), ImVec2(x, rowMax.y), IM_COL32(40, 43, 50, 150));
            }

            // DIBUJAR LOS DIAMANTES DE CADA KEYFRAME (◆)
            float diamondRadius = 6.0f;
            float rowCenterY = currentY + (rowHeight * 0.5f);

            for (int kIdx = 0; kIdx < (int)trk.keyframes.size(); ++kIdx) {
                const auto& k = trk.keyframes[kIdx];
                float keyX = canvasPos.x + (k.time / totalDur) * timelineWidth;

                // Vértices del rombo/diamante rotado 45°
                ImVec2 pTop(keyX, rowCenterY - diamondRadius);
                ImVec2 pRight(keyX + diamondRadius, rowCenterY);
                ImVec2 pBottom(keyX, rowCenterY + diamondRadius);
                ImVec2 pLeft(keyX - diamondRadius, rowCenterY);

                // Detección de cursor sobre el keyframe
                bool isHovered = (std::abs(io.MousePos.x - keyX) <= diamondRadius + 2.0f &&
                                  std::abs(io.MousePos.y - rowCenterY) <= diamondRadius + 2.0f);

                bool isSel = (m_SelectedTrackIdx == trkIdx && m_SelectedKeyframeIdx == kIdx);
                ImU32 fillColor = isSel ? IM_COL32(255, 215, 0, 255) : (isHovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 180, 250, 255));
                ImU32 borderColor = IM_COL32(20, 20, 25, 255);

                drawList->AddQuadFilled(pTop, pRight, pBottom, pLeft, fillColor);
                drawList->AddQuad(pTop, pRight, pBottom, pLeft, borderColor, 1.5f);

                if (isHovered) {
                    ImGui::SetTooltip("Pista: %s\nTiempo: %.3f s\nValor: %.2f\n[Clic Der] Eliminar", trk.name.c_str(), k.time, k.value);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        m_SelectedTrackIdx = trkIdx;
                        m_SelectedKeyframeIdx = kIdx;
                        m_SeqCurrentTime = k.time;
                        m_ForcePreviewUpdate = true;
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        deleteTrackIdx = trkIdx;
                        deleteKeyIdx = kIdx;
                    }
                }
            }

            currentY += rowHeight;
        }

        // Eliminar keyframe si se hizo clic derecho
        if (deleteTrackIdx >= 0 && deleteKeyIdx >= 0 && deleteTrackIdx < (int)group.tracks.size()) {
            group.tracks[deleteTrackIdx].RemoveKeyframe(deleteKeyIdx);
            m_SelectedKeyframeIdx = -1;
            m_StatusMessage = "Keyframe eliminado";
            m_StatusMessageTimer = 2.0f;
        }

        // 3. AGUJA DE REPRODUCCIÓN (PLAYHEAD / SCRUBBER)
        float scrubberX = canvasPos.x + (m_SeqCurrentTime / totalDur) * timelineWidth;
        float bottomY = currentY;
        if (bottomY < canvasPos.y + canvasSize.y) bottomY = canvasPos.y + canvasSize.y;

        // Cabeza triangular de la aguja en la regla
        ImVec2 tri1(scrubberX - 6.0f, rulerMin.y + 2.0f);
        ImVec2 tri2(scrubberX + 6.0f, rulerMin.y + 2.0f);
        ImVec2 tri3(scrubberX, rulerMin.y + 14.0f);
        drawList->AddTriangleFilled(tri1, tri2, tri3, IM_COL32(245, 50, 50, 255));

        // Línea vertical que cruza todas las pistas
        drawList->AddLine(ImVec2(scrubberX, rulerMin.y + 14.0f), ImVec2(scrubberX, bottomY), IM_COL32(245, 50, 50, 240), 2.0f);
    }
    ImGui::EndChild();
}

void AnimationEditorPanel::UpdateSequencerPreview(float deltaTime) {
    if (m_StatusMessageTimer > 0.0f) {
        m_StatusMessageTimer -= deltaTime;
    }

    if (!m_SequencerClip || m_SequencerClip->duration <= 0.0f) return;

    bool timeAdvanced = false;
    if (m_SeqIsPlaying) {
        m_SeqCurrentTime += deltaTime * m_SeqPlaybackSpeed;
        if (m_SeqCurrentTime >= m_SequencerClip->duration) {
            if (m_SeqLoop) {
                m_SeqCurrentTime = std::fmod(m_SeqCurrentTime, m_SequencerClip->duration);
            } else {
                m_SeqCurrentTime = m_SequencerClip->duration;
                m_SeqIsPlaying = false;
            }
        }
        timeAdvanced = true;
    }

    bool timeChanged = (std::abs(m_SeqCurrentTime - m_SeqLastSampledTime) > 1e-4f);

    // Solo actualizamos la pose en la entidad cuando:
    // 1. Se está reproduciendo la animación (timeAdvanced)
    // 2. El usuario ha movido el tiempo/scrubber (timeChanged)
    // 3. Se ha solicitado una actualización forzada (m_ForcePreviewUpdate)
    // Esto es crucial: cuando la animación está en pausa, NO sobreescribimos
    // la entidad en cada frame. Así el usuario puede modificar las propiedades
    // en el Inspector o con gizmos, y luego presionar [*] Grabar Keyframes sin
    // que sus cambios se borren ni reviertan al frame anterior.
    if (timeAdvanced || timeChanged || m_ForcePreviewUpdate) {
        if (m_TargetEntity) {
            m_SequencerClip->SampleEntity(m_TargetEntity, m_SeqCurrentTime);
            if (auto smc = m_TargetEntity->GetComponent<SkinnedMeshComponent>()) {
                smc->MarkBoneMatricesDirty();
                smc->UpdateBoneMatrices();
            }
        } else if (m_ContextLevel) {
            m_SequencerClip->Sample(m_ContextLevel, m_SeqCurrentTime);
            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                if (ent) {
                    if (auto smc = ent->GetComponent<SkinnedMeshComponent>()) {
                        smc->MarkBoneMatricesDirty();
                        smc->UpdateBoneMatrices();
                    }
                }
            }
        }
        m_SeqLastSampledTime = m_SeqCurrentTime;
        m_ForcePreviewUpdate = false;
    }
}

// =========================================================================
// 2. PREVIEW ESQUELÉTICO 3D (FBX/GLTF)
// =========================================================================
void AnimationEditorPanel::RenderSkeletalPreview() {
    float timelineHeight = 130.0f;
    float contentHeight = ImGui::GetContentRegionAvail().y - timelineHeight;

    ImGui::BeginChild("##AnimTopArea", ImVec2(0.0f, contentHeight), false);
    {
        float sidePropertiesWidth = 240.0f;
        float vpWidth = ImGui::GetContentRegionAvail().x - sidePropertiesWidth - 8.0f;

        ImGui::BeginChild("##AnimViewportContainer", ImVec2(vpWidth, 0.0f), true);
        RenderViewport();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##AnimPropertiesContainer", ImVec2(0.0f, 0.0f), true);
        RenderClipProperties();
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::BeginChild("##AnimTimelineContainer", ImVec2(0.0f, 0.0f), true);
    RenderTimelineAndControls();
    ImGui::EndChild();
}

void AnimationEditorPanel::RenderViewport() {
    if (SlipRHI::GetActiveAPI() != GraphicsAPI::OpenGL) {
        ImGui::TextDisabled("Preview esqueletico 3D disponible en modo OpenGL (migracion Vulkan en Fase 4).");
        return;
    }
    ImVec2 viewSize = ImGui::GetContentRegionAvail();
    int newW = (std::max)(64, (int)viewSize.x);
    int newH = (std::max)(64, (int)viewSize.y);

    ResizeFBO(newW, newH);

    UpdateCamera();

    // Renderizar la escena 3D aislada en el FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
    glClearColor(0.14f, 0.14f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    DrawPreviewScene();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Mostrar textura del FBO en ImGui
    uint64_t texID = static_cast<uint64_t>(m_ColorTexture);
    ImGui::Image(reinterpret_cast<void*>(texID), ImVec2((float)m_ViewportWidth, (float)m_ViewportHeight), ImVec2(0, 1), ImVec2(1, 0));
}

void AnimationEditorPanel::UpdateCamera() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered()) {
        if (io.MouseWheel != 0.0f) {
            m_CameraDistance -= io.MouseWheel * 0.4f;
            if (m_CameraDistance < 0.5f) m_CameraDistance = 0.5f;
            if (m_CameraDistance > 30.0f) m_CameraDistance = 30.0f;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            m_CameraYaw += io.MouseDelta.x * 0.4f;
            m_CameraPitch -= io.MouseDelta.y * 0.4f;
            m_CameraPitch = glm::clamp(m_CameraPitch, -85.0f, 85.0f);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
            m_CameraTarget.y += io.MouseDelta.y * 0.005f * m_CameraDistance;
            glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), glm::vec3(cos(glm::radians(m_CameraYaw)), 0, sin(glm::radians(m_CameraYaw)))));
            m_CameraTarget += right * io.MouseDelta.x * 0.005f * m_CameraDistance;
        }
    }
}

void AnimationEditorPanel::DrawPreviewScene() {
    glEnable(GL_DEPTH_TEST);

    glm::vec3 camPos;
    camPos.x = m_CameraTarget.x + m_CameraDistance * cos(glm::radians(m_CameraPitch)) * cos(glm::radians(m_CameraYaw));
    camPos.y = m_CameraTarget.y + m_CameraDistance * sin(glm::radians(m_CameraPitch));
    camPos.z = m_CameraTarget.z + m_CameraDistance * cos(glm::radians(m_CameraPitch)) * sin(glm::radians(m_CameraYaw));

    glm::mat4 view = glm::lookAt(camPos, m_CameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)m_ViewportWidth / (float)m_ViewportHeight, 0.1f, 100.0f);

    std::shared_ptr<SlipSkeleton> skel = nullptr;
    if (m_SkinnedMeshComp && m_SkinnedMeshComp->skeleton) {
        skel = m_SkinnedMeshComp->skeleton;
    } else if (m_PreviewSkeleton) {
        skel = m_PreviewSkeleton;
    }

    AnimationClip* activeClip = nullptr;
    if (m_AnimatorComp && m_AnimatorComp->GetCurrentClip()) {
        activeClip = m_AnimatorComp->GetCurrentClip();
        if (m_IsPlaying) {
            m_AnimatorComp->OnUpdate(SlipGlobals::Get().GetDeltaTime() * m_PlaybackSpeed);
            m_CurrentTime = m_AnimatorComp->currentTime;
        } else {
            m_AnimatorComp->SetTime(m_CurrentTime);
        }
    } else if (!m_PreviewClips.empty() && m_SelectedClipIdx < (int)m_PreviewClips.size()) {
        activeClip = m_PreviewClips[m_SelectedClipIdx].get();
        if (m_IsPlaying && activeClip && activeClip->duration > 0.0f) {
            m_CurrentTime += SlipGlobals::Get().GetDeltaTime() * m_PlaybackSpeed;
            if (m_CurrentTime >= activeClip->duration) {
                m_CurrentTime = m_IsLooping ? std::fmod(m_CurrentTime, activeClip->duration) : activeClip->duration;
            }
        }
    }

    std::vector<glm::mat4> animBones;
    if (skel && activeClip) {
        skel->ComputeTransforms(activeClip, m_CurrentTime, animBones);
        if (m_SkinnedMeshComp) {
            m_SkinnedMeshComp->boneTransforms = animBones;
        }
    }

    if (m_ShowGrid) {
        auto* unlit = SlipShaderLibrary::Get("Unlit");
        if (unlit) {
            unlit->use();
            unlit->setMat4("u_Model", glm::mat4(1.0f));
            unlit->setMat4("u_View", view);
            unlit->setMat4("u_Projection", proj);
            unlit->setBool("u_HasTexture", false);

            std::vector<float> gridVerts;
            float gSize = 8.0f;
            float gStep = 1.0f;
            for (float x = -gSize; x <= gSize; x += gStep) {
                gridVerts.push_back(x); gridVerts.push_back(0.0f); gridVerts.push_back(-gSize);
                gridVerts.push_back(x); gridVerts.push_back(0.0f); gridVerts.push_back(gSize);
            }
            for (float z = -gSize; z <= gSize; z += gStep) {
                gridVerts.push_back(-gSize); gridVerts.push_back(0.0f); gridVerts.push_back(z);
                gridVerts.push_back(gSize); gridVerts.push_back(0.0f); gridVerts.push_back(z);
            }

            GLuint vao = 0, vbo = 0;
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

            unlit->setVec3("u_Color", glm::vec3(0.28f, 0.28f, 0.32f));
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gridVerts.size() / 3));

            glBindVertexArray(0);
            glDeleteBuffers(1, &vbo);
            glDeleteVertexArrays(1, &vao);
        }
    }

    SlipMesh* meshToDraw = nullptr;
    glm::mat4 modelMat = glm::mat4(1.0f);

    if (m_SkinnedMeshComp && m_SkinnedMeshComp->model) {
        meshToDraw = m_SkinnedMeshComp->model;
    } else if (m_PreviewMesh) {
        meshToDraw = m_PreviewMesh.get();
    }

    if (meshToDraw) {
        float effectiveScale = 1.0f;
        if (m_TargetEntity && m_TargetEntity->scale.x > 0.0f && std::abs(m_TargetEntity->scale.x - 1.0f) > 1e-4f) {
            effectiveScale = m_TargetEntity->scale.x;
        } else {
            float maxDim = (std::max)({ meshToDraw->aabbMax.x - meshToDraw->aabbMin.x,
                                        meshToDraw->aabbMax.y - meshToDraw->aabbMin.y,
                                        meshToDraw->aabbMax.z - meshToDraw->aabbMin.z });
            if (maxDim > 40.0f) {
                effectiveScale = 0.01f;
            }
        }
        modelMat = glm::scale(glm::mat4(1.0f), glm::vec3(effectiveScale));

        auto* litShader = SlipShaderLibrary::Get("Lit");
        if (litShader) {
            litShader->use();
            litShader->setMat4("u_Model", modelMat);
            litShader->setMat4("u_View", view);
            litShader->setMat4("u_Projection", proj);
            litShader->setVec3("u_CameraPos", camPos);
            litShader->setBool("u_ShadowsEnabled", false);
            litShader->setBool("u_DDGIEnabled", false);
            litShader->setBool("u_HasEnvCube", false);
            litShader->setInt("u_NumDirLights", 1);
            litShader->setVec3("u_DirLights[0].direction", -0.5f, -1.0f, -0.5f);
            litShader->setVec3("u_DirLights[0].color", 1.0f, 1.0f, 1.0f);
            litShader->setFloat("u_DirLights[0].intensity", 2.0f);
            litShader->setInt("u_NumPointLights", 0);
            litShader->setInt("u_NumSpotLights", 0);

            bool hasBones = !animBones.empty();
            litShader->setBool("u_HasBones", hasBones);
            litShader->setInt("u_SkinningQuality", SlipGraphicsSettings::Get().GetData().skinningQuality);
            if (hasBones) {
                litShader->setMat4Array("u_BoneTransforms", animBones.data(), static_cast<int>(animBones.size()));
            }
        }
        meshToDraw->draw(modelMat, nullptr, true);
    }
}

void AnimationEditorPanel::RenderClipProperties() {
    ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Propiedades de Animacion");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Cargar Modelo (FBX/GLTF)...", ImVec2(-1, 26))) {
        std::string assetsDir = SlipFileSystem::GetProjectAssetsPath();
        if (assetsDir.empty()) assetsDir = "assets";
        auto files = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".fbx");
        if (files.empty()) files = SlipFileSystem::ListDirectoryRecursive(assetsDir, ".gltf");
        if (!files.empty()) {
            LoadModelForPreview(files[0].fullPath);
        }
    }

    if (!m_LoadedModelPath.empty()) {
        ImGui::TextWrapped("Modelo: %s", SlipFileSystem::GetFileName(m_LoadedModelPath).c_str());
    } else if (m_TargetEntity) {
        ImGui::Text("Entidad: %s", m_TargetEntity->GetName().c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<std::string> clipNames;
    if (m_AnimatorComp) {
        for (const auto& c : m_AnimatorComp->clips) {
            if (c) clipNames.push_back(c->name);
        }
    } else {
        for (const auto& c : m_PreviewClips) {
            if (c) clipNames.push_back(c->name);
        }
    }

    if (clipNames.empty()) {
        ImGui::TextDisabled("No hay clips de animacion disponibles.");
        return;
    }

    if (m_SelectedClipIdx >= (int)clipNames.size()) m_SelectedClipIdx = 0;

    if (ImGui::BeginCombo("Clips", clipNames[m_SelectedClipIdx].c_str())) {
        for (int i = 0; i < (int)clipNames.size(); ++i) {
            bool isSel = (i == m_SelectedClipIdx);
            ImGui::PushID(i);
            std::string label = clipNames[i] + "##clip_" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), isSel)) {
                m_SelectedClipIdx = i;
                m_CurrentTime = 0.0f;
                if (m_AnimatorComp) m_AnimatorComp->Play(i, m_PlaybackSpeed, m_IsLooping);
            }
            if (isSel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    AnimationClip* activeClip = nullptr;
    if (m_AnimatorComp && m_SelectedClipIdx < (int)m_AnimatorComp->clips.size()) {
        activeClip = m_AnimatorComp->clips[m_SelectedClipIdx].get();
    } else if (m_SelectedClipIdx < (int)m_PreviewClips.size()) {
        activeClip = m_PreviewClips[m_SelectedClipIdx].get();
    }

    if (activeClip) {
        ImGui::Spacing();
        ImGui::Text("Duracion: %.2f s (%d frames)", activeClip->duration, activeClip->GetTotalFrames());
        ImGui::Text("Canales / Huesos: %d", (int)activeClip->tracks.size());
        ImGui::Text("FPS Muestreo: %.1f", activeClip->ticksPerSecond);

        ImGui::Spacing();
        char nameBuf[128] = { 0 };
        std::strncpy(nameBuf, activeClip->name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Nombre Clip", nameBuf, sizeof(nameBuf))) {
            activeClip->name = nameBuf;
        }

        ImGui::InputInt("Fotograma Inicio", &activeClip->startFrame);
        ImGui::InputInt("Fotograma Fin", &activeClip->endFrame);

        ImGui::Spacing();
        if (ImGui::Button("Guardar Clip (.anim)", ImVec2(-1, 26))) {
            std::string modelPathToUse = m_LoadedModelPath;
            if (modelPathToUse.empty() && m_SkinnedMeshComp) modelPathToUse = m_SkinnedMeshComp->modelPath;
            std::string modelDir = SlipFileSystem::GetParentPath(modelPathToUse);
            if (modelDir.empty()) modelDir = SlipFileSystem::GetProjectAssetsPath();
            std::string animDir = SlipFileSystem::CombinePath(modelDir, "Animations");
            SlipFileSystem::CreateDirectoryPath(animDir);
            std::string safeName = SlipModelExtract::SanitizeClipFileName(activeClip->name);
            std::string animFilePath = SlipFileSystem::CombinePath(animDir, safeName + ".anim");

            AnimationClipAsset assetData;
            assetData.name = activeClip->name;
            assetData.sourceModel = modelPathToUse;
            assetData.clipIndex = m_SelectedClipIdx;
            assetData.duration = activeClip->duration;
            assetData.ticksPerSecond = activeClip->ticksPerSecond;
            assetData.loop = m_IsLooping;
            assetData.playbackSpeed = m_PlaybackSpeed;
            assetData.startTime = (activeClip->startFrame >= 0) ? (float)activeClip->startFrame / activeClip->ticksPerSecond : 0.0f;
            assetData.endTime = (activeClip->endFrame > 0) ? (float)activeClip->endFrame / activeClip->ticksPerSecond : activeClip->duration;

            if (SlipModelExtract::SaveAnimAsset(animFilePath, assetData)) {
                SlipConsole::LogSuccess("Clip de animacion guardado: " + animFilePath);
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Checkbox("Mostrar Cuadricula (Grid)", &m_ShowGrid);
    ImGui::Checkbox("Visualizar Huesos (Debug)", &m_ShowBones);
}

void AnimationEditorPanel::RenderTimelineAndControls() {
    float totalDur = 1.0f;
    int totalFrames = 30;
    if (m_AnimatorComp && m_AnimatorComp->GetCurrentClip()) {
        totalDur = m_AnimatorComp->GetCurrentDuration();
        totalFrames = m_AnimatorComp->GetCurrentClip()->GetTotalFrames();
    } else if (!m_PreviewClips.empty() && m_SelectedClipIdx < (int)m_PreviewClips.size()) {
        totalDur = m_PreviewClips[m_SelectedClipIdx]->duration;
        totalFrames = m_PreviewClips[m_SelectedClipIdx]->GetTotalFrames();
    }
    if (totalDur <= 0.0f) totalDur = 1.0f;

    int currentFrame = (int)((m_CurrentTime / totalDur) * (float)totalFrames);

    if (ImGui::Button(" |< ")) {
        m_CurrentTime = 0.0f;
        if (m_AnimatorComp) m_AnimatorComp->SetTime(0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button(" < ")) {
        float step = 1.0f / 30.0f;
        m_CurrentTime = (std::max)(0.0f, m_CurrentTime - step);
        if (m_AnimatorComp) m_AnimatorComp->SetTime(m_CurrentTime);
    }
    ImGui::SameLine();
    const char* playPauseIcon = m_IsPlaying ? " || " : " > ";
    if (ImGui::Button(playPauseIcon, ImVec2(40, 0))) {
        m_IsPlaying = !m_IsPlaying;
        if (m_AnimatorComp) {
            if (m_IsPlaying) m_AnimatorComp->Resume();
            else m_AnimatorComp->Pause();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(" > ")) {
        float step = 1.0f / 30.0f;
        m_CurrentTime = (std::min)(totalDur, m_CurrentTime + step);
        if (m_AnimatorComp) m_AnimatorComp->SetTime(m_CurrentTime);
    }
    ImGui::SameLine();
    if (ImGui::Button(" >| ")) {
        m_CurrentTime = totalDur;
        if (m_AnimatorComp) m_AnimatorComp->SetTime(totalDur);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_IsLooping);
    if (m_AnimatorComp) m_AnimatorComp->isLooping = m_IsLooping;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("Velocidad", &m_PlaybackSpeed, 0.1f, 3.0f, "%.2fx")) {
        if (m_AnimatorComp) m_AnimatorComp->playbackSpeed = m_PlaybackSpeed;
    }

    ImGui::SameLine();
    ImGui::Text("  Tiempo: %.2fs / %.2fs  (Frame %d / %d)", m_CurrentTime, totalDur, currentFrame, totalFrames);

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##AnimScrubber", &m_CurrentTime, 0.0f, totalDur, "%.3f s")) {
        if (m_AnimatorComp) {
            m_AnimatorComp->SetTime(m_CurrentTime);
        }
    }
}
