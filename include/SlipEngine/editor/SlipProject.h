#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

struct ProjectInfo {
    std::string name;
    std::string rootPath;
    std::string assetsPath;
    std::string scenesPath;
    std::string configPath;
    std::string projectFilePath;
    std::string currentScenePath;
    std::string gameStartScene;       // Escena inicial para el juego standalone (relativa a scenes/ o absoluta)
    std::string editorStartScene;     // Escena inicial para el editor
    bool reopenLastEditedScene = true; // Si es true, el editor reabre la última escena editada
    glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    float fixedTimestep = 1.0f / 60.0f;
    int solverIterations = 10;
    bool isLoaded = false;
};

class SlipProject {
public:
    static SlipProject& Get();

    // Obtener información del proyecto actualmente activo
    ProjectInfo& GetCurrentProject() { return m_CurrentProject; }
    const ProjectInfo& GetCurrentProject() const { return m_CurrentProject; }
    bool HasActiveProject() const { return m_CurrentProject.isLoaded; }

    // Ruta de la escena activa actualmente
    std::string GetCurrentScenePath() const { return m_CurrentProject.currentScenePath; }
    void SetCurrentScenePath(const std::string& path) { m_CurrentProject.currentScenePath = path; }
    std::string GetDefaultScenePath() const;

    // Escenas de inicio
    std::string GetGameStartScenePath() const;
    std::string GetEditorStartScenePath() const;
    void SetGameStartScenePath(const std::string& path) { m_CurrentProject.gameStartScene = path; }
    void SetEditorStartScenePath(const std::string& path) { m_CurrentProject.editorStartScene = path; }
    bool IsReopenLastEditedScene() const { return m_CurrentProject.reopenLastEditedScene; }
    void SetReopenLastEditedScene(bool val) { m_CurrentProject.reopenLastEditedScene = val; }

    // Crear un nuevo proyecto con estructura Unity/UE5 (assets/, scenes/, config/, etc.)
    bool CreateNewProject(const std::string& parentDirectory, const std::string& projectName);

    // Abrir un proyecto existente a partir de una carpeta o archivo .slipproj
    bool OpenProject(const std::string& path);

    // Guardar configuración del proyecto actual
    bool SaveCurrentProject();

    // Gestión de proyectos recientes
    std::vector<std::string> GetRecentProjects();
    void AddRecentProject(const std::string& projectPath);
    void RemoveRecentProject(const std::string& projectPath);

    // Inicializar proyecto por defecto al inicio
    void InitDefaultProject();

private:
    SlipProject();
    ~SlipProject() = default;

    std::string GetRecentProjectsFilePath();
    void EnsureProjectStructure(const std::string& rootPath, const std::string& projectName);

    ProjectInfo m_CurrentProject;
};

