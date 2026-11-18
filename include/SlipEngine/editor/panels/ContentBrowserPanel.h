#pragma once

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include <string>
#include <vector>
#include <functional>

enum class ContentClipboardOp {
    None,
    Copy,
    Cut
};

enum class PendingCreationType {
    None,
    Folder,
    Material,
    Script,
    AnimatorController,
    ShaderGraph
};

class ContentBrowserPanel : public EditorPanel {
public:
    using AssetSelectionCallback = std::function<void(const std::string&)>;

    ContentBrowserPanel(AssetSelectionCallback onAssetSelect = nullptr);
    virtual ~ContentBrowserPanel() = default;

    void OnImGuiRender() override;
    void SetAssetSelectionCallback(AssetSelectionCallback cb) { m_OnAssetSelectCallback = cb; }

    void SetCurrentDirPath(const std::string& dir);
    void RefreshCurrentDirectory();

    void OpenCreateModal(PendingCreationType type);
    void ConfirmCreation();

    void CreateNewFolder(const std::string& folderName = "Nueva_Carpeta");
    std::string CreateMaterialAsset(const std::string& baseName = "NuevoMaterial");
    void RenameItem(const std::string& oldPath, const std::string& newName);
    void OpenRenameModal(const std::string& path);
    void CopyItem(const std::string& path);
    void CutItem(const std::string& path);
    void PasteItem(const std::string& targetDir);
    void DeleteItem(const std::string& path);
    void MoveItem(const std::string& sourcePath, const std::string& targetDir);
    void RequestAssetImport(const std::string& sourceFilePath, const std::string& targetDir = "");

private:
    std::string m_BaseDirectory;
    std::string m_CurrentDirectory;
    std::vector<FileInfo> m_Entries;
    std::string m_SelectedPath;
    char m_SearchBuffer[128] = "";
    AssetSelectionCallback m_OnAssetSelectCallback;

    std::string m_ClipboardPath;
    ContentClipboardOp m_ClipboardOp = ContentClipboardOp::None;

    PendingCreationType m_PendingCreation = PendingCreationType::None;
    char m_CreationNameBuffer[128] = "";
    bool m_ShowCreateModal = false;

    std::string m_ItemToRename;
    char m_RenameBuffer[128] = "";
    bool m_ShowRenameModal = false;

    std::string m_PendingModelImportSource;
    std::string m_PendingModelImportTargetDir;
    AssetMetadata m_PendingModelImportOptions;
    bool m_ShowModelImportModal = false;

    std::string m_CachedSelectedPath;
    std::string m_CachedOriginalFile;
    std::string m_CachedAssetType;
    std::string m_CachedGuid;
    bool m_HasCachedMeta = false;
};
