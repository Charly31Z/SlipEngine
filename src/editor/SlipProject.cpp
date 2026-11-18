#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include "SlipGlobals.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

SlipProject& SlipProject::Get() {
    static SlipProject s_Instance;
    return s_Instance;
}

SlipProject::SlipProject() {
    m_CurrentProject.isLoaded = false;
}

std::string SlipProject::GetRecentProjectsFilePath() {
    std::string internalPath = SlipFileSystem::GetInternalStoragePath();
    SlipFileSystem::CreateDirectoryPath(internalPath);
    return SlipFileSystem::CombinePath(internalPath, "recent_projects.txt");
}

std::string SlipProject::GetDefaultScenePath() const {
    if (!m_CurrentProject.isLoaded) return "";
    return SlipFileSystem::CombinePath(m_CurrentProject.scenesPath, "Main.scene");
}

static bool IsPathAbsolute(const std::string& p) {
    if (p.size() >= 2 && p[1] == ':') return true;
    if (!p.empty() && (p[0] == '/' || p[0] == '\\')) return true;
    return false;
}

std::string SlipProject::GetGameStartScenePath() const {
    if (!m_CurrentProject.isLoaded) return "";
    if (!m_CurrentProject.gameStartScene.empty()) {
        if (IsPathAbsolute(m_CurrentProject.gameStartScene)) {
            return m_CurrentProject.gameStartScene;
        }
        return SlipFileSystem::CombinePath(m_CurrentProject.rootPath, m_CurrentProject.gameStartScene);
    }
    return GetDefaultScenePath();
}

std::string SlipProject::GetEditorStartScenePath() const {
    if (!m_CurrentProject.isLoaded) return "";
    if (m_CurrentProject.reopenLastEditedScene && !m_CurrentProject.currentScenePath.empty() && SlipFileSystem::Exists(m_CurrentProject.currentScenePath)) {
        return m_CurrentProject.currentScenePath;
    }
    if (!m_CurrentProject.editorStartScene.empty()) {
        if (IsPathAbsolute(m_CurrentProject.editorStartScene)) {
            return m_CurrentProject.editorStartScene;
        }
        return SlipFileSystem::CombinePath(m_CurrentProject.rootPath, m_CurrentProject.editorStartScene);
    }
    return GetDefaultScenePath();
}

void SlipProject::EnsureProjectStructure(const std::string& rootPath, const std::string& projectName) {
    m_CurrentProject.name = projectName;
    m_CurrentProject.rootPath = SlipFileSystem::NormalizePath(rootPath);
    m_CurrentProject.assetsPath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, "assets");
    m_CurrentProject.scenesPath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, "scenes");
    m_CurrentProject.configPath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, "config");
    m_CurrentProject.projectFilePath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, projectName + ".slipproj");
    m_CurrentProject.currentScenePath = SlipFileSystem::CombinePath(m_CurrentProject.scenesPath, "Main.scene");
    m_CurrentProject.gameStartScene = "scenes/Main.scene";
    m_CurrentProject.editorStartScene = "scenes/Main.scene";
    m_CurrentProject.reopenLastEditedScene = true;
    m_CurrentProject.isLoaded = true;

    // Crear las carpetas estándar de estilo Unity/UE5
    SlipFileSystem::CreateDirectoryPath(m_CurrentProject.assetsPath);
    SlipFileSystem::CreateDirectoryPath(m_CurrentProject.scenesPath);
    SlipFileSystem::CreateDirectoryPath(m_CurrentProject.configPath);

    SlipFileSystem::SetProjectPaths(m_CurrentProject.rootPath, m_CurrentProject.assetsPath);
}

void SlipProject::InitDefaultProject() {
    std::string defaultPath = SlipFileSystem::GetDefaultWorkingPath();
    std::string projName = SlipFileSystem::GetFileName(defaultPath);
    if (projName.empty()) projName = "SlipProject";

    m_CurrentProject.name = projName;
    m_CurrentProject.rootPath = SlipFileSystem::NormalizePath(defaultPath);
    m_CurrentProject.assetsPath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, "assets");
    m_CurrentProject.scenesPath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, "scenes");
    m_CurrentProject.configPath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, "config");
    m_CurrentProject.projectFilePath = SlipFileSystem::CombinePath(m_CurrentProject.rootPath, projName + ".slipproj");
    m_CurrentProject.isLoaded = false;
}

bool SlipProject::CreateNewProject(const std::string& parentDirectory, const std::string& projectName) {
    if (parentDirectory.empty() || projectName.empty()) return false;

    std::string projectDir = SlipFileSystem::CombinePath(parentDirectory, projectName);
    SlipFileSystem::CreateDirectoryPath(projectDir);

    EnsureProjectStructure(projectDir, projectName);

    // Escribir archivo de configuración del proyecto .slipproj
    std::string projFileContent = "name: " + projectName + "\n"
                                  "version: 1.0\n"
                                  "engine_version: 0.1.0\n"
                                  "default_scene: scenes/Main.scene\n";

    SlipFileSystem::WriteAllText(m_CurrentProject.projectFilePath, projFileContent);

    // Crear una escena básica de ejemplo
    std::string defaultScenePath = SlipFileSystem::CombinePath(m_CurrentProject.scenesPath, "Main.scene");
    m_CurrentProject.currentScenePath = defaultScenePath;
    if (!SlipFileSystem::Exists(defaultScenePath)) {
        SlipFileSystem::WriteAllText(defaultScenePath, "# SlipEngine Scene File\nVersion: 1\nEntitiesCount: 0\n");
    }

    AddRecentProject(m_CurrentProject.projectFilePath);

    // Escanear assets iniciales y generar metas si existen
    SlipModelExtract::ScanAndGenerateMetas(m_CurrentProject.assetsPath);

    return true;
}

bool SlipProject::OpenProject(const std::string& path) {
    if (path.empty() || !SlipFileSystem::Exists(path)) return false;

    std::string projFile = path;
    std::string rootDir;
    std::string projName;

    if (SlipFileSystem::IsDirectory(path)) {
        rootDir = path;
        // Buscar algún archivo .slipproj dentro
        auto files = SlipFileSystem::ListDirectory(path, ".slipproj");
        if (!files.empty()) {
            projFile = files[0].fullPath;
            projName = files[0].name.substr(0, files[0].name.find_last_of('.'));
        } else {
            projName = SlipFileSystem::GetFileName(path);
            projFile = SlipFileSystem::CombinePath(path, projName + ".slipproj");
        }
    } else {
        // Es un archivo .slipproj
        rootDir = SlipFileSystem::GetParentPath(path);
        projName = SlipFileSystem::GetFileName(path);
        size_t dot = projName.find_last_of('.');
        if (dot != std::string::npos) {
            projName = projName.substr(0, dot);
        }
    }

    EnsureProjectStructure(rootDir, projName);
    SlipFileSystem::SetProjectPaths(m_CurrentProject.rootPath, m_CurrentProject.assetsPath);

    std::string projText;
    if (SlipFileSystem::ReadAllText(m_CurrentProject.projectFilePath, projText)) {
        std::stringstream ss(projText);
        std::string line;
        while (std::getline(ss, line)) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            key.erase(0, key.find_first_not_of(" \t\r\n")); key.erase(key.find_last_not_of(" \t\r\n") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n")); val.erase(val.find_last_not_of(" \t\r\n") + 1);

            if (key == "game_start_scene") m_CurrentProject.gameStartScene = val;
            else if (key == "editor_start_scene") m_CurrentProject.editorStartScene = val;
            else if (key == "reopen_last_scene") m_CurrentProject.reopenLastEditedScene = (val == "true" || val == "1");
            else if (key == "last_opened_scene" && !val.empty()) m_CurrentProject.currentScenePath = val;
            else if (key == "gravity") {
                std::stringstream vss(val); float gx, gy, gz;
                if (vss >> gx >> gy >> gz) {
                    m_CurrentProject.gravity = glm::vec3(gx, gy, gz);
                    SlipPhysics::Get().setGravity(m_CurrentProject.gravity);
                }
            } else if (key == "fixed_timestep") {
                try {
                    m_CurrentProject.fixedTimestep = std::stof(val);
                    SlipGlobals::Get().SetFixedDeltaTime(m_CurrentProject.fixedTimestep);
                } catch (...) {}
            } else if (key == "solver_iterations") {
                try { m_CurrentProject.solverIterations = std::stoi(val); } catch (...) {}
            }
        }
    }

    // Cargar GraphicsSettings.ini e InputSettings.ini si existen en config/
    std::string configDir = m_CurrentProject.configPath;
    if (!configDir.empty()) {
        std::string gfxPath = SlipFileSystem::CombinePath(configDir, "GraphicsSettings.ini");
        if (SlipFileSystem::Exists(gfxPath)) {
            SlipGraphicsSettings::Get().LoadFromFile(gfxPath);
        }
        std::string inPath = SlipFileSystem::CombinePath(configDir, "InputSettings.ini");
        if (SlipFileSystem::Exists(inPath)) {
            SlipInputMap::Get().LoadFromFile(inPath);
        }
    }

    std::string startScene = GetEditorStartScenePath();
    if (SlipFileSystem::Exists(startScene)) {
        m_CurrentProject.currentScenePath = startScene;
    } else {
        std::string defaultScene = SlipFileSystem::CombinePath(m_CurrentProject.scenesPath, "Main.scene");
        if (SlipFileSystem::Exists(defaultScene)) {
            m_CurrentProject.currentScenePath = defaultScene;
        } else {
            auto scenes = SlipFileSystem::ListDirectory(m_CurrentProject.scenesPath, ".scene");
            if (!scenes.empty()) {
                m_CurrentProject.currentScenePath = scenes[0].fullPath;
            } else {
                m_CurrentProject.currentScenePath = defaultScene;
            }
        }
    }

    // Si no existe el archivo .slipproj, generarlo
    if (!SlipFileSystem::Exists(m_CurrentProject.projectFilePath)) {
        SaveCurrentProject();
    }

    AddRecentProject(m_CurrentProject.projectFilePath);

    // Escanear la carpeta de assets y generar los archivos .meta para cualquier archivo crudo nuevo
    SlipModelExtract::ScanAndGenerateMetas(m_CurrentProject.assetsPath);

    return true;
}

bool SlipProject::SaveCurrentProject() {
    if (!m_CurrentProject.isLoaded) return false;

    std::string content = "name: " + m_CurrentProject.name + "\n"
                          "version: 1.0\n"
                          "engine_version: 0.1.0\n"
                          "assets_path: assets\n"
                          "scenes_path: scenes\n"
                          "game_start_scene: " + m_CurrentProject.gameStartScene + "\n"
                          "editor_start_scene: " + m_CurrentProject.editorStartScene + "\n"
                          "reopen_last_scene: " + (m_CurrentProject.reopenLastEditedScene ? "true" : "false") + "\n"
                          "last_opened_scene: " + m_CurrentProject.currentScenePath + "\n"
                          "gravity: " + std::to_string(m_CurrentProject.gravity.x) + " " +
                                        std::to_string(m_CurrentProject.gravity.y) + " " +
                                        std::to_string(m_CurrentProject.gravity.z) + "\n"
                          "fixed_timestep: " + std::to_string(m_CurrentProject.fixedTimestep) + "\n"
                          "solver_iterations: " + std::to_string(m_CurrentProject.solverIterations) + "\n";

    bool projOk = SlipFileSystem::WriteAllText(m_CurrentProject.projectFilePath, content);

    // Sincronizar automáticamente GraphicsSettings.ini e InputSettings.ini en config/
    std::string configDir = m_CurrentProject.configPath;
    if (!configDir.empty()) {
        std::string gfxPath = SlipFileSystem::CombinePath(configDir, "GraphicsSettings.ini");
        SlipGraphicsSettings::Get().SaveToFile(gfxPath);
        std::string inPath = SlipFileSystem::CombinePath(configDir, "InputSettings.ini");
        SlipInputMap::Get().SaveToFile(inPath);
    }

    return projOk;
}

std::vector<std::string> SlipProject::GetRecentProjects() {
    std::vector<std::string> results;
    std::string filePath = GetRecentProjectsFilePath();

    std::string content;
    if (SlipFileSystem::ReadAllText(filePath, content)) {
        std::stringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            // Trim
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            line.erase(0, line.find_first_not_of(" \t\r\n"));

            if (!line.empty() && SlipFileSystem::Exists(line)) {
                // Evitar duplicados
                if (std::find(results.begin(), results.end(), line) == results.end()) {
                    results.push_back(line);
                }
            }
        }
    }
    return results;
}

void SlipProject::AddRecentProject(const std::string& projectPath) {
    if (projectPath.empty()) return;

    std::string normPath = SlipFileSystem::NormalizePath(projectPath);
    std::vector<std::string> current = GetRecentProjects();

    // Mover al inicio si ya existe
    auto it = std::find(current.begin(), current.end(), normPath);
    if (it != current.end()) {
        current.erase(it);
    }
    current.insert(current.begin(), normPath);

    // Limitar a los 10 más recientes
    if (current.size() > 10) {
        current.resize(10);
    }

    std::string out;
    for (const auto& p : current) {
        out += p + "\n";
    }

    SlipFileSystem::WriteAllText(GetRecentProjectsFilePath(), out);
}

void SlipProject::RemoveRecentProject(const std::string& projectPath) {
    std::string normPath = SlipFileSystem::NormalizePath(projectPath);
    std::vector<std::string> current = GetRecentProjects();

    auto it = std::find(current.begin(), current.end(), normPath);
    if (it != current.end()) {
        current.erase(it);
        std::string out;
        for (const auto& p : current) {
            out += p + "\n";
        }
        SlipFileSystem::WriteAllText(GetRecentProjectsFilePath(), out);
    }
}

