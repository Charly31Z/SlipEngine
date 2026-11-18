#pragma once
#include "SlipEngine/editor/EditorPanel.h"
#include <functional>

class InspectorPanel : public EditorPanel {
public:
    using SelectionCallback = std::function<void(SlipEntity*)>;

    InspectorPanel(SelectionCallback onSelect = nullptr);
    void OnImGuiRender() override;

private:
    SelectionCallback m_OnSelectCallback;
};