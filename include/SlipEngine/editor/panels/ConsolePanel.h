#pragma once
#include "SlipEngine/editor/EditorPanel.h"
#include <functional>

class ConsolePanel : public EditorPanel {
public:
    using SelectionCallback = std::function<void(SlipEntity*)>;

    ConsolePanel(SelectionCallback onSelect = nullptr) : m_OnSelectCallback(onSelect) {}
    void OnImGuiRender() override;

private:
    SelectionCallback m_OnSelectCallback;
};