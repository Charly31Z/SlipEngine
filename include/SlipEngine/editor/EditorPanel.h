#pragma once
#include <string>
#include <vector>

class SlipLevel;
class SlipEntity;

class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    // Se ejecuta cada frame dentro del ciclo ImGui
    virtual void OnImGuiRender() = 0;

    // Actualiza la referencia a la escena activa o entidad seleccionada
    virtual void SetContext(SlipLevel* level) { m_ContextLevel = level; }
    virtual void SetSelectedEntity(SlipEntity* entity) {
        m_SelectedEntity = entity;
        m_Selection.clear();
        if (entity) m_Selection.push_back(entity);
    }
    virtual void SetSelectedAsset(const std::string& assetPath) { m_SelectedAssetPath = assetPath; }

    // Multi-selección: sincroniza el conjunto completo (la primaria es la última clickeada)
    virtual void ApplySelection(const std::vector<SlipEntity*>& selection, SlipEntity* primary) {
        m_Selection = selection;
        m_SelectedEntity = primary;
    }

    const std::vector<SlipEntity*>& GetSelection() const { return m_Selection; }
    bool IsEntitySelected(SlipEntity* entity) const {
        if (!entity) return false;
        for (SlipEntity* e : m_Selection) {
            if (e == entity) return true;
        }
        return false;
    }

    const std::string& GetSelectedAsset() const { return m_SelectedAssetPath; }

    bool IsOpen() const { return m_IsOpen; }
    void SetOpen(bool open) { m_IsOpen = open; }

protected:
    bool m_IsOpen = true;
    SlipLevel* m_ContextLevel = nullptr;
    SlipEntity* m_SelectedEntity = nullptr;
    std::vector<SlipEntity*> m_Selection;
    std::string m_SelectedAssetPath;
};
