#pragma once

#ifndef SLIP_PROJECT_SETTINGS_PANEL_H
#define SLIP_PROJECT_SETTINGS_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include <string>

class ProjectSettingsPanel : public EditorPanel {
public:
    ProjectSettingsPanel();
    ~ProjectSettingsPanel() override = default;

    void OnImGuiRender() override;
    void SaveAllSettings();

private:
    void RenderGeneralCategory();
    void RenderGraphicsCategory();
    void RenderPhysicsCategory();
    void RenderInputCategory();
    void DrawBindingControl(const char* label, InputBinding& binding, int listenKind, int index, int slot);
    void ClearInputListen();

    int m_SelectedCategory = 0; // 0: General, 1: Gráficos, 2: Física, 3: Entrada
    int m_ListenKind = 0;       // 0: ninguno, 1: acción, 2: eje
    int m_ListenIndex = -1;
    int m_ListenSlot = -1;
};

#endif // !SLIP_PROJECT_SETTINGS_PANEL_H

