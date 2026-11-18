#include "SlipEngine/editor/SlipFileDialog.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

SlipFileDialog& SlipFileDialog::Instance() {
    static SlipFileDialog s_Instance;
    return s_Instance;
}

SlipFileDialog::SlipFileDialog() {
    std::memset(m_FileNameBuffer, 0, sizeof(m_FileNameBuffer));
    std::memset(m_SearchBuffer, 0, sizeof(m_SearchBuffer));
    std::memset(m_PathInputBuffer, 0, sizeof(m_PathInputBuffer));
}

void SlipFileDialog::Open(const std::string& dialogKey,
                          const std::string& title,
                          const std::string& filters,
                          const std::string& startPath,
                          SlipFileDialogType type) {
    m_CurrentDialogKey = dialogKey;
    m_Title = title.empty() ? "Selector de Archivos" : title;
    m_FilterStr = filters.empty() ? "*.*" : filters;
    m_Type = type;
    m_IsOpen = true;
    m_HasSelected = false;
    m_NeedsOpenPopup = true;
    m_SelectedPath.clear();
    m_SelectedFileName.clear();
    std::memset(m_FileNameBuffer, 0, sizeof(m_FileNameBuffer));
    std::memset(m_SearchBuffer, 0, sizeof(m_SearchBuffer));

    // Parsear filtros separados por comas
    m_FilterList.clear();
    std::stringstream ss(m_FilterStr);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            item.erase(item.find_last_not_of(" \t\n\r") + 1);
            item.erase(0, item.find_first_not_of(" \t\n\r"));
            m_FilterList.push_back(item);
        }
    }
    if (m_FilterList.empty()) {
        m_FilterList.push_back("*.*");
    }
    m_CurrentFilterIndex = 0;

    // Directorio de inicio
    std::string initPath = startPath;
    if (initPath.empty() || !SlipFileSystem::Exists(initPath)) {
        initPath = SlipFileSystem::GetDefaultWorkingPath();
    }
    SetDirectory(initPath);
}

void SlipFileDialog::Close() {
    m_IsOpen = false;
    m_NeedsOpenPopup = false;
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::CloseCurrentPopup();
    }
}

void SlipFileDialog::SetDirectory(const std::string& newPath) {
    m_CurrentPath = SlipFileSystem::NormalizePath(newPath);
    if (m_CurrentPath.empty()) {
        m_CurrentPath = "/";
    }

    std::strncpy(m_PathInputBuffer, m_CurrentPath.c_str(), sizeof(m_PathInputBuffer) - 1);
    m_PathInputBuffer[sizeof(m_PathInputBuffer) - 1] = '\0';

    RefreshDirectory();
}

void SlipFileDialog::RefreshDirectory() {
    std::string activeFilter = (m_CurrentFilterIndex < (int)m_FilterList.size()) ? m_FilterList[m_CurrentFilterIndex] : "*.*";
    m_CurrentEntries = SlipFileSystem::ListDirectory(m_CurrentPath, activeFilter);
}

static std::string FormatBytes(uintmax_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
        return ss.str();
    } else {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
        return ss.str();
    }
}

bool SlipFileDialog::Render() {
    if (!m_IsOpen) return false;
    if (ImGui::GetCurrentContext() == nullptr) return false;

    if (m_NeedsOpenPopup) {
        ImGui::OpenPopup(m_Title.c_str());
        m_NeedsOpenPopup = false;
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 dialogSize = ImVec2(
        std::min(displaySize.x * 0.92f, 850.0f),
        std::min(displaySize.y * 0.88f, 560.0f)
    );
    ImGui::SetNextWindowSize(dialogSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool finished = false;

    if (ImGui::BeginPopupModal(m_Title.c_str(), &m_IsOpen, ImGuiWindowFlags_MenuBar)) {

        // --- Atajos rápidos de carpetas según plataforma ---
        ImGui::Text("Atajos:");
        ImGui::SameLine();

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        if (ImGui::Button("Descargas")) {
            SetDirectory("/storage/emulated/0/Download");
        }
        ImGui::SameLine();
        if (ImGui::Button("Documentos")) {
            SetDirectory("/storage/emulated/0/Documents");
        }
        ImGui::SameLine();
        if (ImGui::Button("Almacenamiento (/sdcard)")) {
            SetDirectory(SlipFileSystem::GetExternalStoragePath());
        }
        ImGui::SameLine();
        if (ImGui::Button("Interno (App)")) {
            SetDirectory(SlipFileSystem::GetInternalStoragePath());
        }

        if (!SlipFileSystem::HasStoragePermission()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
            ImGui::Text("Aviso: Falta autorizar acceso a todos los archivos para navegar en tu dispositivo Android.");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Conceder Permisos de Almacenamiento")) {
                SlipFileSystem::RequestStoragePermissions();
            }
        }
#else
        if (ImGui::Button("Proyecto")) {
            SetDirectory(SlipFileSystem::GetDefaultWorkingPath());
        }
        ImGui::SameLine();
        if (ImGui::Button("Assets")) {
            SetDirectory(SlipFileSystem::CombinePath(SlipFileSystem::GetDefaultWorkingPath(), "assets"));
        }
        ImGui::SameLine();
        if (ImGui::Button("C:/")) {
            SetDirectory("C:/");
        }
        ImGui::SameLine();
        if (ImGui::Button("D:/")) {
            SetDirectory("D:/");
        }
#endif

        ImGui::Separator();

        // --- Barra de navegación superior: Subir, Recargar, Ruta editable ---
        if (ImGui::Button(" ^ Subir ")) {
            std::string parent = SlipFileSystem::GetParentPath(m_CurrentPath);
            if (!parent.empty() && parent != m_CurrentPath) {
                SetDirectory(parent);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(" Recargar ")) {
            RefreshDirectory();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::InputText("##PathInput", m_PathInputBuffer, sizeof(m_PathInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (SlipFileSystem::Exists(m_PathInputBuffer)) {
                SetDirectory(m_PathInputBuffer);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Ir")) {
            if (SlipFileSystem::Exists(m_PathInputBuffer)) {
                SetDirectory(m_PathInputBuffer);
            }
        }

        // Filtro de búsqueda rápida
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##SearchInput", "Filtrar por nombre...", m_SearchBuffer, sizeof(m_SearchBuffer));

        ImGui::Separator();

        // --- Área central con la lista de carpetas y archivos ---
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 3.2f;
        ImVec2 listSize = ImVec2(0, ImGui::GetContentRegionAvail().y - footerHeight);

        if (ImGui::BeginTable("FileListTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, listSize)) {
            ImGui::TableSetupColumn("Nombre", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Tamano", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            std::string searchLower = m_SearchBuffer;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

            for (const auto& entry : m_CurrentEntries) {
                if (!searchLower.empty()) {
                    std::string entryNameLower = entry.name;
                    std::transform(entryNameLower.begin(), entryNameLower.end(), entryNameLower.begin(), ::tolower);
                    if (entryNameLower.find(searchLower) == std::string::npos) {
                        continue;
                    }
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                bool isSelected = (m_SelectedFileName == entry.name);
                std::string label = (entry.isDirectory ? "[DIR]  " : "[FILE] ") + entry.name;

                if (entry.isDirectory) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f)); // Color dorado de carpeta
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                }

                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (entry.isDirectory) {
                        // Doble clic o toque en carpeta -> entra a la carpeta
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            SetDirectory(entry.fullPath);
                            ImGui::PopStyleColor();
                            break;
                        }
                    } else {
                        // Selección de archivo
                        m_SelectedFileName = entry.name;
                        std::strncpy(m_FileNameBuffer, entry.name.c_str(), sizeof(m_FileNameBuffer) - 1);
                        m_FileNameBuffer[sizeof(m_FileNameBuffer) - 1] = '\0';

                        // Doble clic confirma la apertura
                        if (ImGui::IsMouseDoubleClicked(0) && m_Type != SlipFileDialogType::SelectFolder) {
                            m_SelectedPath = entry.fullPath;
                            m_HasSelected = true;
                            finished = true;
                            Close();
                            ImGui::PopStyleColor();
                            break;
                        }
                    }
                }

                ImGui::PopStyleColor();

                // Columna Tipo
                ImGui::TableSetColumnIndex(1);
                if (entry.isDirectory) {
                    ImGui::TextDisabled("Carpeta");
                } else {
                    ImGui::TextDisabled("%s", entry.extension.empty() ? "Archivo" : entry.extension.c_str());
                }

                // Columna Tamaño
                ImGui::TableSetColumnIndex(2);
                if (entry.isDirectory) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::TextDisabled("%s", FormatBytes(entry.sizeInBytes).c_str());
                }
            }

            ImGui::EndTable();
        }

        ImGui::Separator();

        // --- Pie de ventana: Nombre de archivo, Selector de filtro y Botones de acción ---
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Archivo:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220.0f);
        ImGui::InputText("##SelectedFileInput", m_FileNameBuffer, sizeof(m_FileNameBuffer));

        // Selector de tipo de filtro
        ImGui::SameLine();
        ImGui::SetNextItemWidth(210.0f);
        if (!m_FilterList.empty()) {
            if (ImGui::BeginCombo("##FilterCombo", m_FilterList[m_CurrentFilterIndex].c_str())) {
                for (int i = 0; i < (int)m_FilterList.size(); ++i) {
                    bool isSelected = (m_CurrentFilterIndex == i);
                    if (ImGui::Selectable(m_FilterList[i].c_str(), isSelected)) {
                        m_CurrentFilterIndex = i;
                        RefreshDirectory();
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        // Botones de acción
        const char* confirmButtonText = "Abrir";
        if (m_Type == SlipFileDialogType::SaveFile) {
            confirmButtonText = "Guardar";
        } else if (m_Type == SlipFileDialogType::SelectFolder) {
            confirmButtonText = "Seleccionar";
        }

        float buttonWidth = 120.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (buttonWidth * 2.0f + 25.0f));

        if (ImGui::Button(confirmButtonText, ImVec2(buttonWidth, 0))) {
            if (m_Type == SlipFileDialogType::SelectFolder) {
                m_SelectedPath = m_CurrentPath;
                m_HasSelected = true;
                finished = true;
                Close();
            } else {
                std::string fname = m_FileNameBuffer;
                if (!fname.empty()) {
                    m_SelectedFileName = fname;
                    m_SelectedPath = SlipFileSystem::CombinePath(m_CurrentPath, fname);
                    m_HasSelected = true;
                    finished = true;
                    Close();
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(buttonWidth, 0))) {
            m_HasSelected = false;
            finished = true;
            Close();
        }

        ImGui::EndPopup();
    } else {
        m_IsOpen = false;
    }

    return finished;
}

