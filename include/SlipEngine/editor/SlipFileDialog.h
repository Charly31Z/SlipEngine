#pragma once

#include "SlipEngine/tools/SlipFileSystem.h"
#include <string>
#include <vector>
#include <functional>

enum class SlipFileDialogType {
    OpenFile,
    SaveFile,
    SelectFolder
};

class SlipFileDialog {
public:
    static SlipFileDialog& Instance();

    // Abre el diálogo modal de archivos
    // dialogKey: Identificador único de este diálogo (ej: "OpenModel", "SaveScene")
    // title: Título mostrado en la barra de la ventana ImGui
    // filters: Filtros separados por comas (ej: ".obj,.smd,.bsp", ".png,.jpg", "*.*")
    // startPath: Directorio inicial (vacío para usar el almacenamiento predeterminado)
    // type: Modo de selección (OpenFile, SaveFile, SelectFolder)
    void Open(const std::string& dialogKey,
              const std::string& title,
              const std::string& filters = "*.*",
              const std::string& startPath = "",
              SlipFileDialogType type = SlipFileDialogType::OpenFile);

    // Dibuja el diálogo en ImGui.
    // Retorna true cuando el usuario confirma una selección o cancela.
    bool Render();

    bool HasSelected() const { return m_HasSelected; }
    std::string GetSelectedPath() const { return m_SelectedPath; }
    std::string GetSelectedFileName() const { return m_SelectedFileName; }
    std::string GetCurrentDialogKey() const { return m_CurrentDialogKey; }
    bool IsOpen() const { return m_IsOpen; }

    void ClearSelection() {
        m_HasSelected = false;
        m_CurrentDialogKey.clear();
    }

    void Close();

private:
    SlipFileDialog();
    ~SlipFileDialog() = default;

    void SetDirectory(const std::string& newPath);
    void RefreshDirectory();

    bool m_IsOpen = false;
    bool m_HasSelected = false;
    bool m_NeedsOpenPopup = false;
    SlipFileDialogType m_Type = SlipFileDialogType::OpenFile;

    std::string m_CurrentDialogKey;
    std::string m_Title;
    std::string m_CurrentPath;
    std::string m_SelectedPath;
    std::string m_SelectedFileName;
    std::string m_FilterStr;
    std::vector<std::string> m_FilterList;
    int m_CurrentFilterIndex = 0;

    std::vector<FileInfo> m_CurrentEntries;
    char m_FileNameBuffer[260] = "";
    char m_SearchBuffer[128] = "";
    char m_PathInputBuffer[512] = "";
};

