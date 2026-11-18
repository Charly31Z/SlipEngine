#pragma once

#ifndef SLIP_ANIMATION_EDITOR_PANEL_H
#define SLIP_ANIMATION_EDITOR_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/render/SlipFrameBuffer.h"
#include "SlipEngine/render/SlipAnimation.h"
#include "SlipEngine/render/SlipSkeleton.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipKeyframeAnimation.h"
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class AnimationEditorPanel : public EditorPanel {
public:
    AnimationEditorPanel();
    ~AnimationEditorPanel() override;

    void OnImGuiRender() override;

    void SetTargetEntity(SlipEntity* entity);
    SlipEntity* GetTargetEntity() const { return m_TargetEntity; }
    void LoadModelForPreview(const std::string& modelPath);

    static AnimationEditorPanel* GetInstance() { return s_Instance; }

private:
    inline static AnimationEditorPanel* s_Instance = nullptr;

    // Modos del panel: 0 = Secuenciador Keyframes (Estilo Unity/UE5/Roblox), 1 = Preview Esquelético 3D
    int m_PanelTab = 0;

    // =========================================================
    // 1. SECUENCIADOR DE KEYFRAMES (Cinemáticas y Entidades)
    // =========================================================
    void RenderKeyframeSequencer();
    void RenderSequencerToolbar();
    void RenderSequencerTimeline();
    void RenderSequencerDopeSheet(float leftPaneWidth, float contentHeight);
    void UpdateSequencerPreview(float deltaTime);

    std::shared_ptr<KeyframeClip> m_SequencerClip;
    float m_SeqCurrentTime = 0.0f;
    bool m_SeqIsPlaying = false;
    bool m_SeqLoop = true;
    float m_SeqPlaybackSpeed = 1.0f;
    int m_SelectedTrackIdx = -1;
    int m_SelectedKeyframeIdx = -1;
    std::string m_ClipFileName = "Cinematica_Intro";
    std::string m_StatusMessage;
    float m_StatusMessageTimer = 0.0f;
    float m_SeqLastSampledTime = -1.0f;
    bool m_ForcePreviewUpdate = true;

    // =========================================================
    // 2. PREVIEW ESQUELÉTICO 3D (FBX/GLTF)
    // =========================================================
    void RenderSkeletalPreview();
    void RenderViewport();
    void RenderTimelineAndControls();
    void RenderClipProperties();
    void UpdateCamera();
    void DrawPreviewScene();
    void ResizeFBO(int width, int height);

    unsigned int m_FBO = 0;
    unsigned int m_ColorTexture = 0;
    unsigned int m_DepthRBO = 0;
    int m_ViewportWidth = 640;
    int m_ViewportHeight = 480;

    // Cámara de previsualización 3D (Orbital)
    glm::vec3 m_CameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);
    float m_CameraDistance = 3.5f;
    float m_CameraYaw = -90.0f;
    float m_CameraPitch = 15.0f;

    // Estado de reproducción y animación esquelética
    SlipEntity* m_TargetEntity = nullptr;
    SkinnedMeshComponent* m_SkinnedMeshComp = nullptr;
    AnimatorComponent* m_AnimatorComp = nullptr;

    bool m_ShowGrid = true;
    bool m_ShowBones = false;
    float m_PlaybackSpeed = 1.0f;
    bool m_IsPlaying = true;
    bool m_IsLooping = true;
    float m_CurrentTime = 0.0f;
    int m_SelectedClipIdx = 0;

    std::vector<std::shared_ptr<AnimationClip>> m_PreviewClips;
    std::shared_ptr<SlipSkeleton> m_PreviewSkeleton;
    std::unique_ptr<SlipMesh> m_PreviewMesh;
    std::string m_LoadedModelPath;
};

#endif // !SLIP_ANIMATION_EDITOR_PANEL_H
