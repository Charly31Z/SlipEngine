#include "SlipEngine/editor/panels/ScriptEditorPanel.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/editor/SlipProject.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include <SDL2/SDL.h>
#include <imgui.h>
#include <algorithm>

ScriptEditorPanel::ScriptEditorPanel() {
    m_IsOpen = true;
}

ScriptEditorPanel::~ScriptEditorPanel() = default;

void ScriptEditorPanel::OpenScript(const std::string& filePath, int targetLine) {
    m_IsOpen = true;

    // Comprobar si ya está abierto en alguna pestaña
    for (size_t i = 0; i < m_Tabs.size(); ++i) {
        if (m_Tabs[i].filePath == filePath) {
            m_ActiveTabIndex = static_cast<int>(i);
            m_Tabs[i].isOpen = true;
            if (targetLine > 0) {
                m_Tabs[i].editor.SetCursorPosition(targetLine, 0);
            }
            return;
        }
    }

    // Leer contenido del archivo
    std::string content;
    if (!SlipFileSystem::ReadAllText(filePath, content)) {
        SlipConsole::LogError("No se pudo abrir el script: " + filePath);
        return;
    }

    ScriptTab newTab;
    newTab.filePath = filePath;
    newTab.title = SlipFileSystem::GetFileName(filePath);
    newTab.editor.SetText(content);
    if (targetLine > 0) {
        newTab.editor.SetCursorPosition(targetLine, 0);
    }

    // Callbacks del editor
    newTab.editor.SetAutocompleteTriggerCallback([this](const std::string& prefix, ImVec2 pos) {
        m_AutocompletePrefix = prefix;
        m_AutocompletePos = pos;
        m_CurrentSuggestions = SlipScriptIndexer::Get().QuerySymbols(prefix);
        if (!m_CurrentSuggestions.empty()) {
            m_ShowAutocomplete = true;
            m_SelectedSuggestionIndex = 0;
        } else {
            m_ShowAutocomplete = false;
        }
    });

    newTab.editor.SetDismissAutocompleteCallback([this]() {
        m_ShowAutocomplete = false;
    });

    newTab.editor.SetGoToDefinitionCallback([this](const std::string& word) {
        const ScriptSymbol* def = SlipScriptIndexer::Get().FindDefinition(word);
        if (def && !def->sourceFile.empty() && def->line > 0 && SlipFileSystem::Exists(def->sourceFile)) {
            OpenScript(def->sourceFile, def->line);
            SlipConsole::LogInfo("Navegando a definición: " + def->displayName + " en " + def->sourceFile + ":" + std::to_string(def->line));
        } else if (def) {
            SlipConsole::LogInfo("Definición nativa: " + def->displayName + " - " + def->description);
        }
    });

    m_Tabs.push_back(std::move(newTab));
    m_ActiveTabIndex = static_cast<int>(m_Tabs.size()) - 1;

    // Asegurar que el archivo esté indexado en el catálogo de símbolos
    SlipScriptIndexer::Get().IndexScript(filePath, content);
}

void ScriptEditorPanel::SaveActiveScript() {
    if (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Tabs.size())) {
        SaveScript(m_Tabs[m_ActiveTabIndex]);
    }
}

void ScriptEditorPanel::SaveScript(ScriptTab& tab) {
    std::string text = tab.editor.GetText();
    if (SlipFileSystem::WriteAllText(tab.filePath, text)) {
        tab.editor.SetModified(false);
        SlipScriptIndexer::Get().IndexScript(tab.filePath, text);
        SlipConsole::LogSuccess("Script guardado correctamente: " + tab.title);

        // Recargar en caliente todas las entidades de la escena que usen este script
        if (m_ContextLevel) {
            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                if (!ent) continue;
                for (auto& sc : ent->GetScripts()) {
                    if (sc && (sc->scriptPath == tab.filePath || sc->scriptPath == tab.title || SlipFileSystem::GetFileName(sc->scriptPath) == tab.title)) {
                        sc->ReloadScript();
                    }
                }
            }
        }
    } else {
        SlipConsole::LogError("Error al guardar script: " + tab.filePath);
    }
}

void ScriptEditorPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(WINDOW_NAME, &m_IsOpen, ImGuiWindowFlags_MenuBar)) {
        // Atajo global Ctrl+S para guardar
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            SaveActiveScript();
        }

        // Tecla ESC para cerrar cualquier popup de autocompletado inmediatamente
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_ShowAutocomplete = false;
            if (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Tabs.size())) {
                m_Tabs[m_ActiveTabIndex].editor.DismissAutocomplete();
            }
        }

        // Barra de menús del editor de código
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Archivo")) {
                if (ImGui::MenuItem("Guardar Script", "Ctrl+S", false, m_ActiveTabIndex >= 0)) {
                    SaveActiveScript();
                }
                if (ImGui::MenuItem("Guardar Todos", nullptr, false, !m_Tabs.empty())) {
                    for (auto& tab : m_Tabs) SaveScript(tab);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reindexar Simbolos del Proyecto")) {
                    if (SlipProject::Get().HasActiveProject()) {
                        SlipScriptIndexer::Get().RefreshIndex(SlipProject::Get().GetCurrentProject().assetsPath);
                        SlipConsole::LogInfo("Símbolos de scripts del proyecto reindexados.");
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Editar")) {
                if (ImGui::MenuItem("Cortar", "Ctrl+X", false, m_ActiveTabIndex >= 0)) {
                    if (m_ActiveTabIndex >= 0) m_Tabs[m_ActiveTabIndex].editor.Cut();
                }
                if (ImGui::MenuItem("Copiar", "Ctrl+C", false, m_ActiveTabIndex >= 0)) {
                    if (m_ActiveTabIndex >= 0) m_Tabs[m_ActiveTabIndex].editor.Copy();
                }
                if (ImGui::MenuItem("Pegar", "Ctrl+V", false, m_ActiveTabIndex >= 0)) {
                    if (m_ActiveTabIndex >= 0) m_Tabs[m_ActiveTabIndex].editor.Paste();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Seleccionar Todo", "Ctrl+A", false, m_ActiveTabIndex >= 0)) {
                    if (m_ActiveTabIndex >= 0) m_Tabs[m_ActiveTabIndex].editor.SelectAll();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Ir a Definicion", "F12 / Ctrl+Clic", false, m_ActiveTabIndex >= 0)) {
                    if (m_ActiveTabIndex >= 0) {
                        std::string word = m_Tabs[m_ActiveTabIndex].editor.GetCurrentWord();
                        const ScriptSymbol* def = SlipScriptIndexer::Get().FindDefinition(word);
                        if (def && !def->sourceFile.empty() && def->line > 0 && SlipFileSystem::Exists(def->sourceFile)) {
                            OpenScript(def->sourceFile, def->line);
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if (m_ActiveTabIndex >= 0) {
                if (ImGui::Button(" [Pegar] ")) {
                    m_Tabs[m_ActiveTabIndex].editor.Paste();
                }
            }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
            ImGui::SameLine();
            if (ImGui::Button(SDL_IsTextInputActive() ? " [Ocultar Teclado] " : " [Teclado Virtual] ")) {
                if (SDL_IsTextInputActive()) {
                    SDL_StopTextInput();
                } else {
                    SDL_StartTextInput();
                }
            }
#endif

            ImGui::EndMenuBar();
        }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        bool isEditorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !m_Tabs.empty();
        if (isEditorFocused && !SDL_IsTextInputActive()) {
            SDL_StartTextInput();
        } else if (!isEditorFocused && SDL_IsTextInputActive()) {
            SDL_StopTextInput();
        }
#endif

        RenderTabBar();
        RenderAutocompletePopup();
    }
    ImGui::End();
}

void ScriptEditorPanel::RenderTabBar() {
    if (m_Tabs.empty()) {
        ImGui::TextDisabled("No hay scripts abiertos.");
        ImGui::TextDisabled("Haz doble clic en cualquier script .lua en el Explorador de Archivos o pulsa 'Editar Script' en el Inspector.");
        return;
    }

    if (ImGui::BeginTabBar("ScriptTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int i = 0; i < static_cast<int>(m_Tabs.size()); ++i) {
            auto& tab = m_Tabs[i];
            if (!tab.isOpen) continue;

            std::string label = tab.title;
            if (tab.editor.IsModified()) {
                label += " *";
            }
            label += "###Tab_" + tab.filePath;

            bool tabOpen = tab.isOpen;
            if (ImGui::BeginTabItem(label.c_str(), &tabOpen)) {
                m_ActiveTabIndex = i;
                tab.editor.Render(("CodeView_" + std::to_string(i)).c_str());
                ImGui::EndTabItem();
            }

            tab.isOpen = tabOpen;
        }
        ImGui::EndTabBar();
    }

    // Limpiar pestañas cerradas
    m_Tabs.erase(
        std::remove_if(m_Tabs.begin(), m_Tabs.end(), [](const ScriptTab& t) { return !t.isOpen; }),
        m_Tabs.end()
    );

    if (m_ActiveTabIndex >= static_cast<int>(m_Tabs.size())) {
        m_ActiveTabIndex = static_cast<int>(m_Tabs.size()) - 1;
    }
}

void ScriptEditorPanel::RenderAutocompletePopup() {
    if (!m_ShowAutocomplete || m_CurrentSuggestions.empty() || m_ActiveTabIndex < 0 || m_ActiveTabIndex >= static_cast<int>(m_Tabs.size())) {
        m_ShowAutocomplete = false;
        return;
    }

    // Controles de navegación en el popup de sugerencias
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        m_SelectedSuggestionIndex--;
        if (m_SelectedSuggestionIndex < 0) {
            m_SelectedSuggestionIndex = static_cast<int>(m_CurrentSuggestions.size()) - 1;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        m_SelectedSuggestionIndex++;
        if (m_SelectedSuggestionIndex >= static_cast<int>(m_CurrentSuggestions.size())) {
            m_SelectedSuggestionIndex = 0;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_ShowAutocomplete = false;
        m_Tabs[m_ActiveTabIndex].editor.DismissAutocomplete();
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        m_ShowAutocomplete = false;
        m_Tabs[m_ActiveTabIndex].editor.DismissAutocomplete();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) || ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        if (m_SelectedSuggestionIndex >= 0 && m_SelectedSuggestionIndex < static_cast<int>(m_CurrentSuggestions.size())) {
            m_Tabs[m_ActiveTabIndex].editor.InsertCompletion(m_CurrentSuggestions[m_SelectedSuggestionIndex].name);
        }
        m_ShowAutocomplete = false;
        return;
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 popupPos = m_AutocompletePos;
    float popupW = 360.0f;
    float popupH = 220.0f;

    float lineH = (m_ActiveTabIndex >= 0 && m_ActiveTabIndex < static_cast<int>(m_Tabs.size())) ? 
                  m_Tabs[m_ActiveTabIndex].editor.GetLineHeight() : 18.0f;

    // Si se sale por abajo de la pantalla, mostrar arriba de la línea del cursor
    if (popupPos.y + popupH > displaySize.y - 20.0f) {
        popupPos.y -= (popupH + lineH + 4.0f);
    }
    // Si se sale por la derecha de la pantalla, mover hacia la izquierda
    if (popupPos.x + popupW > displaySize.x - 10.0f) {
        popupPos.x = displaySize.x - popupW - 10.0f;
    }

    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(popupW, popupH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_HorizontalScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.15f, 0.18f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.50f, 0.85f, 0.8f));

    if (ImGui::Begin("##IntellisensePopup", nullptr, flags)) {
        if (m_AutocompletePrefix.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Sugerencias disponibles (Ctrl+Espacio):");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Sugerencias: '%s'", m_AutocompletePrefix.c_str());
        }
        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(m_CurrentSuggestions.size()); ++i) {
            const auto& sym = m_CurrentSuggestions[i];
            bool isSelected = (i == m_SelectedSuggestionIndex);

            ImVec4 badgeColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            switch (sym.type) {
                case SymbolType::EngineFunction:
                case SymbolType::EngineClass:
                case SymbolType::EngineProperty:
                    badgeColor = ImVec4(0.3f, 0.75f, 1.0f, 1.0f); // Celeste
                    break;
                case SymbolType::GlobalFunction:
                    badgeColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f); // Verde
                    break;
                case SymbolType::ModuleFunction:
                    badgeColor = ImVec4(0.85f, 0.45f, 0.85f, 1.0f); // Violeta
                    break;
                case SymbolType::Variable:
                    badgeColor = ImVec4(1.0f, 0.7f, 0.3f, 1.0f); // Naranja
                    break;
            }

            ImGui::PushID(i);
            ImGui::TextColored(badgeColor, "%s", sym.GetTypeBadge().c_str());
            ImGui::SameLine();

            if (ImGui::Selectable(sym.displayName.c_str(), isSelected)) {
                m_Tabs[m_ActiveTabIndex].editor.InsertCompletion(sym.name);
                m_ShowAutocomplete = false;
                ImGui::PopID();
                break;
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(badgeColor, "%s %s", sym.GetTypeBadge().c_str(), sym.displayName.c_str());
                if (!sym.signature.empty()) {
                    ImGui::TextDisabled("Firma: %s", sym.signature.c_str());
                }
                if (!sym.description.empty()) {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", sym.description.c_str());
                }
                if (!sym.sourceFile.empty()) {
                    ImGui::TextDisabled("Origen: %s:%d", SlipFileSystem::GetFileName(sym.sourceFile).c_str(), sym.line);
                }
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

