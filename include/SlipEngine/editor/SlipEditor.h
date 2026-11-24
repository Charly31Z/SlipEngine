#pragma once

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <string>

class SlipWindow;
struct SDL_Window;
typedef void* SDL_GLContext;
union SDL_Event;

enum class EditorPlayState {
    Edit = 0,
    Play,
    Pause
};

struct TouchGestureState {
    int fingerCount = 0;
    float panDeltaX = 0.0f;
    float panDeltaY = 0.0f;
    float pinchDelta = 0.0f;
};

struct TouchFingerState {
    int64_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float prevX = 0.0f;
    float prevY = 0.0f;
};

class SlipEditor {
public:
    static SlipEditor* GetInstance() { return s_Instance; }

    SlipEditor();
    ~SlipEditor();

    bool Init(SlipWindow& window);
    bool Init(SDL_Window* window, SDL_GLContext glContext, GraphicsAPI api = GraphicsAPI::OpenGL);
    void ProcessEvent(const SDL_Event& event);
    void BeginFrame();
    void RenderPanels();
    void EndFrame();
    void Shutdown();

    const TouchGestureState& GetTouchGesture() const { return m_TouchGesture; }
    void ResetTouchGesture() { m_TouchGesture.panDeltaX = 0.0f; m_TouchGesture.panDeltaY = 0.0f; m_TouchGesture.pinchDelta = 0.0f; }
    const std::vector<TouchFingerState>& GetTouchFingers() const { return m_TouchFingers; }

    bool IsAndroidGameView() const;
    void SetAndroidGameView(bool gameView);

    void SetContextLevel(SlipLevel* level);
    SlipLevel* GetContextLevel() const { return m_ActiveLevel; }
    SDL_Window* GetWindow() const { return m_Window; }

    // Multi-selección: el editor guarda el conjunto canónico y lo propaga a todos los paneles
    void NotifySelectionChanged(const std::vector<SlipEntity*>& selection, SlipEntity* primary);
    const std::vector<SlipEntity*>& GetSelection() const { return m_Selection; }

    // Portapapeles de entidades central (Ctrl+D / Ctrl+C / Ctrl+X / Ctrl+V / Supr)
    void DuplicateSelection();
    void CopySelection();
    void CutSelection();
    void PasteFromClipboard(SlipEntity* pasteParent = nullptr);
    void DeleteSelection();
    bool HasEntityClipboard() const { return !m_ClipboardDataList.empty(); }

    // Enfocar (dar focus ImGui) a una ventana/panel por nombre en el siguiente frame
    void RequestPanelFocus(const std::string& windowName) { m_PendingFocusWindow = windowName; }

    void SaveCurrentScene();
    void LoadScene(const std::string& scenePath);
    void NotifyProjectOpened();
    void CloseCurrentProject();
    bool OpenProject(const std::string& projectFilePath);
    bool CreateAndOpenProject(const std::string& parentDir, const std::string& projName);

    // Indicador y carga diferida visual para proyectos y escenas
    void RequestOpenProject(const std::string& projectFilePath);
    void RequestCreateAndOpenProject(const std::string& parentDir, const std::string& projName);
    void RequestLoadScene(const std::string& scenePath);
    bool IsLoading() const { return m_IsLoading; }
    void OpenScript(const std::string& scriptPath, int line = 1);
    void OpenPrefab(const std::string& prefabPath);
    void OpenProjectSettings();
    void OpenAnimatorController(const std::string& controllerPath);
    void OpenShaderGraph(const std::string& graphPath);
    void OpenPixelArtEditor();

    // Play Mode Controls
    void StartPlayMode();
    void PausePlayMode();
    void ResumePlayMode();
    void StopPlayMode();
    EditorPlayState GetPlayState() const { return m_PlayState; }
    bool IsPlaying() const { return m_PlayState == EditorPlayState::Play; }
    bool IsPaused() const { return m_PlayState == EditorPlayState::Pause; }
    bool IsInPlaySession() const { return m_PlayState != EditorPlayState::Edit; }

    void OpenProjectHub() { m_ShowProjectHub = true; }
    void CloseProjectHub() { m_ShowProjectHub = false; }
    bool IsProjectHubOpen() const { return m_ShowProjectHub; }

    void ResetDefaultLayout();

    template<typename T>
    T* GetPanel() const {
        for (const auto& p : m_Panels) {
            if (auto target = dynamic_cast<T*>(p.get())) return target;
        }
        return nullptr;
    }
    class ViewportPanel* GetViewportPanel() const;

private:
    void RenderDockSpace();
    void RenderToolbar();
    void RenderProjectHub();
    void RenderLoadingOverlay();
    void ProcessPendingLoading();

    bool ExecuteOpenProject(const std::string& projectFilePath);
    bool ExecuteCreateAndOpenProject(const std::string& parentDir, const std::string& projName);
    void ExecuteLoadScene(const std::string& scenePath);

    bool m_IsLoading = false;
    bool m_IsLoadingExecution = false;
    std::string m_LoadingMessage = "";
    std::string m_PendingOpenProjectPath = "";
    std::string m_PendingCreateParentDir = "";
    std::string m_PendingCreateProjName = "";
    std::string m_PendingLoadScenePath = "";
    int m_LoadingFrameCounter = 0;

    SDL_Window* m_Window = nullptr;
    SDL_GLContext m_GLContext = nullptr;
    GraphicsAPI m_GraphicsAPI = GraphicsAPI::OpenGL;
    std::vector<std::unique_ptr<EditorPanel>> m_Panels;
    SlipEntity* m_SelectedEntity = nullptr;
    std::vector<SlipEntity*> m_Selection;
    std::vector<std::string> m_ClipboardDataList;
    std::vector<SlipEntity*> m_CutEntities;
    std::string m_PendingFocusWindow;
    SlipLevel*  m_ActiveLevel = nullptr;
    bool m_Initialized = false;

    EditorPlayState m_PlayState = EditorPlayState::Edit;
    std::string m_PlaySnapshotPath = "";

    bool m_ShowProjectHub = true;
    char m_NewProjectNameBuf[128] = "NuevoProyecto";
    char m_NewProjectPathBuf[512] = "";

    TouchGestureState m_TouchGesture;
    std::vector<TouchFingerState> m_TouchFingers;
    bool m_AndroidGameView = false;
    bool m_NeedsResetLayout = false;

    inline static SlipEditor* s_Instance = nullptr;
};
