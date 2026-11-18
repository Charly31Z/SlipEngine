#pragma once

#ifndef PREFAB_EDITOR_PANEL_H
#define PREFAB_EDITOR_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include <string>
#include <memory>

class SlipEntity;
class SlipLevel;

class PrefabEditorPanel : public EditorPanel {
public:
    PrefabEditorPanel();
    ~PrefabEditorPanel() override;

    void OnImGuiRender() override;

    void OpenPrefab(const std::string& filePath);
    void SavePrefab();
    void ClosePrefab();

    bool IsPrefabOpen() const { return m_RootEntity != nullptr; }

private:
    void RenderPrefabHierarchy();
    void RenderPrefabInspector();

    std::string m_PrefabPath;
    std::string m_PrefabName;
    std::unique_ptr<SlipLevel> m_PrefabLevel;
    SlipEntity* m_RootEntity = nullptr;
    SlipEntity* m_SelectedEntity = nullptr;
    bool m_IsModified = false;
};

#endif // !PREFAB_EDITOR_PANEL_H

