#pragma once

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/scene/SlipBVH.h"
#include "SlipEngine/render/SlipRenderQueue.h"
#include "SlipEngine/render/SlipRender.h"
#include <functional>
#include <glm/glm.hpp>
#include <cstdint>

class SlipEntity;

enum class GizmoMode {
    None = 0,
    Translate,
    Rotate,
    Scale
};

enum class GizmoSpace {
    World = 0,
    Local = 1
};

enum class ViewportCameraType {
    EditorCamera = 0,
    GameCamera = 1
};

class CameraComponent;

class ViewportPanel : public EditorPanel {
public:
    using SelectionCallback = std::function<void(SlipEntity*)>;
    using MultiSelectionCallback = std::function<void(const std::vector<SlipEntity*>&, SlipEntity*)>;

    ViewportPanel(SelectionCallback onSelect = nullptr);
    ~ViewportPanel() override;

    void OnImGuiRender() override;

    void SetTextureID(uint32_t textureID) { m_TextureID = textureID; }
    uint32_t GetTextureID() const { return m_TextureID; }

    void SetMultiSelectionCallback(MultiSelectionCallback cb) { m_OnMultiSelectCallback = cb; }

    // Tecla F: enfoca la cámara del editor sobre la selección actual (transición suave)
    void FocusOnSelection();

    const glm::vec2& GetViewportSize() const { return m_ViewportSize; }
    bool IsFocused() const { return m_IsFocused; }
    bool IsHovered() const { return m_IsHovered; }

    GizmoMode GetGizmoMode() const { return m_GizmoMode; }
    void SetGizmoMode(GizmoMode mode) { m_GizmoMode = mode; }

    GizmoSpace GetGizmoSpace() const { return m_GizmoSpace; }
    void SetGizmoSpace(GizmoSpace space) { m_GizmoSpace = space; }

    ViewportCameraType GetActiveCameraType() const { return m_ActiveCameraType; }
    void SetActiveCameraType(ViewportCameraType type) { m_ActiveCameraType = type; }

    bool Is2DMode() const { return m_Is2DMode; }
    void Set2DMode(bool is2D) { m_Is2DMode = is2D; }

    bool IsSnapToGrid2D() const { return m_SnapToGrid2D; }
    void SetSnapToGrid2D(bool snap) { m_SnapToGrid2D = snap; }
    float GetGridSnapSize() const { return m_GridSnapSize; }
    void SetGridSnapSize(float s) { m_GridSnapSize = s; }

    void RequestReflectionBake() { m_ReflectionBakeNeeded = true; }
    void BakeReflectionProbe(class ReflectionProbeComponent* probe, const glm::vec3& probePos);
    void BakeAllReflectionProbes();

private:
    void InitRenderer();
    void ResizeFBO(int width, int height);
    void RenderScene();
    void RenderSprites(const glm::mat4& view, const glm::mat4& projection);
    void RenderCanvasUI(const glm::mat4& view, const glm::mat4& projection);
    void ProcessUIInteraction();
    void CleanUpRenderer();

    void DrawGizmo(const glm::mat4& view, const glm::mat4& projection);
    void BuildGizmoDragSnapshots();
    void BeginGizmoMouseCapture();
    void EndGizmoMouseCapture();
    void UpdateFocusAnimation(float dt);
    void DrawColliderWireframe(const glm::mat4& view, const glm::mat4& projection);
    void DrawCameraFrustum(const glm::mat4& view, const glm::mat4& projection, CameraComponent* cam);
    void DrawLightGizmo(const glm::mat4& view, const glm::mat4& projection, class LightComponent* light);
    void DrawVolumeGizmo(const glm::mat4& view, const glm::mat4& projection, class DDGIVolumeComponent* volume);
    void DrawReflectionProbeGizmo(const glm::mat4& view, const glm::mat4& projection, class ReflectionProbeComponent* probe);
    void DrawAabbGizmo(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& center, const glm::vec3& size, const glm::vec4& color);
    void DrawPostProcessGizmo(const glm::mat4& view, const glm::mat4& projection, class PostProcessVolumeComponent* volume);
    void DrawParticleGizmo(const glm::mat4& view, const glm::mat4& projection, class ParticleSystemComponent* ps);
    void DrawVClustBoundsGizmo(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const SlipFrustum& frustum);
    void SpawnStressTestInstances(int instanceCount = 1000);
    void ClearStressTestInstances();
    void DrawSSR(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const glm::vec3& mainLightDir);
    void DrawProceduralSky(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const glm::vec3& mainLightDir);
    void BakeReflectionCubemap(const glm::vec3& center, bool includeDynamic);
    bool ProjectToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj, glm::vec2& outScreenPos);
    SlipEntity* CastRayFromMouse(const glm::vec2& mouseScreenPos, const glm::mat4& view, const glm::mat4& projection);

    SelectionCallback m_OnSelectCallback;
    MultiSelectionCallback m_OnMultiSelectCallback;

    ViewportCameraType m_ActiveCameraType = ViewportCameraType::EditorCamera;

    uint32_t m_TextureID = 0;
    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
    glm::vec2 m_ViewportBounds[2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    uint32_t m_SavedDockId = 0;
    bool m_WasAndroidGameView = false;

    bool m_IsFocused = false;
    bool m_IsHovered = false;

    // Gizmo 3D Interactivo (0: X, 1: Y, 2: Z, 3: XY, 4: XZ, 5: YZ)
    GizmoMode m_GizmoMode = GizmoMode::Translate;
    GizmoSpace m_GizmoSpace = GizmoSpace::Local;
    int m_ActiveGizmoAxis = -1;
    int m_GizmoHoveredAxis = -1;
    glm::vec2 m_GizmoDragStartMouse = { 0.0f, 0.0f };
    glm::vec3 m_GizmoInitialPos = { 0.0f, 0.0f, 0.0f };
    glm::vec3 m_GizmoInitialRot = { 0.0f, 0.0f, 0.0f };
    glm::vec3 m_GizmoInitialScale = { 1.0f, 1.0f, 1.0f };
    glm::vec2 m_GizmoInitialAnchoredPos = { 0.0f, 0.0f };
    glm::vec2 m_GizmoInitialRectSize = { 100.0f, 100.0f };
    glm::vec2 m_GizmoDragOrigin2D = { 0.0f, 0.0f };
    glm::vec2 m_GizmoDragAxisDirX = { 1.0f, 0.0f };
    glm::vec2 m_GizmoDragAxisDirY = { 0.0f, -1.0f };
    glm::vec2 m_GizmoDragAxisDirZ = { 0.0f, 1.0f };
    float m_GizmoDragWorldPerPixelX = 0.01f;
    float m_GizmoDragWorldPerPixelY = 0.01f;
    float m_GizmoDragWorldPerPixelZ = 0.01f;
    float m_GizmoStartAngle = 0.0f;

    // Snapshot por entidad para arrastrar la multi-selección con el gizmo
    struct GizmoDragSnapshot {
        SlipEntity* entity = nullptr;
        glm::vec3 initialPos = glm::vec3(0.0f);
        glm::vec3 initialRot = glm::vec3(0.0f);
        glm::vec3 initialScale = glm::vec3(1.0f);
        glm::vec3 initialWorldPos = glm::vec3(0.0f);
        glm::vec2 initialAnchoredPos = glm::vec2(0.0f);
        glm::vec2 initialRectSize = glm::vec2(100.0f);
        bool hasRect = false;
    };
    std::vector<GizmoDragSnapshot> m_GizmoDragSnapshots;

    // Captura continua del mouse durante el arrastre del gizmo (SDL relative mode):
    // permite arrastrar más allá de los bordes de la pantalla sin cortar el movimiento
    bool m_GizmoMouseCaptured = false;

    // FBO integrado del Viewport
    uint32_t m_FBO = 0;
    uint32_t m_ColorTexture = 0;
    uint32_t m_RBO = 0;
    uint32_t m_DepthTexture = 0;
    uint32_t m_NormalTexture = 0;
    uint32_t m_ColorCopy = 0;
    uint32_t m_PresentFBO = 0;
    uint32_t m_PresentTexture = 0;
    uint32_t m_PresentDepthTexture = 0;
    uint32_t m_OutlineFBO = 0;
    uint32_t m_OutlineTexture = 0;
    uint32_t m_SSRVAO = 0;
    uint32_t m_SSRTarget = 0;
    uint32_t m_SSRFBO = 0;
    int m_SSRWidth = 0;
    int m_SSRHeight = 0;
    int m_FboWidth = 0;
    int m_FboHeight = 0;

    // Grilla 3D y shader de escena
    uint32_t m_GridVAO = 0;
    uint32_t m_GridVBO = 0;
    uint32_t m_GridVertexCount = 0;
    uint32_t m_ShaderProgram = 0;
    bool m_RendererInitialized = false;

    // Buffer dinámico para wireframes de colisión
    uint32_t m_LineVAO = 0;
    uint32_t m_LineVBO = 0;

    // Quad unitario para Sprites 2D y Sistema UI
    uint32_t m_QuadVAO = 0;
    uint32_t m_QuadVBO = 0;

    // Buffer dinámico para Sistemas de Partículas (Billboards y GL_POINTS Niagara)
    uint32_t m_ParticleVAO = 0;
    uint32_t m_ParticleVBO = 0;

    // Cielo Procedimental y Cubemap de Reflejos
    uint32_t m_SkyVAO = 0;
    uint32_t m_ReflectionCubemap = 0;
    uint32_t m_ReflectionFBO = 0;
    uint32_t m_ReflectionRBO = 0;
    bool m_ReflectionBakeNeeded = true;
    float m_ReflectionBakeTimer = 0.0f;

    // Parámetros de cámara del editor
    float m_CameraPitch = 25.0f;
    float m_CameraYaw = 45.0f;
    float m_CameraDistance = 6.0f;
    glm::vec3 m_CameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 m_CameraPos = glm::vec3(0.0f, 2.5f, 6.0f);
    bool m_CameraPosInitialized = false;

    // Animación de focus de cámara (tecla F)
    bool m_FocusActive = false;
    float m_FocusProgress = 0.0f;
    const float m_FocusDuration = 0.35f;
    glm::vec3 m_FocusStartPos = glm::vec3(0.0f);
    glm::vec3 m_FocusEndPos = glm::vec3(0.0f);
    glm::vec3 m_FocusStartTarget = glm::vec3(0.0f);
    glm::vec3 m_FocusEndTarget = glm::vec3(0.0f);
    float m_FocusStartPitch = 0.0f;
    float m_FocusEndPitch = 0.0f;
    float m_FocusStartYaw = 0.0f;
    float m_FocusEndYaw = 0.0f;
    float m_FocusStartDist = 0.0f;
    float m_FocusEndDist = 0.0f;
    bool m_ShowGrid = true;
    bool m_ShowGizmos = true;
    bool m_Is2DMode = false;
    bool m_SnapToGrid2D = false;
    float m_GridSnapSize = 1.0f;
    bool m_FrustumCulling2D = true;
    int m_FrustumCulled2DCount = 0;
    int m_Visible2DSpritesCount = 0;
    bool m_ShowStats = true;

    // Aceleración espacial y Cola de renderizado optimizada
    SlipSpatialScene m_SpatialScene;
    SlipRenderQueue m_RenderQueue;
    size_t m_LastEntityCount = 0;
    uint64_t m_LastStaticHash = 0;

    // Estadísticas de renderizado en tiempo real
    int m_StatDrawCalls = 0;
    int m_StatTotalSubmeshes = 0;
    int m_StatVisibleSubmeshes = 0;
    int m_StatFrustumCulled = 0;
    int m_StatOcclusionCulled = 0;

    // Sistema V-Clust (Voxel-Anchored Cluster LOD) - Preview y Prueba de Estrés
    bool m_VClustEnabled = true;
    int  m_VClustDebugMode = 0; // 0 = Material Real, 1 = Color por Clúster (Nanite), 2 = Mapa de Calor LOD (0..N)
    bool m_VClustShowBounds = false;
    float m_VClustSseThreshold = 1.0f;
    SlipVClustStats m_VClustFrameStats{};
    std::vector<SlipEntity*> m_StressTestEntities;
    SlipMesh* m_StressFallbackMesh = nullptr;
};
