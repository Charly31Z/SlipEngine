#include "SlipEngine/scene/SlipSortingLayers.h"
#include <algorithm>

SlipSortingLayers& SlipSortingLayers::Get()
{
    static SlipSortingLayers instance;
    return instance;
}

SlipSortingLayers::SlipSortingLayers()
{
    ResetToDefaults();
}

void SlipSortingLayers::ResetToDefaults()
{
    m_Layers = {
        "Default",
        "Background",
        "Middleground",
        "Characters",
        "Foreground",
        "Effects",
        "UI"
    };
}

void SlipSortingLayers::SetLayers(const std::vector<std::string>& layers)
{
    if (!layers.empty()) {
        m_Layers = layers;
    }
}

void SlipSortingLayers::AddLayer(const std::string& name)
{
    if (name.empty() || HasLayer(name)) return;
    m_Layers.push_back(name);
}

void SlipSortingLayers::RemoveLayer(const std::string& name)
{
    if (name == "Default") return; // "Default" no puede ser eliminada
    auto it = std::find(m_Layers.begin(), m_Layers.end(), name);
    if (it != m_Layers.end()) {
        m_Layers.erase(it);
    }
}

bool SlipSortingLayers::RenameLayer(size_t index, const std::string& newName)
{
    if (index >= m_Layers.size() || newName.empty()) return false;
    if (m_Layers[index] == "Default") return false; // Default protegido
    if (HasLayer(newName)) return false;
    m_Layers[index] = newName;
    return true;
}

int SlipSortingLayers::GetLayerIndex(const std::string& name) const
{
    for (size_t i = 0; i < m_Layers.size(); ++i) {
        if (m_Layers[i] == name) return static_cast<int>(i);
    }
    return 0; // Por defecto es 0 ("Default")
}

std::string SlipSortingLayers::GetLayerName(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
        return m_Layers[index];
    }
    return "Default";
}

bool SlipSortingLayers::HasLayer(const std::string& name) const
{
    return std::find(m_Layers.begin(), m_Layers.end(), name) != m_Layers.end();
}

