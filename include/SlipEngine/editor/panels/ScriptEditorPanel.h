#pragma once

#ifndef SCRIPT_EDITOR_PANEL_H
#define SCRIPT_EDITOR_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/editor/texteditor/TextEditor.h"
#include "SlipEngine/editor/texteditor/SlipScriptIndexer.h"
#include <vector>
#include <string>
#include <memory>

struct ScriptTab {
    std::string filePath;
    std::string title;
    SlipTextEditor editor;
    bool isOpen = true;
};

class ScriptEditorPanel : public EditorPanel {
public:
    // Nombre de la ventana ImGui del panel (usado para docking y focus programático)
    inline static const char* WINDOW_NAME = "Editor de Scripts Lua";

    ScriptEditorPanel();
    ~ScriptEditorPanel() override;

    void OnImGuiRender() override;

    // Abrir un archivo .lua en una pestaña
    void OpenScript(const std::string& filePath, int targetLine = 1);

    // Guardar el script de la pestaña actual
    void SaveActiveScript();

    // Guardar un script específico
    void SaveScript(ScriptTab& tab);

private:
    void RenderTabBar();
    void RenderAutocompletePopup();

    std::vector<ScriptTab> m_Tabs;
    int m_ActiveTabIndex = -1;

    // Estado del Autocompletado / Intellisense
    bool m_ShowAutocomplete = false;
    std::string m_AutocompletePrefix;
    ImVec2 m_AutocompletePos;
    int m_SelectedSuggestionIndex = 0;
    std::vector<ScriptSymbol> m_CurrentSuggestions;
};

#endif // !SCRIPT_EDITOR_PANEL_H

