#pragma once

#ifndef HIERARCHY_PANEL_H
#define HIERARCHY_PANEL_H

#include "SlipEngine/editor/EditorPanel.h"
#include <functional>
#include <string>

class SlipEntity;

class HierarchyPanel : public EditorPanel {
public:
    using SelectionCallback = std::function<void(SlipEntity*)>;
    using MultiSelectionCallback = std::function<void(const std::vector<SlipEntity*>&, SlipEntity*)>;

    HierarchyPanel(SelectionCallback onSelect = nullptr);
    void OnImGuiRender() override;

    void DuplicateSelected();
    void CopySelected();
    void CutSelected();
    void Paste();
    void SaveSelectedAsPrefab();

    void SetMultiSelectionCallback(MultiSelectionCallback cb) { m_OnMultiSelectCallback = cb; }

private:
    // Aplica la selección localmente y notifica al editor para propagarla al resto de paneles
    void SelectEntities(const std::vector<SlipEntity*>& selection, SlipEntity* primary);

    SelectionCallback m_OnSelectCallback;
    MultiSelectionCallback m_OnMultiSelectCallback;
    std::vector<SlipEntity*> m_OrderedEntities;
};

#endif // !HIERARCHY_PANEL_H
