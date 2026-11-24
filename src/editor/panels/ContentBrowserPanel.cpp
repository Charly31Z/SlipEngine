#include "SlipEngine/editor/panels/ContentBrowserPanel.h"
#include "SlipEngine/editor/panels/AnimationEditorPanel.h"
#include "SlipEngine/editor/SlipFileDialog.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipTexture.h"
#include "SlipEngine/render/SlipAnimatorController.h"
#include "SlipEngine/render/shadergraph/ShaderGraph.h"
#include "SlipEngine/render/shadergraph/ShaderNode.h"
#include "SlipEngine/editor/texteditor/SlipScriptIndexer.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/editor/SlipThumbnailManager.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace {
bool IsInsideDirectory(const std::string& path, const std::string& base) {
    if (path.empty() || base.empty()) return false;
    std::error_code ec;
    std::string absPath = fs::absolute(path, ec).string();
    std::string absBase = fs::absolute(base, ec).string();
    std::string np = SlipFileSystem::NormalizePath(absPath.empty() ? path : absPath);
    std::string nb = SlipFileSystem::NormalizePath(absBase.empty() ? base : absBase);
    if (np == nb) return true;
    if (np.size() > nb.size() && np.rfind(nb, 0) == 0) {
        char next = np[nb.size()];
        return next == '/' || next == '\\';
    }
    return false;
}
}

ContentBrowserPanel::ContentBrowserPanel(AssetSelectionCallback onAssetSelect)
    : m_OnAssetSelectCallback(onAssetSelect) {
    if (SlipProject::Get().HasActiveProject()) {
        m_BaseDirectory = SlipProject::Get().GetCurrentProject().assetsPath;
    } else {
        m_BaseDirectory = SlipFileSystem::GetDefaultWorkingPath();
    }
    m_CurrentDirectory = m_BaseDirectory;
    RefreshCurrentDirectory();
}

void ContentBrowserPanel::SetCurrentDirPath(const std::string& dir) {
    if (SlipProject::Get().HasActiveProject()) {
        std::string projAssets = SlipProject::Get().GetCurrentProject().assetsPath;
        if (!projAssets.empty()) {
            m_BaseDirectory = projAssets;
        }
    }
    if (!dir.empty() && SlipFileSystem::Exists(dir)) {
        if (m_BaseDirectory.empty() || IsInsideDirectory(dir, m_BaseDirectory)) {
            m_CurrentDirectory = dir;
            RefreshCurrentDirectory();
        }
    }
}

void ContentBrowserPanel::RefreshCurrentDirectory() {
    // Iniciar directamente dentro de assets/ estilo Unity
    if (SlipProject::Get().HasActiveProject()) {
        std::string projAssets = SlipProject::Get().GetCurrentProject().assetsPath;
        if (!projAssets.empty()) {
            m_BaseDirectory = projAssets;
            if (m_CurrentDirectory.empty() || !SlipFileSystem::Exists(m_CurrentDirectory) || !IsInsideDirectory(m_CurrentDirectory, m_BaseDirectory)) {
                m_CurrentDirectory = m_BaseDirectory;
            }
        }
    } else {
        if (m_CurrentDirectory.empty() || !SlipFileSystem::Exists(m_CurrentDirectory)) {
            m_CurrentDirectory = SlipFileSystem::GetDefaultWorkingPath();
            m_BaseDirectory = m_CurrentDirectory;
        }
    }

    auto rawEntries = SlipFileSystem::ListDirectory(m_CurrentDirectory);
    m_Entries.clear();

    // Ocultar archivos .meta del explorador visual (estilo Unity/Unreal)
    for (const auto& entry : rawEntries) {
        if (!entry.isDirectory && entry.extension == ".meta") {
            continue;
        }
        m_Entries.push_back(entry);
    }
}

void ContentBrowserPanel::OpenCreateModal(PendingCreationType type) {
    m_PendingCreation = type;
    std::string defaultName;
    switch (type) {
        case PendingCreationType::Folder: defaultName = "NuevaCarpeta"; break;
        case PendingCreationType::Material: defaultName = "NuevoMaterial"; break;
        case PendingCreationType::Script: defaultName = "NuevoScript"; break;
        case PendingCreationType::AnimatorController: defaultName = "NuevoController"; break;
        case PendingCreationType::ShaderGraph: defaultName = "NuevoShaderGraph"; break;
        default: defaultName = "NuevoElemento"; break;
    }
    std::strncpy(m_CreationNameBuffer, defaultName.c_str(), sizeof(m_CreationNameBuffer) - 1);
    m_CreationNameBuffer[sizeof(m_CreationNameBuffer) - 1] = '\0';
    m_ShowCreateModal = true;
}

void ContentBrowserPanel::ConfirmCreation() {
    std::string rawName = m_CreationNameBuffer;
    rawName.erase(0, rawName.find_first_not_of(" \t\r\n"));
    rawName.erase(rawName.find_last_not_of(" \t\r\n") + 1);
    if (rawName.empty()) return;

    for (char ch : rawName) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            SlipConsole::LogError("Nombre inválido: no puede contener caracteres como / \\ : * ? \" < > |");
            return;
        }
    }

    switch (m_PendingCreation) {
        case PendingCreationType::Folder: {
            std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, rawName);
            if (SlipFileSystem::Exists(path)) {
                SlipConsole::LogWarning("Ya existe una carpeta con el nombre '" + rawName + "'.");
                return;
            }
            SlipFileSystem::CreateDirectoryPath(path);
            RefreshCurrentDirectory();
            m_SelectedPath = path;
            SetSelectedAsset(path);
            break;
        }
        case PendingCreationType::Material: {
            std::string base = rawName;
            if (SlipFileSystem::GetFileExtension(base) != ".mat") {
                base += ".mat";
            }
            std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, base);
            if (SlipFileSystem::Exists(path)) {
                SlipConsole::LogWarning("Ya existe un material con el nombre '" + base + "'.");
                return;
            }
            SlipMaterial newMat;
            newMat.Save(path);
            SlipModelExtract::EnsureMetaForFile(path);
            SlipThumbnailManager::Get().GenerateMaterialThumbnail(path, true);
            RefreshCurrentDirectory();
            m_SelectedPath = path;
            SetSelectedAsset(path);
            if (m_OnAssetSelectCallback) m_OnAssetSelectCallback(path);
            break;
        }
        case PendingCreationType::Script: {
            std::string base = rawName;
            if (SlipFileSystem::GetFileExtension(base) == ".lua") {
                base = SlipFileSystem::GetFileNameWithoutExtension(base);
            }
            if (!SlipScriptIndexer::Get().IsScriptNameUnique(base)) {
                std::string existing = SlipScriptIndexer::Get().FindScriptPathByName(base);
                SlipConsole::LogError("Ya existe un script llamado '" + base + ".lua' en el proyecto: " + existing);
                return;
            }
            std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, base + ".lua");
            std::string templateCode =
                "-- Script de Entidad en SlipEngine\n"
                "local Script = {}\n\n"
                "-- Propiedades publicas expuestas en el Inspector (estilo Unity)\n"
                "Script.speed = 10.0\n"
                "Script.enabled = true\n\n"
                "-- Invocado al iniciar la simulacion (Play Mode)\n"
                "function Script:OnStart()\n"
                "    Slip.Log(\"Script iniciado en entidad: \" .. self.name)\n"
                "end\n\n"
                "-- Invocado en cada fotograma con deltaTime (en segundos)\n"
                "function Script:OnUpdate(dt)\n"
                "    -- self:GetTransform():Rotate(0, self.speed * dt, 0)\n"
                "end\n\n"
                "-- Eventos de colision fisica\n"
                "function Script:OnCollisionEnter(other)\n"
                "    -- Slip.Log(\"Colision con: \" .. other.name)\n"
                "end\n\n"
                "return Script\n";
            if (SlipFileSystem::WriteAllText(path, templateCode)) {
                SlipScriptIndexer::Get().RegisterScriptFile(path);
                SlipModelExtract::EnsureMetaForFile(path);
                RefreshCurrentDirectory();
                m_SelectedPath = path;
                SetSelectedAsset(path);
                if (m_OnAssetSelectCallback) m_OnAssetSelectCallback(path);
                if (SlipEditor::GetInstance()) {
                    SlipEditor::GetInstance()->OpenScript(path);
                }
            }
            break;
        }
        case PendingCreationType::AnimatorController: {
            std::string base = rawName;
            if (SlipFileSystem::GetFileExtension(base) != ".controller") {
                base += ".controller";
            }
            std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, base);
            SlipAnimatorController newCtrl;
            newCtrl.name = SlipFileSystem::GetFileNameWithoutExtension(base);
            newCtrl.AddParameter("Speed", AnimatorParamType::Float);
            newCtrl.AddParameter("IsGrounded", AnimatorParamType::Bool);
            newCtrl.AddParameter("Jump", AnimatorParamType::Trigger);
            newCtrl.AddState("Idle", "");
            if (newCtrl.SaveToFile(path)) {
                SlipModelExtract::EnsureMetaForFile(path);
                RefreshCurrentDirectory();
                m_SelectedPath = path;
                SetSelectedAsset(path);
                if (SlipEditor::GetInstance()) {
                    SlipEditor::GetInstance()->OpenAnimatorController(path);
                }
            }
            break;
        }
        case PendingCreationType::ShaderGraph: {
            std::string base = rawName;
            if (SlipFileSystem::GetFileExtension(base) != ".shadergraph") {
                base += ".shadergraph";
            }
            std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, base);
            SlipShaderGraph::ShaderGraph newGraph;
            newGraph.InitializeDefaultGraph();
            if (newGraph.SaveToFile(path)) {
                SlipModelExtract::EnsureMetaForFile(path);
                RefreshCurrentDirectory();
                m_SelectedPath = path;
                SetSelectedAsset(path);
                if (SlipEditor::GetInstance()) {
                    SlipEditor::GetInstance()->OpenShaderGraph(path);
                }
            }
            break;
        }
        default: break;
    }
    m_PendingCreation = PendingCreationType::None;
}

void ContentBrowserPanel::CreateNewFolder(const std::string& folderName) {
    std::string newPath = SlipFileSystem::CombinePath(m_CurrentDirectory, folderName);
    int counter = 1;
    while (SlipFileSystem::Exists(newPath)) {
        newPath = SlipFileSystem::CombinePath(m_CurrentDirectory, folderName + "_" + std::to_string(counter++));
    }
    SlipFileSystem::CreateDirectoryPath(newPath);
    RefreshCurrentDirectory();
}

std::string ContentBrowserPanel::CreateMaterialAsset(const std::string& baseName) {
    std::string name = baseName;
    std::string path = SlipFileSystem::CombinePath(m_CurrentDirectory, name + ".mat");
    int counter = 1;
    while (SlipFileSystem::Exists(path)) {
        path = SlipFileSystem::CombinePath(m_CurrentDirectory, name + "_" + std::to_string(counter++) + ".mat");
    }

    SlipMaterial newMat;
    newMat.Save(path);
    SlipModelExtract::EnsureMetaForFile(path);
    SlipThumbnailManager::Get().GenerateMaterialThumbnail(path, true);

    RefreshCurrentDirectory();

    m_SelectedPath = path;
    SetSelectedAsset(path);
    if (m_OnAssetSelectCallback) {
        m_OnAssetSelectCallback(path);
    }
    return path;
}

void ContentBrowserPanel::OpenRenameModal(const std::string& path) {
    if (path.empty() || !SlipFileSystem::Exists(path)) return;
    m_ItemToRename = path;
    std::string name = SlipFileSystem::GetFileName(path);
    std::strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
    m_ShowRenameModal = true;
}

void ContentBrowserPanel::RenameItem(const std::string& oldPath, const std::string& newName) {
    if (oldPath.empty() || newName.empty() || !SlipFileSystem::Exists(oldPath)) return;

    std::string ext = SlipFileSystem::GetFileExtension(oldPath);
    std::string newExt = SlipFileSystem::GetFileExtension(newName);
    if (ext == ".lua" || newExt == ".lua") {
        std::string baseNew = SlipFileSystem::GetFileNameWithoutExtension(newName);
        if (!SlipScriptIndexer::Get().IsScriptNameUnique(baseNew, oldPath)) {
            std::string existing = SlipScriptIndexer::Get().FindScriptPathByName(baseNew);
            SlipConsole::LogError("Ya existe un script llamado '" + baseNew + ".lua' en el proyecto: " + existing);
            return;
        }
    }

    std::string parent = SlipFileSystem::GetParentPath(oldPath);
    std::string newPath = SlipFileSystem::CombinePath(parent, newName);

    if (newPath == oldPath) return;

    std::error_code ec;
    fs::rename(oldPath, newPath, ec);

    std::string oldMeta = oldPath + ".meta";
    if (SlipFileSystem::Exists(oldMeta)) {
        std::string newMeta = newPath + ".meta";
        fs::rename(oldMeta, newMeta, ec);
        AssetMetadata meta;
        if (SlipModelExtract::LoadMeta(newMeta, meta)) {
            meta.originalFile = newName;
            SlipModelExtract::SaveMeta(newMeta, meta);
        }
    }

    SlipThumbnailManager::Get().InvalidateThumbnail(oldPath);
    SlipThumbnailManager::Get().InvalidateThumbnail(newPath);

    if (ext == ".lua") {
        SlipScriptIndexer::Get().UnregisterScriptFile(oldPath);
        SlipScriptIndexer::Get().RegisterScriptFile(newPath);
    }

    if (m_SelectedPath == oldPath) {
        m_SelectedPath = newPath;
        SetSelectedAsset(newPath);
        if (m_OnAssetSelectCallback) m_OnAssetSelectCallback(newPath);
    }

    RefreshCurrentDirectory();
}

void ContentBrowserPanel::CopyItem(const std::string& path) {
    m_ClipboardPath = path;
    m_ClipboardOp = ContentClipboardOp::Copy;
}

void ContentBrowserPanel::CutItem(const std::string& path) {
    m_ClipboardPath = path;
    m_ClipboardOp = ContentClipboardOp::Cut;
}

void ContentBrowserPanel::MoveItem(const std::string& sourcePath, const std::string& targetDir) {
    if (sourcePath.empty() || targetDir.empty() || !SlipFileSystem::Exists(sourcePath)) return;
    if (sourcePath == targetDir) return;

    std::string name = SlipFileSystem::GetFileName(sourcePath);
    std::string dest = SlipFileSystem::CombinePath(targetDir, name);
    if (dest == sourcePath) return;

    std::error_code ec;
    fs::rename(sourcePath, dest, ec);

    // Mover .meta adjunto si existe
    std::string sourceMeta = sourcePath + ".meta";
    if (SlipFileSystem::Exists(sourceMeta)) {
        std::string destMeta = dest + ".meta";
        fs::rename(sourceMeta, destMeta, ec);
    }

    RefreshCurrentDirectory();
}

void ContentBrowserPanel::PasteItem(const std::string& targetDir) {
    if (m_ClipboardOp == ContentClipboardOp::None || m_ClipboardPath.empty()) return;
    if (!SlipFileSystem::Exists(m_ClipboardPath)) {
        m_ClipboardOp = ContentClipboardOp::None;
        m_ClipboardPath.clear();
        return;
    }

    std::string name = SlipFileSystem::GetFileName(m_ClipboardPath);
    std::string dest = SlipFileSystem::CombinePath(targetDir, name);

    std::error_code ec;
    if (m_ClipboardOp == ContentClipboardOp::Cut) {
        if (dest != m_ClipboardPath) {
            fs::rename(m_ClipboardPath, dest, ec);
            std::string srcMeta = m_ClipboardPath + ".meta";
            if (SlipFileSystem::Exists(srcMeta)) {
                fs::rename(srcMeta, dest + ".meta", ec);
            }
        }
        m_ClipboardOp = ContentClipboardOp::None;
        m_ClipboardPath.clear();
    } else if (m_ClipboardOp == ContentClipboardOp::Copy) {
        if (SlipFileSystem::Exists(dest)) {
            std::string ext = SlipFileSystem::GetFileExtension(name);
            std::string base = name.substr(0, name.length() - ext.length());
            dest = SlipFileSystem::CombinePath(targetDir, base + "_Copia" + ext);
        }

        if (SlipFileSystem::IsDirectory(m_ClipboardPath)) {
            fs::copy(m_ClipboardPath, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        } else {
            fs::copy_file(m_ClipboardPath, dest, fs::copy_options::overwrite_existing, ec);
            std::string srcMeta = m_ClipboardPath + ".meta";
            if (SlipFileSystem::Exists(srcMeta)) {
                SlipModelExtract::EnsureMetaForFile(dest);
            }
        }
    }

    RefreshCurrentDirectory();
}

void ContentBrowserPanel::DeleteItem(const std::string& path) {
    if (path.empty() || !SlipFileSystem::Exists(path)) return;
    SlipThumbnailManager::Get().InvalidateThumbnail(path);
    SlipFileSystem::DeleteFileOrDir(path);
    std::string metaPath = path + ".meta";
    if (SlipFileSystem::Exists(metaPath)) {
        SlipFileSystem::DeleteFileOrDir(metaPath);
    }
    if (m_SelectedPath == path) {
        m_SelectedPath.clear();
        SetSelectedAsset("");
    }
    RefreshCurrentDirectory();
}

void ContentBrowserPanel::RequestAssetImport(const std::string& sourceFilePath, const std::string& targetDir) {
    if (sourceFilePath.empty() || !SlipFileSystem::Exists(sourceFilePath)) return;

    std::string destDir = targetDir.empty() ? m_CurrentDirectory : targetDir;
    std::string ext = SlipFileSystem::GetFileExtension(sourceFilePath);
    if (SlipModelExtract::DeduceAssetType(ext) == "Model") {
        m_PendingModelImportSource = sourceFilePath;
        m_PendingModelImportTargetDir = destDir;
        m_PendingModelImportOptions = AssetMetadata();
        m_PendingModelImportOptions.assetType = "Model";
        m_PendingModelImportOptions.importMaterials = true;
        m_PendingModelImportOptions.importTextures = true;
        m_PendingModelImportOptions.importNormals = true;
        m_PendingModelImportOptions.flipUVs = false;
        m_PendingModelImportOptions.generateColliders = true;
        m_PendingModelImportOptions.bakeTransforms = true;
        m_PendingModelImportOptions.importScale = 1.0f;
        m_PendingModelImportOptions.importRotation = glm::vec3(0.0f);
        m_ShowModelImportModal = true;
    } else {
        SlipModelExtract::ImportRawAssetAsync(sourceFilePath, destDir, [this](bool, const std::string&) {
            RefreshCurrentDirectory();
        });
    }
}

void ContentBrowserPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    if (ImGui::Begin("Explorador de Archivos", &m_IsOpen)) {
        // Asegurar que inicie y se mantenga dentro de assets/ si hay proyecto activo
        if (SlipProject::Get().HasActiveProject()) {
            std::string projAssets = SlipProject::Get().GetCurrentProject().assetsPath;
            if (!projAssets.empty()) {
                if (m_BaseDirectory.empty() || 
                    SlipFileSystem::NormalizePath(m_BaseDirectory) != SlipFileSystem::NormalizePath(projAssets) ||
                    m_CurrentDirectory.empty() ||
                    !IsInsideDirectory(m_CurrentDirectory, projAssets)) {
                    m_BaseDirectory = projAssets;
                    m_CurrentDirectory = projAssets;
                    RefreshCurrentDirectory();
                }
            }
        }

        // Barra superior de navegación y acciones (restringida a la raíz del proyecto)
        bool canGoUp = (!m_BaseDirectory.empty() && 
                        SlipFileSystem::NormalizePath(m_CurrentDirectory) != SlipFileSystem::NormalizePath(m_BaseDirectory) && 
                        IsInsideDirectory(m_CurrentDirectory, m_BaseDirectory));
        if (!canGoUp) ImGui::BeginDisabled();
        if (ImGui::Button(" ^ Subir nivel ")) {
            std::string parent = SlipFileSystem::GetParentPath(m_CurrentDirectory);
            if (!parent.empty() && parent != m_CurrentDirectory && IsInsideDirectory(parent, m_BaseDirectory)) {
                m_CurrentDirectory = parent;
                RefreshCurrentDirectory();
            }
        }
        if (!canGoUp) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(" Actualizar ")) {
            SlipModelExtract::ScanAndGenerateMetas(m_CurrentDirectory);
            RefreshCurrentDirectory();
        }
        ImGui::SameLine();
        if (ImGui::Button(" [Carpeta] ")) {
            OpenCreateModal(PendingCreationType::Folder);
        }
        ImGui::SameLine();
        if (ImGui::Button(" [+] Crear ▾ ")) {
            ImGui::OpenPopup("ContentBrowserContextMenu");
        }
        ImGui::SameLine();
        if (ImGui::Button(" Importar Asset ")) {
            SlipFileDialog::Instance().Open("ImportAssetPicker", "Importar Asset Crudo al Proyecto", ".fbx,.obj,.dae,.gltf,.png,.jpg,.jpeg,.tga,.wav,.ogg", "", SlipFileDialogType::OpenFile);
        }

        if (m_ClipboardOp != ContentClipboardOp::None && !m_ClipboardPath.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(" Pegar ")) {
                PasteItem(m_CurrentDirectory);
            }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Ruta: %s", m_CurrentDirectory.c_str());

        // Si hay una tarea en segundo plano (bakeado / importación), mostrar indicador con barra de progreso
        if (SlipModelExtract::IsProcessing()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.20f, 0.32f, 0.85f));
            if (ImGui::BeginChild("##AssetProcessingBanner", ImVec2(0, 32), true, ImGuiWindowFlags_NoScrollbar)) {
                float prog = SlipModelExtract::GetProcessProgress();
                std::string st = SlipModelExtract::GetProcessStatus();
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), " Tarea activa: %s", st.c_str());
                ImGui::SameLine();
                char overlay[32];
                snprintf(overlay, sizeof(overlay), "%.0f%%", prog * 100.0f);
                ImGui::ProgressBar(prog, ImVec2(180.0f, 18.0f), overlay);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        // Escuchar si el usuario confirmó importar un archivo
        if (SlipFileDialog::Instance().GetCurrentDialogKey() == "ImportAssetPicker" && SlipFileDialog::Instance().HasSelected()) {
            std::string source = SlipFileDialog::Instance().GetSelectedPath();
            SlipFileDialog::Instance().ClearSelection();
            RequestAssetImport(source, m_CurrentDirectory);
        }

        // Grilla de archivos y carpetas
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        float thumbnailSize = 110.0f;
        float padding = 16.0f;
#else
        float thumbnailSize = 85.0f;
        float padding = 12.0f;
#endif
        float cellSize = thumbnailSize + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        float bottomBarHeight = 35.0f;
        ImVec2 tableSize = ImVec2(0, ImGui::GetContentRegionAvail().y - bottomBarHeight);

        std::string itemToDelete;
        std::string folderToOpen;

        static bool s_LongPressFired = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            s_LongPressFired = false;
        }

        if (ImGui::BeginTable("ContentGrid", columnCount, ImGuiTableFlags_ScrollY, tableSize)) {
            for (const auto& entry : m_Entries) {
                ImGui::TableNextColumn();
                ImGui::PushID(entry.name.c_str());

                bool isSelected = (m_SelectedPath == entry.fullPath);
                unsigned int thumbTexId = SlipThumbnailManager::Get().GetThumbnail(entry.fullPath, entry.isDirectory, entry.extension);

                ImVec4 btnBg = isSelected ? ImVec4(0.24f, 0.45f, 0.78f, 0.90f) : ImVec4(0.14f, 0.15f, 0.18f, 0.70f);
                ImVec4 btnHover = isSelected ? ImVec4(0.28f, 0.52f, 0.88f, 1.0f) : ImVec4(0.22f, 0.24f, 0.28f, 0.85f);
                ImGui::PushStyleColor(ImGuiCol_Button, btnBg);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.58f, 0.95f, 1.0f));

                float imgW = thumbnailSize;
                float imgH = thumbnailSize - 18.0f;
                bool clicked = false;
                if (SlipRHI::GetActiveAPI() == GraphicsAPI::OpenGL && thumbTexId != 0) {
                    clicked = ImGui::ImageButton("##ThumbBtn",
                                                 (ImTextureID)(uintptr_t)thumbTexId,
                                                 ImVec2(imgW, imgH),
                                                 ImVec2(0, 0), ImVec2(1, 1),
                                                 ImVec4(0, 0, 0, 0),
                                                 ImVec4(1, 1, 1, 1));
                } else {
                    const char* badge = entry.isDirectory ? "[DIR]" : (entry.extension.empty() ? "[FILE]" : entry.extension.c_str());
                    clicked = ImGui::Button(badge, ImVec2(imgW, imgH));
                }
                ImGui::PopStyleColor(3);

                bool isItemHovered = ImGui::IsItemHovered();
                bool isItemActive = ImGui::IsItemActive();

                // Drag & Drop Origen
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", entry.fullPath.c_str(), entry.fullPath.size() + 1);
                    ImGui::Text("%s '%s'", entry.isDirectory ? "Mover carpeta" : "Mover archivo", entry.name.c_str());
                    ImGui::EndDragDropSource();
                }

                // Drag & Drop Destino en carpetas
                if (entry.isDirectory && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string draggedPath = (const char*)payload->Data;
                        MoveItem(draggedPath, entry.fullPath);
                    }
                    ImGui::EndDragDropTarget();
                }

                // Texto centrado y recortado debajo de la miniatura/icono
                std::string shortName = entry.name;
                if (shortName.size() > 12) {
                    shortName = shortName.substr(0, 10) + "..";
                }
                float textWidth = ImGui::CalcTextSize(shortName.c_str()).x;
                float textOffset = (thumbnailSize - textWidth) * 0.5f;
                if (textOffset > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffset);
                if (isSelected) {
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", shortName.c_str());
                } else {
                    ImGui::TextUnformatted(shortName.c_str());
                }

                // Tooltip enriquecido con información y previsualización
                if (isItemHovered) {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s", entry.name.c_str());
                    if (entry.isDirectory) {
                        ImGui::TextDisabled("Tipo: Carpeta");
                    } else {
                        std::string typeDesc = SlipModelExtract::DeduceAssetType(entry.extension);
                        ImGui::TextDisabled("Tipo: %s (%s)", typeDesc.c_str(), entry.extension.c_str());
                        ImGui::TextDisabled("Tamaño: %.2f KB", (float)entry.sizeInBytes / 1024.0f);
                    }
                    if (SlipRHI::GetActiveAPI() == GraphicsAPI::OpenGL &&
                        !entry.isDirectory && thumbTexId != 0 &&
                        thumbTexId != SlipThumbnailManager::Get().GetFileIcon() &&
                        thumbTexId != SlipThumbnailManager::Get().GetFolderIcon()) {
                        ImGui::Spacing();
                        ImGui::Image((ImTextureID)(uintptr_t)thumbTexId, ImVec2(96.0f, 96.0f));
                    }
                    ImGui::EndTooltip();
                }

                // Clic simple: Seleccionar elemento
                if (clicked) {
                    m_SelectedPath = entry.fullPath;
                    SetSelectedAsset(entry.fullPath);
                    if (m_OnAssetSelectCallback) {
                        m_OnAssetSelectCallback(entry.fullPath);
                    }
                }

                // Doble clic: Abrir carpeta o editar archivo
                if (isItemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.isDirectory) {
                        folderToOpen = entry.fullPath;
                    } else if (entry.extension == ".scene") {
                        if (SlipEditor::GetInstance()) {
                            SlipEditor::GetInstance()->LoadScene(entry.fullPath);
                        } else if (m_ContextLevel) {
                            m_ContextLevel->LoadScene(entry.fullPath);
                            SlipProject::Get().SetCurrentScenePath(entry.fullPath);
                        }
                    } else if (entry.extension == ".lua") {
                        if (SlipEditor::GetInstance()) {
                            SlipEditor::GetInstance()->OpenScript(entry.fullPath);
                        }
                    } else if (entry.extension == ".prefab") {
                        if (SlipEditor::GetInstance()) {
                            SlipEditor::GetInstance()->OpenPrefab(entry.fullPath);
                        }
                    } else if (entry.extension == ".anim") {
                        AnimationClipAsset animAsset;
                        if (SlipModelExtract::LoadAnimAsset(entry.fullPath, animAsset) && !animAsset.sourceModel.empty()) {
                            if (auto aep = AnimationEditorPanel::GetInstance()) {
                                aep->SetOpen(true);
                                aep->LoadModelForPreview(animAsset.sourceModel);
                            }
                        }
                    } else if (entry.extension == ".controller") {
                        if (SlipEditor::GetInstance()) {
                            SlipEditor::GetInstance()->OpenAnimatorController(entry.fullPath);
                        }
                    } else if (entry.extension == ".shadergraph") {
                        if (SlipEditor::GetInstance()) {
                            SlipEditor::GetInstance()->OpenShaderGraph(entry.fullPath);
                        }
                    }
                }

                // Clic derecho: Abrir menú contextual
                if (isItemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    m_SelectedPath = entry.fullPath;
                    SetSelectedAsset(entry.fullPath);
                    if (m_OnAssetSelectCallback) {
                        m_OnAssetSelectCallback(entry.fullPath);
                    }
                    ImGui::OpenPopup(entry.isDirectory ? "FolderContextMenu" : "FileContextMenu");
                }

                // Pulsación prolongada táctil (Android)
                bool isLongPress = isItemActive &&
                                   ImGui::GetIO().MouseDownDuration[0] >= 0.45f &&
                                   !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10.0f);
                if (isLongPress && !s_LongPressFired) {
                    s_LongPressFired = true;
                    m_SelectedPath = entry.fullPath;
                    SetSelectedAsset(entry.fullPath);
                    if (m_OnAssetSelectCallback) {
                        m_OnAssetSelectCallback(entry.fullPath);
                    }
                    ImGui::OpenPopup(entry.isDirectory ? "FolderContextMenu" : "FileContextMenu");
                }

                // Menús contextuales
                if (entry.isDirectory) {
                    if (ImGui::BeginPopup("FolderContextMenu")) {
                        if (ImGui::MenuItem("Abrir Carpeta")) {
                            folderToOpen = entry.fullPath;
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Renombrar")) {
                            OpenRenameModal(entry.fullPath);
                        }
                        if (ImGui::MenuItem("Copiar")) {
                            CopyItem(entry.fullPath);
                        }
                        if (ImGui::MenuItem("Cortar")) {
                            CutItem(entry.fullPath);
                        }
                        if (m_ClipboardOp != ContentClipboardOp::None) {
                            if (ImGui::MenuItem("Pegar dentro")) {
                                PasteItem(entry.fullPath);
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Eliminar")) {
                            itemToDelete = entry.fullPath;
                        }
                        ImGui::EndPopup();
                    }
                } else {
                    if (ImGui::BeginPopup("FileContextMenu")) {
                        m_SelectedPath = entry.fullPath;
                        SetSelectedAsset(entry.fullPath);
                        if (m_OnAssetSelectCallback) {
                            m_OnAssetSelectCallback(entry.fullPath);
                        }

                        if (entry.extension == ".lua") {
                            if (ImGui::MenuItem("Editar en Editor de Scripts")) {
                                if (SlipEditor::GetInstance()) {
                                    SlipEditor::GetInstance()->OpenScript(entry.fullPath);
                                }
                            }
                            ImGui::Separator();
                        } else if (entry.extension == ".prefab") {
                            if (ImGui::MenuItem("Editar Prefab")) {
                                if (SlipEditor::GetInstance()) {
                                    SlipEditor::GetInstance()->OpenPrefab(entry.fullPath);
                                }
                            }
                            if (ImGui::MenuItem("Instanciar en Escena")) {
                                SlipPrefab::Instantiate(entry.fullPath, m_ContextLevel);
                            }
                            ImGui::Separator();
                        } else if (entry.extension == ".controller") {
                            if (ImGui::MenuItem("Editar en Grafo de Animator")) {
                                if (SlipEditor::GetInstance()) {
                                    SlipEditor::GetInstance()->OpenAnimatorController(entry.fullPath);
                                }
                            }
                            ImGui::Separator();
                        } else if (entry.extension == ".shadergraph") {
                            if (ImGui::MenuItem("Editar en Shader Graph")) {
                                if (SlipEditor::GetInstance()) {
                                    SlipEditor::GetInstance()->OpenShaderGraph(entry.fullPath);
                                }
                            }
                            ImGui::Separator();
                        } else if (entry.extension == ".fbx" || entry.extension == ".gltf" || entry.extension == ".dae") {
                            if (ImGui::MenuItem("Extraer solo Animaciones (.anim)")) {
                                std::vector<std::string> outClips;
                                SlipModelExtract::ExtractAnimationClipsOnly(entry.fullPath, m_CurrentDirectory, outClips);
                                RefreshCurrentDirectory();
                            }
                            ImGui::Separator();
                        }

                        if (ImGui::MenuItem("Renombrar")) {
                            OpenRenameModal(entry.fullPath);
                        }
                        if (ImGui::MenuItem("Copiar")) {
                            CopyItem(entry.fullPath);
                        }
                        if (ImGui::MenuItem("Cortar")) {
                            CutItem(entry.fullPath);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Eliminar")) {
                            itemToDelete = entry.fullPath;
                        }
                        ImGui::EndPopup();
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!folderToOpen.empty()) {
            m_CurrentDirectory = folderToOpen;
            RefreshCurrentDirectory();
        }

        if (!itemToDelete.empty()) {
            DeleteItem(itemToDelete);
        }

        // Detección de clic derecho o pulsación prolongada (Long-Press táctil / Android) en espacio vacío
        bool isBlankLongPress = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
                                !ImGui::IsAnyItemHovered() &&
                                ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                                ImGui::GetIO().MouseDownDuration[0] >= 0.45f &&
                                !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 10.0f);

        if (((ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
              ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
              !ImGui::IsAnyItemHovered())) || (isBlankLongPress && !s_LongPressFired))
        {
            if (isBlankLongPress) s_LongPressFired = true;
            ImGui::OpenPopup("ContentBrowserContextMenu");
        }

        // Menú Contextual del Explorador de Archivos
        if (ImGui::BeginPopup("ContentBrowserContextMenu")) {
            if (ImGui::MenuItem("Nueva Carpeta")) {
                OpenCreateModal(PendingCreationType::Folder);
            }
            if (ImGui::MenuItem("Nuevo Material (.mat)")) {
                OpenCreateModal(PendingCreationType::Material);
            }
            if (ImGui::MenuItem("Nuevo Script Lua (.lua)")) {
                OpenCreateModal(PendingCreationType::Script);
            }
            if (ImGui::MenuItem("Nuevo Animator Controller (.controller)")) {
                OpenCreateModal(PendingCreationType::AnimatorController);
            }
            if (ImGui::MenuItem("Nuevo Shader Graph (.shadergraph)")) {
                OpenCreateModal(PendingCreationType::ShaderGraph);
            }
            if (m_ClipboardOp != ContentClipboardOp::None && !m_ClipboardPath.empty()) {
                if (ImGui::MenuItem("Pegar")) {
                    PasteItem(m_CurrentDirectory);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Actualizar")) {
                RefreshCurrentDirectory();
            }
            ImGui::EndPopup();
        }

        // Modal de Creación de Elemento (Primero pide el nombre y luego lo genera)
        if (m_ShowCreateModal) {
            ImGui::OpenPopup("Crear Elemento");
            m_ShowCreateModal = false;
        }

        if (ImGui::BeginPopupModal("Crear Elemento", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const char* promptText = "Introduce el nombre:";
            switch (m_PendingCreation) {
                case PendingCreationType::Folder: promptText = "Nombre de la nueva carpeta:"; break;
                case PendingCreationType::Material: promptText = "Nombre del nuevo material (.mat):"; break;
                case PendingCreationType::Script: promptText = "Nombre del nuevo script Lua (.lua):"; break;
                case PendingCreationType::AnimatorController: promptText = "Nombre del Animator Controller (.controller):"; break;
                case PendingCreationType::ShaderGraph: promptText = "Nombre del Shader Graph (.shadergraph):"; break;
                default: break;
            }
            ImGui::Text("%s", promptText);
            ImGui::Spacing();
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            bool enterPressed = ImGui::InputText("##CreateNameInput", m_CreationNameBuffer, sizeof(m_CreationNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();

            if (ImGui::Button(" Crear ", ImVec2(120, 0)) || enterPressed) {
                ConfirmCreation();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(" Cancelar ", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_PendingCreation = PendingCreationType::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Modal de Renombrado
        if (m_ShowRenameModal) {
            ImGui::OpenPopup("Renombrar Elemento");
            m_ShowRenameModal = false;
        }

        if (ImGui::BeginPopupModal("Renombrar Elemento", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Nuevo nombre para: %s", SlipFileSystem::GetFileName(m_ItemToRename).c_str());
            ImGui::Spacing();
            ImGui::InputText("##RenameInput", m_RenameBuffer, sizeof(m_RenameBuffer));
            ImGui::Spacing();

            if (ImGui::Button(" Confirmar ", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                RenameItem(m_ItemToRename, m_RenameBuffer);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(" Cancelar ", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Modal de Opciones de Importación de Modelo 3D (.obj / .fbx / .gltf)
        static ModelInspectionInfo s_ImportInspectInfo;
        if (m_ShowModelImportModal) {
            SlipModelExtract::InspectModelDetailed(m_PendingModelImportSource, s_ImportInspectInfo);
            ImGui::OpenPopup("Opciones de Importación de Modelo");
            m_ShowModelImportModal = false;
        }

        if (ImGui::BeginPopupModal("Opciones de Importación de Modelo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            std::string srcName = SlipFileSystem::GetFileName(m_PendingModelImportSource);
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Modelo: %s", srcName.c_str());
            ImGui::TextDisabled("Carpeta destino: %s", m_PendingModelImportTargetDir.c_str());
            ImGui::TextDisabled("Mallas: %d | Vertices: %d", s_ImportInspectInfo.meshCount, s_ImportInspectInfo.vertexCount);
            ImGui::Separator();
            ImGui::Spacing();

            // Sección: Esqueleto y Animaciones
            ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "Esqueleto y Animaciones:");
            ImGui::Checkbox("Importar Esqueleto / Huesos (Skinning)", &m_PendingModelImportOptions.importBones);
            ImGui::Checkbox("Importar Animaciones (Clips)", &m_PendingModelImportOptions.importAnimations);

            if (s_ImportInspectInfo.boneCount > 0) {
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "  -> Huesos detectados: %d", s_ImportInspectInfo.boneCount);
            }
            if (!s_ImportInspectInfo.animations.empty()) {
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "  -> Animaciones detectadas (%d):", (int)s_ImportInspectInfo.animations.size());
                for (size_t a = 0; a < s_ImportInspectInfo.animations.size(); ++a) {
                    const auto& anim = s_ImportInspectInfo.animations[a];
                    ImGui::BulletText("Clip: \"%s\"  |  Duracion: %.2f s (%.0f fps)", anim.name.c_str(), anim.durationSeconds, anim.ticksPerSecond);
                }
            } else {
                ImGui::TextDisabled("  -> (No se detectaron animaciones en este archivo)");
            }

            ImGui::Spacing();
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "Materiales y Texturas:");
            ImGui::Checkbox("Importar Materiales del Modelo", &m_PendingModelImportOptions.importMaterials);
            if (m_PendingModelImportOptions.importMaterials) {
                ImGui::TextDisabled("  -> Se creará la subcarpeta 'Materials/' junto al modelo.");
                ImGui::Checkbox("Importar Texturas Referenciadas", &m_PendingModelImportOptions.importTextures);
                if (m_PendingModelImportOptions.importTextures) {
                    ImGui::TextDisabled("  -> Se creará la subcarpeta 'Textures/' si el modelo incluye texturas.");
                }
            } else {
                ImGui::BeginDisabled();
                bool disabledTex = false;
                ImGui::Checkbox("Importar Texturas Referenciadas", &disabledTex);
                ImGui::EndDisabled();
                ImGui::TextDisabled("  -> Los slots de material del modelo quedarán vacíos ([Vacío / Default])\n     para que puedas asignarles materiales creados en el motor.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "Geometría y Transformación:");
            ImGui::DragFloat("Escala de Importación", &m_PendingModelImportOptions.importScale, 0.005f, 0.0001f, 1000.0f, "%.4f");
            ImGui::DragFloat3("Rotación (°)", &m_PendingModelImportOptions.importRotation.x, 1.0f, -360.0f, 360.0f, "%.1f");
            ImGui::Checkbox("Hornear Transformaciones de Nodos (Bake)", &m_PendingModelImportOptions.bakeTransforms);
            ImGui::Checkbox("Generar Normales Suaves", &m_PendingModelImportOptions.importNormals);
            ImGui::Checkbox("Voltear Coordenadas UV (Flip UVs)", &m_PendingModelImportOptions.flipUVs);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Si las texturas del modelo se ven al reves o de cabeza (ej. modelos de Mixamo), desmarca o marca esta opcion.");
            }
            ImGui::Checkbox("Generar Colliders Automáticos", &m_PendingModelImportOptions.generateColliders);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(" Importar Modelo ", ImVec2(150, 32))) {
                AssetMetadata opts = m_PendingModelImportOptions;
                if (!opts.importMaterials) {
                    opts.importTextures = false;
                }
                std::string src = m_PendingModelImportSource;
                std::string dst = m_PendingModelImportTargetDir;
                SlipModelExtract::ImportRawAssetAsync(src, dst, [this](bool, const std::string&) {
                    RefreshCurrentDirectory();
                }, &opts);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(" Solo Animacion (.anim) ", ImVec2(185, 32))) {
                std::string src = m_PendingModelImportSource;
                std::string dst = m_PendingModelImportTargetDir;
                std::vector<std::string> outClips;
                SlipModelExtract::ExtractAnimationClipsOnly(src, dst, outClips);
                RefreshCurrentDirectory();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(" Cancelar ", ImVec2(100, 32))) {
                m_PendingModelImportSource.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        // Barra inferior: Información de metadatos del asset seleccionado (con caché de alta velocidad para evitar lecturas de disco a 60 FPS)
        if (!m_SelectedPath.empty() && SlipFileSystem::Exists(m_SelectedPath)) {
            if (m_CachedSelectedPath != m_SelectedPath) {
                m_CachedSelectedPath = m_SelectedPath;
                AssetMetadata meta;
                m_HasCachedMeta = SlipModelExtract::EnsureMetaForFile(m_SelectedPath, &meta);
                if (m_HasCachedMeta) {
                    m_CachedOriginalFile = meta.originalFile;
                    m_CachedAssetType = meta.assetType;
                    m_CachedGuid = meta.guid;
                }
            }
            if (m_HasCachedMeta) {
                ImGui::Text("Asset: %s  |  Tipo: %s  |  UID: ", m_CachedOriginalFile.c_str(), m_CachedAssetType.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", m_CachedGuid.c_str());
            }
        } else {
            m_CachedSelectedPath.clear();
            m_HasCachedMeta = false;
            ImGui::TextDisabled("Ningún asset seleccionado. Selecciona un archivo para ver su UID.");
        }
    }
    ImGui::End();
}
